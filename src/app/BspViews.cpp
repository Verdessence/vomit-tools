//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See BspViews.h.
//
//===========================================================================//
#include "BspViews.h"

#include <algorithm>

#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBlock>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "FileDialogs.h"
#include "bsp/Bsp.h"
#include "bsp/Stripper.h"
#include "vfs/Vfs.h"

namespace
{

enum EntityColumn_t
{
	ENTITY_COLUMN_INDEX,
	ENTITY_COLUMN_CLASS,
	ENTITY_COLUMN_NAME,
	ENTITY_COLUMN_ORIGIN,
	ENTITY_COLUMN_COUNT
};

// Rows Stripper changed; readable on light and dark backgrounds.
const QColor REMOVED_COLOUR( 200, 80, 80 );
const QColor ADDED_COLOUR( 70, 160, 80 );
const QColor MODIFIED_COLOUR( 200, 140, 40 );

constexpr int ROLE_SHOWN_INDEX = Qt::UserRole + 1; // the row's place in m_Shown

//-----------------------------------------------------------------------------
// Purpose: Stripper configs as the parser reads them: section words, keys (a pair's first
//          string), values (regular expressions apart), braces, and comments from ;, # or //.
//-----------------------------------------------------------------------------
class CStripperHighlighter : public CCodeHighlighter
{
public:
	using CCodeHighlighter::CCodeHighlighter;

protected:
	void highlightBlock( const QString &szText ) override
	{
		const CodeColors_t &colors = Colors();
		const QTextCharFormat section = Format( colors.m_Keyword, true );
		const QTextCharFormat key = Format( colors.m_Key );
		const QTextCharFormat value = Format( colors.m_String );
		const QTextCharFormat regex = Format( colors.m_Special );
		const QTextCharFormat brace = Format( colors.m_Brace, true );
		const QTextCharFormat comment = Format( colors.m_Comment, false, true );
		const auto n = static_cast<int>( szText.size() );
		int nStrings = 0;
		for ( int i = 0; i < n; )
		{
			const QChar c = szText[ i ];
			if ( c.isSpace() )
			{
				i++;
			}
			else if ( c == u';' || c == u'#' || ( c == u'/' && i + 1 < n && szText[ i + 1 ] == u'/' ) )
			{
				setFormat( i, n - i, comment );
				break;
			}
			else if ( c == u'{' || c == u'}' )
			{
				setFormat( i, 1, brace );
				nStrings = 0;
				i++;
			}
			else if ( c == u'"' )
			{
				const qsizetype nEnd = szText.indexOf( u'"', i + 1 );
				const int nStop = nEnd < 0 ? n : static_cast<int>( nEnd ) + 1;
				const QStringView inside = QStringView( szText ).mid( i + 1, std::max( 0, nStop - i - 2 ) );
				const bool bRegex = nStrings % 2 == 1 && inside.size() >= 2 && inside.startsWith( u'/' ) && inside.endsWith( u'/' );
				setFormat( i, nStop - i, nStrings % 2 == 0 ? key : bRegex ? regex
																		  : value );
				nStrings++;
				i = nStop;
			}
			else
			{
				int nEnd = i;
				while ( nEnd < n && !szText[ nEnd ].isSpace() && szText[ nEnd ] != u'"' && szText[ nEnd ] != u'{' && szText[ nEnd ] != u'}' )
				{
					nEnd++;
				}
				if ( szText[ nEnd - 1 ] == u':' )
				{
					setFormat( i, nEnd - i, section );
				}
				i = nEnd;
			}
		}
	}
};

// What Stripper does with each section word, for the notes on hovering one.
struct StripperWordNote_t
{
	const char *m_pszWord;
	const char *m_pszNote;
};

constexpr StripperWordNote_t STRIPPER_WORD_NOTES[] = {
	{ "add:", QT_TRANSLATE_NOOP( "CEntityView", "add: each block after it is a new entity, made as the map loads." ) },
	{ "filter:", QT_TRANSLATE_NOOP( "CEntityView", "filter: each block after it removes every entity that has all of its pairs. A value between slashes (/.../) is a regular expression." ) },
	{ "remove:", QT_TRANSLATE_NOOP( "CEntityView", "remove: the same as filter:. Each block removes every entity that has all of its pairs." ) },
	{ "modify:", QT_TRANSLATE_NOOP( "CEntityView", "modify: each block after it changes the entities its match: block finds, with replace:, delete: and insert:." ) },
	{ "match:", QT_TRANSLATE_NOOP( "CEntityView", "match: the pairs an entity must all have for this modify to change it. A value between slashes (/.../) is a regular expression." ) },
	{ "replace:", QT_TRANSLATE_NOOP( "CEntityView", "replace: new values for keys the entity already has." ) },
	{ "delete:", QT_TRANSLATE_NOOP( "CEntityView", "delete: pairs taken off the entity, each a key with this value (or a value its /regular expression/ finds)." ) },
	{ "insert:", QT_TRANSLATE_NOOP( "CEntityView", "insert: pairs added to the entity." ) },
};

//-----------------------------------------------------------------------------
// Purpose: Whether a key's values are outputs ("target,input,parameter,delay,times").
//-----------------------------------------------------------------------------
bool IsOutputValue( std::string_view szValue )
{
	return std::ranges::count( szValue, ',' ) >= 4 || std::ranges::count( szValue, '\x1b' ) >= 4;
}

enum LumpColumn_t
{
	LUMP_COLUMN_INDEX,
	LUMP_COLUMN_NAME,
	LUMP_COLUMN_SIZE,
	LUMP_COLUMN_OFFSET,
	LUMP_COLUMN_VERSION,
	LUMP_COLUMN_COMPRESSED,
	LUMP_COLUMN_COUNT
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view sz )
{
	return QString::fromUtf8( sz.data(), static_cast<qsizetype>( sz.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: A tree item that sorts its number columns as numbers.
//-----------------------------------------------------------------------------
class CNumberItem : public QTreeWidgetItem
{
public:
	using QTreeWidgetItem::QTreeWidgetItem;

	bool operator<( const QTreeWidgetItem &other ) const override
	{
		const int nColumn = treeWidget() ? treeWidget()->sortColumn() : 0;
		const QVariant left = data( nColumn, Qt::UserRole );
		const QVariant right = other.data( nColumn, Qt::UserRole );
		if ( left.isValid() && right.isValid() )
		{
			return left.toLongLong() < right.toLongLong();
		}
		return text( nColumn ).compare( other.text( nColumn ), Qt::CaseInsensitive ) < 0;
	}
};

} // namespace

// The map's keys and their values, most used first, for suggesting what to type in a config.
class CStripperWords
{
public:
	explicit CStripperWords( const std::vector<xv::bsp::Entity_t> &entities )
	{
		// Counted by key in any case (Stripper matches keys so), shown as the map first spells them.
		std::unordered_map<QString, std::pair<QString, int>> keys;
		std::unordered_map<QString, std::unordered_map<QString, int>> values;
		std::unordered_map<QString, int> inputs;
		for ( const auto &entity : entities )
		{
			for ( const auto &[ szKey, szValue ] : entity.m_KeyValues )
			{
				const QString szName = Qs( szKey );
				const QString szLower = szName.toLower();
				auto &[ szSpelling, nCount ] = keys[ szLower ];
				if ( szSpelling.isEmpty() )
				{
					szSpelling = szName;
				}
				nCount++;
				values[ szLower ][ Qs( szValue ) ]++;
				if ( IsOutputValue( szValue ) )
				{
					m_Outputs.insert( szLower );
					const char chSeparator = szValue.find( '\x1b' ) != std::string::npos ? '\x1b' : ',';
					const std::size_t nFirst = szValue.find( chSeparator );
					const std::size_t nSecond = szValue.find( chSeparator, nFirst + 1 );
					if ( nSecond != std::string::npos )
					{
						inputs[ Qs( std::string_view( szValue ).substr( nFirst + 1, nSecond - nFirst - 1 ) ) ]++;
					}
				}
			}
		}
		// Keys every config uses, even on a map without them.
		for ( const char *pszKey : { "classname", "targetname", "hammerid", "origin", "angles", "model", "parentname", "spawnflags" } )
		{
			keys.try_emplace( QString::fromLatin1( pszKey ), QString::fromLatin1( pszKey ), 0 );
		}
		std::vector<std::pair<QString, int>> sorted;
		for ( const auto &[ szLower, spelling ] : keys )
		{
			sorted.push_back( spelling );
		}
		m_Keys = MostUsedFirst( std::move( sorted ) );
		for ( auto &[ szKey, counts ] : values )
		{
			m_Counts[ szKey ] = counts;
		}
		m_Inputs = MostUsedFirst( { inputs.begin(), inputs.end() } );
		const auto names = values.find( QStringLiteral( "targetname" ) );
		std::vector<std::pair<QString, int>> targets = names == values.end() ? std::vector<std::pair<QString, int>>{} : std::vector<std::pair<QString, int>>( names->second.begin(), names->second.end() );
		m_Targets = MostUsedFirst( std::move( targets ) );
		for ( const char *pszSpecial : { "!activator", "!caller", "!self", "!player" } )
		{
			m_Targets.push_back( QString::fromLatin1( pszSpecial ) );
		}
	}

	[[nodiscard]] const QStringList &GetKeys() const { return m_Keys; }
	[[nodiscard]] const QStringList &GetTargets() const { return m_Targets; }
	[[nodiscard]] const QStringList &GetInputs() const { return m_Inputs; }
	[[nodiscard]] bool IsOutput( const QString &szKey ) const
	{
		const QString szLower = szKey.toLower();
		// Outputs the map doesn't use are still named On... by every game's FGD.
		return m_Outputs.contains( szLower ) || ( szLower.startsWith( QStringLiteral( "on" ) ) && szKey.size() > 2 && szKey[ 2 ].isUpper() );
	}

	// A key's values in the map, most used first (names for keys that hold one).
	[[nodiscard]] QStringList GetValues( const QString &szKey ) const
	{
		static const QStringList NAME_KEYS = { QStringLiteral( "target" ), QStringLiteral( "parentname" ), QStringLiteral( "filtername" ), QStringLiteral( "damagefilter" ), QStringLiteral( "lightingorigin" ), QStringLiteral( "landmark" ) };
		const QString szLower = szKey.toLower();
		const auto it = m_Counts.find( szLower );
		QStringList words = it == m_Counts.end() ? QStringList{} : MostUsedFirst( { it->second.begin(), it->second.end() } );
		if ( NAME_KEYS.contains( szLower ) )
		{
			for ( const QString &szName : m_Targets )
			{
				if ( !words.contains( szName ) )
				{
					words.push_back( szName );
				}
			}
		}
		constexpr qsizetype MAX_VALUES = 3000;
		if ( words.size() > MAX_VALUES )
		{
			words.resize( MAX_VALUES );
		}
		return words;
	}

private:
	[[nodiscard]] static QStringList MostUsedFirst( std::vector<std::pair<QString, int>> counts )
	{
		std::ranges::sort( counts, []( const auto &a, const auto &b ) { return a.second != b.second ? a.second > b.second : a.first.compare( b.first, Qt::CaseInsensitive ) < 0; } );
		QStringList words;
		words.reserve( static_cast<qsizetype>( counts.size() ) );
		for ( auto &[ szWord, nCount ] : counts )
		{
			if ( !szWord.isEmpty() )
			{
				words.push_back( std::move( szWord ) );
			}
		}
		return words;
	}

	QStringList m_Keys;
	QStringList m_Targets;
	QStringList m_Inputs;
	std::unordered_set<QString> m_Outputs;
	std::unordered_map<QString, std::unordered_map<QString, int>> m_Counts;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CEntityView::~CEntityView() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CStripperWords &CEntityView::Words()
{
	if ( !m_pWords )
	{
		m_pWords = std::make_unique<CStripperWords>( m_Bsp.GetEntities() );
	}
	return *m_pWords;
}

//-----------------------------------------------------------------------------
// Purpose: What may be typed at the cursor: section words, a modify's parts, the map's keys, and
//          a key's values (names for outputs' targets, then inputs).
//-----------------------------------------------------------------------------
CodeCompletion_t CEntityView::CompleteConfig( const QTextCursor &cursor )
{
	using Kind_t = xv::bsp::StripperCursor_t::Kind_t;
	const QString szBefore = m_pConfigText->toPlainText().left( cursor.position() );
	const QByteArray bytes = szBefore.toUtf8();
	const xv::bsp::StripperCursor_t at = xv::bsp::StripperCursorAt( std::string_view( bytes.constData(), static_cast<std::size_t>( bytes.size() ) ) );
	CodeCompletion_t completion;
	const QString szTyped = QString::fromUtf8( bytes.mid( static_cast<qsizetype>( at.m_nStart ) ) );
	completion.m_nStart = cursor.position() - static_cast<int>( szTyped.size() );
	switch ( at.m_nKind )
	{
		case Kind_t::KIND_SECTION:
			completion.m_Words = { QStringLiteral( "add:" ), QStringLiteral( "filter:" ), QStringLiteral( "modify:" ), QStringLiteral( "remove:" ) };
			break;
		case Kind_t::KIND_MODIFY_PART:
			completion.m_Words = { QStringLiteral( "match:" ), QStringLiteral( "replace:" ), QStringLiteral( "delete:" ), QStringLiteral( "insert:" ) };
			break;
		case Kind_t::KIND_KEY:
			completion.m_Words = Words().GetKeys();
			completion.m_szSuffix = QStringLiteral( "\"" );
			break;
		case Kind_t::KIND_VALUE:
		{
			const QString szKey = Qs( at.m_szKey );
			if ( Words().IsOutput( szKey ) )
			{
				// target,input,parameter,delay,times: suggest for the field being typed.
				const QChar chSeparator = szTyped.contains( QChar( 0x1b ) ) ? QChar( 0x1b ) : QChar( u',' );
				const qsizetype nField = szTyped.count( chSeparator );
				completion.m_nStart = cursor.position() - static_cast<int>( szTyped.size() - szTyped.lastIndexOf( chSeparator ) - 1 );
				completion.m_Words = nField == 0 ? Words().GetTargets() : nField == 1 ? Words().GetInputs()
																					  : QStringList{};
				completion.m_szSuffix = nField < 2 ? QString( chSeparator ) : QString{};
			}
			else
			{
				completion.m_Words = Words().GetValues( szKey );
				completion.m_szSuffix = QStringLiteral( "\"" );
			}
			break;
		}
		default: break;
	}
	return completion;
}

//-----------------------------------------------------------------------------
// Purpose: A note on what's under the pointer: what a section word does, how many of the map's
//          entities have a key, or a pair (a /regular expression/ tried as Stripper would).
//-----------------------------------------------------------------------------
QString CEntityView::DescribeConfig( const QTextCursor &cursor )
{
	const QString szLine = cursor.block().text();
	const int nColumn = cursor.positionInBlock();
	// The pair the cursor is in, from the line's strings.
	QStringList strings;
	int nIn = -1;
	for ( qsizetype i = 0; i < szLine.size(); )
	{
		const QChar c = szLine[ i ];
		if ( c == u';' || c == u'#' || ( c == u'/' && i + 1 < szLine.size() && szLine[ i + 1 ] == u'/' ) )
		{
			break;
		}
		if ( c == u'"' )
		{
			const qsizetype nEnd = szLine.indexOf( u'"', i + 1 );
			const qsizetype nStop = nEnd < 0 ? szLine.size() : nEnd;
			if ( nColumn >= i && nColumn <= nStop )
			{
				nIn = static_cast<int>( strings.size() );
			}
			strings.push_back( szLine.mid( i + 1, nStop - i - 1 ) );
			i = nStop + 1;
			continue;
		}
		if ( !c.isSpace() && c != u'{' && c != u'}' )
		{
			qsizetype nEnd = i;
			while ( nEnd < szLine.size() && !szLine[ nEnd ].isSpace() && szLine[ nEnd ] != u'"' && szLine[ nEnd ] != u'{' && szLine[ nEnd ] != u'}' )
			{
				nEnd++;
			}
			if ( nColumn >= i && nColumn <= nEnd )
			{
				const QString szWord = szLine.mid( i, nEnd - i ).toLower();
				for ( const auto &note : STRIPPER_WORD_NOTES )
				{
					if ( szWord == QLatin1String( note.m_pszWord ) )
					{
						return QCoreApplication::translate( "CEntityView", note.m_pszNote );
					}
				}
				return {};
			}
			i = nEnd;
			continue;
		}
		i++;
	}
	const auto &entities = m_Bsp.GetEntities();
	if ( nIn < 0 || entities.empty() )
	{
		return {};
	}
	const int nKey = nIn - nIn % 2;
	const std::string szKey = strings[ nKey ].toStdString();
	if ( nIn == nKey || nKey + 1 >= strings.size() )
	{
		const auto nHave = std::ranges::count_if( entities, [ &szKey ]( const xv::bsp::Entity_t &entity ) {
			xv::bsp::Entity_t match;
			match.m_KeyValues.emplace_back( szKey, "/.*/" );
			return xv::bsp::StripperMatches( match, entity );
		} );
		return tr( "Entities in the map with the key \"%1\": %2" ).arg( strings[ nKey ] ).arg( nHave );
	}
	xv::bsp::Entity_t match;
	match.m_KeyValues.emplace_back( szKey, strings[ nKey + 1 ].toStdString() );
	const auto nMatch = std::ranges::count_if( entities, [ &match ]( const xv::bsp::Entity_t &entity ) { return xv::bsp::StripperMatches( match, entity ); } );
	return nMatch == 0 ? tr( "No entity in the map has \"%1\" \"%2\"." ).arg( strings[ nKey ], strings[ nKey + 1 ] )
					   : tr( "Entities in the map with \"%1\" \"%2\": %3" ).arg( strings[ nKey ], strings[ nKey + 1 ] ).arg( nMatch );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CEntityView::CEntityView( const xv::bsp::CBsp &bsp, const std::filesystem::path &map, QWidget *pParent )
	: QWidget( pParent ),
	  m_Bsp( bsp ),
	  m_Configs( xv::bsp::FindStripperConfigs( map ) ),
	  m_ConfigPath( xv::bsp::StripperMapConfigPath( map ) )
{
	m_pSearch = new QLineEdit;
	m_pSearch->setPlaceholderText( tr( "Search entities… (class, name, or any key or value)" ) );
	m_pSearch->setClearButtonEnabled( true );
	connect( m_pSearch, &QLineEdit::textChanged, this, &CEntityView::SetSearch );

	// Stripper: the map's entities as its configs leave them when it loads.
	m_pStripper = new QCheckBox( tr( "Stripper" ) );
	m_pStripper->setObjectName( QStringLiteral( "entityStripper" ) );
	m_pStripper->setToolTip( tr( "Show the entities as Stripper:Source changes them when the map loads (addons/stripper)" ) );
	connect( m_pStripper, &QCheckBox::toggled, this, &CEntityView::Rebuild );
	auto *pChoose = new QPushButton( tr( "Config…" ) );
	pChoose->setObjectName( QStringLiteral( "entityStripperChoose" ) );
	pChoose->setToolTip( tr( "Preview another Stripper config on this map" ) );
	connect( pChoose, &QPushButton::clicked, this, [ this ] {
		const QString szStart = m_Configs.empty() ? QString{} : QString::fromStdU16String( m_Configs.back().parent_path().u16string() );
		const QString szFile = AskOpenFile( this, tr( "Stripper Config" ), szStart, tr( "Stripper configs (*.cfg);;All files (*)" ) );
		if ( !szFile.isEmpty() )
		{
			if ( m_bConfigModified && QMessageBox::question( this, tr( "Stripper Config" ), tr( "Drop the unsaved changes to %1?" ).arg( QString::fromStdU16String( m_ConfigPath.filename().u16string() ) ) ) != QMessageBox::Yes )
			{
				return;
			}
			m_ConfigPath = std::filesystem::path( szFile.toStdU16String() );
			SetConfigModified( false );
			LoadConfigText();
			SetStripperConfigs( { m_ConfigPath } );
			m_pStripper->setChecked( true ); // a config picked by hand is meant to be seen
		}
	} );
	m_pConfigs = new QLabel;
	m_pConfigs->setObjectName( QStringLiteral( "entityStripperConfigs" ) );
	m_pConfigs->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pEdit = new QPushButton( tr( "Edit Config" ) );
	m_pEdit->setObjectName( QStringLiteral( "entityStripperEdit" ) );
	m_pEdit->setCheckable( true );
	m_pEdit->setEnabled( !m_ConfigPath.empty() );
	m_pEdit->setToolTip( m_ConfigPath.empty() ? tr( "This map isn't in a game folder, so there's nowhere for its Stripper config" )
											  : tr( "Edit %1, seeing the changes in the list as you type" ).arg( QString::fromStdU16String( m_ConfigPath.u16string() ) ) );
	connect( m_pEdit, &QPushButton::toggled, this, &CEntityView::ShowConfigEditor );
	auto *pStripperBar = new QHBoxLayout;
	pStripperBar->addWidget( m_pStripper );
	pStripperBar->addWidget( m_pConfigs, 1 );
	pStripperBar->addWidget( m_pEdit );
	pStripperBar->addWidget( pChoose );
	// Edits saved in another editor show at once (and in the editor here, when it has none of its own).
	m_pWatcher = new QFileSystemWatcher( this );
	connect( m_pWatcher, &QFileSystemWatcher::fileChanged, this, [ this ] {
		if ( !m_bConfigModified )
		{
			LoadConfigText();
		}
		SetStripperConfigs( m_Configs );
	} );

	// The config editor: its text, saved with Save; the list follows what's typed.
	m_pConfigPath = new QLabel;
	m_pConfigPath->setWordWrap( true );
	m_pConfigPath->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pConfigText = new CCodeEdit;
	m_pConfigText->setObjectName( QStringLiteral( "entityStripperText" ) );
	m_pConfigText->SetCompletionSource( [ this ]( const QTextCursor &cursor ) { return CompleteConfig( cursor ); } );
	m_pConfigText->SetHoverSource( [ this ]( const QTextCursor &cursor ) { return DescribeConfig( cursor ); } );
	m_pConfigText->SetBlockWords( { QStringLiteral( "add:" ), QStringLiteral( "filter:" ), QStringLiteral( "remove:" ), QStringLiteral( "modify:" ), QStringLiteral( "match:" ), QStringLiteral( "replace:" ), QStringLiteral( "delete:" ), QStringLiteral( "insert:" ) } );
	new CStripperHighlighter( m_pConfigText );
	m_pReparse = new QTimer( this );
	m_pReparse->setSingleShot( true );
	m_pReparse->setInterval( 300 );
	connect( m_pReparse, &QTimer::timeout, this, [ this ] { SetStripperConfigs( m_Configs ); } );
	connect( m_pConfigText, &QPlainTextEdit::textChanged, this, [ this ] {
		if ( !m_bLoadingText )
		{
			SetConfigModified( true );
			m_pStripper->setChecked( true );
			m_pReparse->start();
		}
	} );
	auto *pSave = new QPushButton( tr( "Save" ) );
	pSave->setObjectName( QStringLiteral( "entityStripperSave" ) );
	connect( pSave, &QPushButton::clicked, this, &CEntityView::SaveConfig );
	auto *pRevert = new QPushButton( tr( "Revert" ) );
	pRevert->setToolTip( tr( "Go back to the file as it's saved" ) );
	connect( pRevert, &QPushButton::clicked, this, [ this ] {
		SetConfigModified( false );
		LoadConfigText();
		SetStripperConfigs( m_Configs );
	} );
	auto *pEditorButtons = new QHBoxLayout;
	pEditorButtons->addWidget( m_pConfigPath, 1 );
	pEditorButtons->addWidget( pRevert );
	pEditorButtons->addWidget( pSave );
	m_pEditor = new QWidget;
	auto *pEditorLayout = new QVBoxLayout( m_pEditor );
	pEditorLayout->setContentsMargins( 0, 0, 0, 0 );
	pEditorLayout->addLayout( pEditorButtons );
	pEditorLayout->addWidget( m_pConfigText, 1 );
	m_pEditor->hide();

	m_pList = new QTreeWidget;
	m_pList->setObjectName( QStringLiteral( "entityList" ) );
	m_pList->setColumnCount( ENTITY_COLUMN_COUNT );
	m_pList->setHeaderLabels( { tr( "#" ), tr( "Class" ), tr( "Name" ), tr( "Origin" ) } );
	m_pList->setRootIsDecorated( false );
	m_pList->setUniformRowHeights( true );
	m_pList->setSortingEnabled( true );
	m_pList->header()->setStretchLastSection( true );
	m_pList->setColumnWidth( ENTITY_COLUMN_INDEX, 60 );
	m_pList->setColumnWidth( ENTITY_COLUMN_CLASS, 200 );
	m_pList->setColumnWidth( ENTITY_COLUMN_NAME, 160 );
	// Rebuilds keep the user's sorting; it starts by index, ascending (Qt's default is descending).
	m_pList->sortByColumn( ENTITY_COLUMN_INDEX, Qt::AscendingOrder );
	m_pList->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pList, &QTreeWidget::customContextMenuRequested, this, &CEntityView::ShowListMenu );
	connect( m_pList, &QTreeWidget::currentItemChanged, this, [ this ]( QTreeWidgetItem *pCurrent ) {
		ShowKeyValues( pCurrent ? pCurrent->data( ENTITY_COLUMN_INDEX, ROLE_SHOWN_INDEX ).toInt() : -1 );
	} );

	m_pKeyValues = new QTableWidget( 0, 2 );
	m_pKeyValues->setHorizontalHeaderLabels( { tr( "Key" ), tr( "Value" ) } );
	m_pKeyValues->horizontalHeader()->setStretchLastSection( true );
	m_pKeyValues->verticalHeader()->setVisible( false );
	m_pKeyValues->setEditTriggers( QAbstractItemView::NoEditTriggers );
	m_pKeyValues->setColumnWidth( 0, 180 );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( m_pList );
	pSplitter->addWidget( m_pKeyValues );
	pSplitter->addWidget( m_pEditor );
	pSplitter->setSizes( { 560, 540, 0 } );

	// The configs' problems in a panel of their own under the views, so a problem appearing or going
	// away as the config is typed never moves the editor.
	m_pSummary = new QLabel;
	m_pSummary->setObjectName( QStringLiteral( "entityStripperSummary" ) );
	m_pSummary->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pProblems = new QTreeWidget;
	m_pProblems->setObjectName( QStringLiteral( "entityStripperProblems" ) );
	m_pProblems->setColumnCount( 3 );
	m_pProblems->setHeaderLabels( { tr( "Line" ), tr( "Problem" ), tr( "Config" ) } );
	m_pProblems->setRootIsDecorated( false );
	m_pProblems->setUniformRowHeights( true );
	m_pProblems->setColumnWidth( 0, 60 );
	m_pProblems->setColumnWidth( 1, 520 );
	m_pProblems->setToolTip( tr( "Double-click a problem in the config being edited to go to its line" ) );
	connect( m_pProblems, &QTreeWidget::itemActivated, this, [ this ]( QTreeWidgetItem *pItem ) { GoToProblem( pItem ); } );
	m_pProblemsPanel = new QWidget;
	auto *pProblemsLayout = new QVBoxLayout( m_pProblemsPanel );
	pProblemsLayout->setContentsMargins( 0, 0, 0, 0 );
	pProblemsLayout->setSpacing( 2 );
	pProblemsLayout->addWidget( m_pSummary );
	pProblemsLayout->addWidget( m_pProblems, 1 );

	auto *pMain = new QSplitter( Qt::Vertical );
	pMain->addWidget( pSplitter );
	pMain->addWidget( m_pProblemsPanel );
	pMain->setStretchFactor( 0, 1 );
	pMain->setStretchFactor( 1, 0 );
	pMain->setCollapsible( 0, false );
	pMain->setSizes( { 640, 120 } );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 6, 6, 6, 0 );
	if ( !bsp.HasEntities() && !map.empty() )
	{
		pLayout->addWidget( new QLabel( tr( "This map's entity lump is compressed and wouldn't unpack; it may be damaged." ) ) );
	}
	pLayout->addLayout( pStripperBar );
	pLayout->addWidget( m_pSearch );
	pLayout->addWidget( pMain, 1 );
	LoadConfigText();
	SetStripperConfigs( m_Configs );
}

//-----------------------------------------------------------------------------
// Purpose: Reads the configs again (they may have changed on disk) and redraws the list.
//-----------------------------------------------------------------------------
void CEntityView::SetStripperConfigs( std::vector<std::filesystem::path> configs )
{
	m_Configs = std::move( configs );
	// The config being edited, with changes typed, joins the list where it runs: after the global one.
	if ( m_bConfigModified && std::ranges::find( m_Configs, m_ConfigPath ) == m_Configs.end() )
	{
		m_Configs.push_back( m_ConfigPath );
	}
	m_Parsed.clear();
	QStringList names;
	bool bDiagnosed = false;
	for ( const auto &file : m_Configs )
	{
		std::string szText;
		if ( ( m_bConfigModified || m_bConfigReadOnly ) && file == m_ConfigPath )
		{
			szText = m_pConfigText->toPlainText().toStdString();
		}
		else
		{
			std::ifstream in( file, std::ios::binary );
			szText.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
		}
		const std::string szName = xv::vfs::ToUtf8( file.filename() ) + ( m_bConfigModified && file == m_ConfigPath ? " (unsaved)" : "" );
		m_Parsed.push_back( xv::bsp::ParseStripper( szText, szName ) );
		if ( file == m_ConfigPath )
		{
			// The problems of the config in the editor, marked on their lines.
			const xv::bsp::StripperConfig_t &parsed = m_Parsed.back();
			std::vector<CodeDiagnostic_t> diagnostics;
			for ( std::size_t i = 0; i < parsed.m_Problems.size() && i < parsed.m_ProblemLines.size(); i++ )
			{
				std::string_view szProblem = parsed.m_Problems[ i ];
				const std::string szWhere = szName + ":" + std::to_string( parsed.m_ProblemLines[ i ] ) + ": ";
				if ( szProblem.starts_with( szWhere ) )
				{
					szProblem.remove_prefix( szWhere.size() );
				}
				diagnostics.push_back( { parsed.m_ProblemLines[ i ], Qs( szProblem ) } );
			}
			m_pConfigText->SetDiagnostics( std::move( diagnostics ) );
			bDiagnosed = true;
		}
		names.push_back( QString::fromStdString( szName ) );
		const QString szPath = QString::fromStdU16String( file.u16string() );
		if ( QFileInfo::exists( szPath ) && !m_pWatcher->files().contains( szPath ) )
		{
			m_pWatcher->addPath( szPath );
		}
	}
	if ( !bDiagnosed )
	{
		m_pConfigText->SetDiagnostics( {} );
	}
	m_pConfigs->setText( names.isEmpty() ? tr( "no Stripper config for this map (addons/stripper/global_filters.cfg, addons/stripper/maps/<map>.cfg)" ) : names.join( QStringLiteral( ", " ) ) );
	m_pConfigs->setToolTip( [ this ] {
		QStringList paths;
		for ( const auto &file : m_Configs )
		{
			paths.push_back( QString::fromStdU16String( file.u16string() ) );
		}
		return paths.join( u'\n' );
	}() );
	m_pStripper->setEnabled( !m_Configs.empty() );
	Rebuild();
}

//-----------------------------------------------------------------------------
// Purpose: The list from the map's entities, or what the configs make of them.
//-----------------------------------------------------------------------------
void CEntityView::Rebuild()
{
	using Status_t = xv::bsp::StrippedEntity_t::Status_t;
	const bool bStripper = m_pStripper->isChecked() && !m_Parsed.empty();
	const auto &entities = m_Bsp.GetEntities();
	if ( bStripper )
	{
		m_Shown = xv::bsp::ApplyStripper( entities, m_Parsed );
	}
	else
	{
		m_Shown.clear();
		m_Shown.reserve( entities.size() );
		for ( std::size_t i = 0; i < entities.size(); i++ )
		{
			m_Shown.push_back( { entities[ i ], Status_t::STATUS_KEPT, i, {}, {}, {} } );
		}
	}

	const std::optional<qlonglong> current = m_pList->currentItem() ? std::optional( m_pList->currentItem()->data( ENTITY_COLUMN_INDEX, Qt::UserRole ).toLongLong() ) : std::nullopt;
	m_pList->setSortingEnabled( false );
	m_pList->clear();
	QList<QTreeWidgetItem *> items;
	items.reserve( static_cast<qsizetype>( m_Shown.size() ) );
	int nRemoved = 0;
	int nAdded = 0;
	int nModified = 0;
	for ( std::size_t i = 0; i < m_Shown.size(); i++ )
	{
		const auto &shown = m_Shown[ i ];
		auto *pItem = new CNumberItem;
		// Added entities sort after the map's own, in the order they were added.
		const qlonglong nOrder = shown.m_Original ? static_cast<qlonglong>( *shown.m_Original ) : static_cast<qlonglong>( entities.size() + i );
		pItem->setText( ENTITY_COLUMN_INDEX, shown.m_Original ? QString::number( *shown.m_Original ) : QStringLiteral( "+" ) );
		pItem->setData( ENTITY_COLUMN_INDEX, Qt::UserRole, nOrder );
		pItem->setData( ENTITY_COLUMN_INDEX, ROLE_SHOWN_INDEX, static_cast<int>( i ) );
		pItem->setText( ENTITY_COLUMN_CLASS, Qs( shown.m_Entity.Get( "classname" ) ) );
		pItem->setText( ENTITY_COLUMN_NAME, Qs( shown.m_Entity.Get( "targetname" ) ) );
		pItem->setText( ENTITY_COLUMN_ORIGIN, Qs( shown.m_Entity.Get( "origin" ) ) );
		QString szStatus;
		QColor colour;
		switch ( shown.m_nStatus )
		{
			case Status_t::STATUS_REMOVED:
				szStatus = tr( "Removed" );
				colour = REMOVED_COLOUR;
				nRemoved++;
				break;
			case Status_t::STATUS_ADDED:
				szStatus = tr( "Added" );
				colour = ADDED_COLOUR;
				nAdded++;
				break;
			case Status_t::STATUS_MODIFIED:
				szStatus = tr( "Modified" );
				colour = MODIFIED_COLOUR;
				nModified++;
				break;
			case Status_t::STATUS_KEPT:
				break;
		}
		if ( colour.isValid() )
		{
			QFont font = pItem->font( 0 );
			font.setStrikeOut( shown.m_nStatus == Status_t::STATUS_REMOVED );
			for ( int nColumn = 0; nColumn < ENTITY_COLUMN_COUNT; nColumn++ )
			{
				pItem->setForeground( nColumn, colour );
				pItem->setFont( nColumn, font );
				pItem->setToolTip( nColumn, tr( "%1 by %2" ).arg( szStatus, QString::fromStdString( [ &shown ] {
					std::string szRules;
					for ( const auto &szRule : shown.m_Rules )
					{
						szRules += ( szRules.empty() ? "" : ", " ) + szRule;
					}
					return szRules;
				}() ) ) );
			}
		}
		items.push_back( pItem );
	}
	m_pList->addTopLevelItems( items );
	m_pList->setSortingEnabled( true );
	m_pList->sortByColumn( m_pList->header()->sortIndicatorSection(), m_pList->header()->sortIndicatorOrder() );

	m_pProblems->clear();
	QList<QTreeWidgetItem *> problems;
	for ( std::size_t nConfig = 0; nConfig < m_Parsed.size() && nConfig < m_Configs.size(); nConfig++ )
	{
		const xv::bsp::StripperConfig_t &config = m_Parsed[ nConfig ];
		const QString szPath = QString::fromStdU16String( m_Configs[ nConfig ].u16string() );
		for ( std::size_t i = 0; i < config.m_Problems.size(); i++ )
		{
			const int nLine = i < config.m_ProblemLines.size() ? config.m_ProblemLines[ i ] : 0;
			// The parser puts "name:line: " in front, which the columns already say.
			QString szProblem = QString::fromStdString( config.m_Problems[ i ] );
			const QString szWhere = QStringLiteral( ":%1: " ).arg( nLine );
			if ( const qsizetype nAt = szProblem.indexOf( szWhere ); nLine > 0 && nAt >= 0 )
			{
				szProblem.remove( 0, nAt + szWhere.size() );
			}
			auto *pItem = new QTreeWidgetItem( { nLine > 0 ? QString::number( nLine ) : QString{}, szProblem, QFileInfo( szPath ).fileName() } );
			pItem->setData( 0, Qt::UserRole, szPath );
			pItem->setData( 0, Qt::UserRole + 1, nLine );
			pItem->setToolTip( 1, szProblem );
			pItem->setToolTip( 2, szPath );
			pItem->setIcon( 0, style()->standardIcon( QStyle::SP_MessageBoxWarning ) );
			problems.push_back( pItem );
		}
	}
	m_pProblems->addTopLevelItems( problems );
	QStringList summary;
	if ( bStripper )
	{
		summary << tr( "Stripper: %1 removed, %2 added, %3 modified" ).arg( nRemoved ).arg( nAdded ).arg( nModified );
	}
	summary << ( problems.isEmpty() ? tr( "No problems in the configs" ) : tr( "%n problem(s)", nullptr, static_cast<int>( problems.size() ) ) );
	m_pSummary->setText( summary.join( QStringLiteral( " · " ) ) );
	m_pProblemsPanel->setVisible( !m_Configs.empty() );
	SetSearch( m_pSearch->text() );
	if ( current )
	{
		SelectEntity( static_cast<int>( *current ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Puts the cursor on a problem's line when it's in the config being edited.
//-----------------------------------------------------------------------------
void CEntityView::GoToProblem( QTreeWidgetItem *pItem )
{
	const int nLine = pItem ? pItem->data( 0, Qt::UserRole + 1 ).toInt() : 0;
	if ( nLine <= 0 || m_ConfigPath.empty() || pItem->data( 0, Qt::UserRole ).toString() != QString::fromStdU16String( m_ConfigPath.u16string() ) )
	{
		return;
	}
	ShowConfigEditor( true );
	const QTextBlock block = m_pConfigText->document()->findBlockByNumber( nLine - 1 );
	if ( block.isValid() )
	{
		QTextCursor cursor( block );
		cursor.movePosition( QTextCursor::EndOfBlock );
		m_pConfigText->setTextCursor( cursor );
		m_pConfigText->centerCursor();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::ShowConfigEditor( bool bShown )
{
	if ( m_ConfigPath.empty() )
	{
		return;
	}
	m_pEdit->setChecked( bShown );
	m_pEditor->setVisible( bShown );
	if ( auto *pSplitter = qobject_cast<QSplitter *>( m_pEditor->parentWidget() ); pSplitter && bShown && pSplitter->sizes().value( 2 ) == 0 )
	{
		pSplitter->setSizes( { 420, 340, 440 } );
	}
	if ( bShown )
	{
		m_pConfigText->setFocus();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CEntityView::GetConfigText() const
{
	return m_pConfigText->toPlainText();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::SetConfigText( const QString &szText )
{
	m_pConfigText->setPlainText( szText );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::EditConfig( const std::filesystem::path &config )
{
	m_ConfigPath = config;
	SetConfigModified( false );
	LoadConfigText();
	std::vector<std::filesystem::path> configs = m_Configs;
	if ( std::ranges::find( configs, config ) == configs.end() )
	{
		configs = { config };
	}
	m_pEdit->setEnabled( true );
	m_pEdit->setToolTip( tr( "Edit %1, seeing the changes in the list as you type" ).arg( QString::fromStdU16String( config.u16string() ) ) );
	SetStripperConfigs( std::move( configs ) );
	m_pStripper->setChecked( true );
	ShowConfigEditor( true );
}

//-----------------------------------------------------------------------------
// Purpose: A blank line between the rules already there and the new ones.
//-----------------------------------------------------------------------------
void CEntityView::AppendToConfig( const std::string &szText )
{
	ShowConfigEditor( true );
	QString szCurrent = m_pConfigText->toPlainText();
	if ( !szCurrent.isEmpty() && !szCurrent.endsWith( u'\n' ) )
	{
		szCurrent += u'\n';
	}
	if ( !szCurrent.isEmpty() )
	{
		szCurrent += u'\n';
	}
	m_pConfigText->setPlainText( szCurrent + QString::fromStdString( szText ) );
	m_pConfigText->moveCursor( QTextCursor::End );
	m_pConfigText->ensureCursorVisible();
	m_pReparse->stop();
	SetStripperConfigs( m_Configs );
}

//-----------------------------------------------------------------------------
// Purpose: The file's text into the editor, without counting as a change.
//-----------------------------------------------------------------------------
void CEntityView::LoadConfigText()
{
	QString szText = m_bConfigReadOnly ? m_szConfigData : QString{};
	QFile file( QString::fromStdU16String( m_ConfigPath.u16string() ) );
	if ( !m_bConfigReadOnly && !m_ConfigPath.empty() && file.open( QIODevice::ReadOnly ) )
	{
		szText = QString::fromUtf8( file.readAll() );
	}
	m_bLoadingText = true;
	m_pConfigText->setPlainText( szText );
	m_bLoadingText = false;
	const QString szState = m_bConfigReadOnly ? tr( " (archive; read-only)" ) : file.exists() ? QString{}
																							  : tr( " (new; made when saved)" );
	m_pConfigPath->setText( m_ConfigPath.empty() ? QString{} : QDir::toNativeSeparators( QString::fromStdU16String( m_ConfigPath.u16string() ) ) + szState );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::SetConfigModified( bool bModified )
{
	if ( m_bConfigModified != bModified )
	{
		m_bConfigModified = bModified;
		emit ConfigModifiedChanged( bModified );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Its folders are made when this is the config's first save.
//-----------------------------------------------------------------------------
bool CEntityView::SaveConfig()
{
	if ( m_bConfigReadOnly )
		return SaveConfigAs();
	if ( m_ConfigPath.empty() )
	{
		return false;
	}
	const QString szPath = QString::fromStdU16String( m_ConfigPath.u16string() );
	QDir{}.mkpath( QFileInfo{ szPath }.absolutePath() );
	QSaveFile file( szPath );
	const QByteArray bytes = m_pConfigText->toPlainText().toUtf8();
	if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() || !file.commit() )
	{
		QMessageBox::critical( this, tr( "Stripper Config" ), tr( "Could not write %1:\n%2" ).arg( QDir::toNativeSeparators( szPath ), file.errorString() ) );
		return false;
	}
	SetConfigModified( false );
	m_pConfigPath->setText( QDir::toNativeSeparators( szPath ) );
	// A new file joins the configs as Stripper would find it.
	auto configs = m_Configs;
	if ( std::ranges::find( configs, m_ConfigPath ) == configs.end() )
	{
		configs.push_back( m_ConfigPath );
	}
	SetStripperConfigs( std::move( configs ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Rules for the entity under the mouse, written into the config being edited.
//-----------------------------------------------------------------------------
void CEntityView::ShowListMenu( const QPoint &at )
{
	QTreeWidgetItem *pItem = m_pList->itemAt( at );
	if ( !pItem )
	{
		return;
	}
	const auto &shown = m_Shown[ static_cast<std::size_t>( pItem->data( ENTITY_COLUMN_INDEX, ROLE_SHOWN_INDEX ).toInt() ) ];
	const xv::bsp::Entity_t entity = shown.m_Entity;
	QMenu menu( this );
	const bool bCanWrite = !m_bConfigReadOnly && !m_ConfigPath.empty() && shown.m_nStatus != xv::bsp::StrippedEntity_t::Status_t::STATUS_REMOVED;
	QAction *pRemove = menu.addAction( tr( "Remove With Stripper" ), this, [ this, entity ] { AppendToConfig( xv::bsp::StripperFilterFor( entity ) ); } );
	QAction *pChange = menu.addAction( tr( "Change With Stripper" ), this, [ this, entity ] { AppendToConfig( xv::bsp::StripperModifyFor( entity ) ); } );
	QAction *pCopy = menu.addAction( tr( "Add a Copy With Stripper" ), this, [ this, entity ] { AppendToConfig( xv::bsp::StripperAddFor( entity ) ); } );
	for ( QAction *pAction : { pRemove, pChange, pCopy } )
	{
		pAction->setEnabled( bCanWrite );
	}
	menu.addSeparator();
	menu.addAction( tr( "Copy Keyvalues" ), this, [ entity ] {
		QString szText;
		for ( const auto &[ key, value ] : entity.m_KeyValues )
		{
			szText += QStringLiteral( "\"%1\" \"%2\"\n" ).arg( Qs( key ), Qs( value ) );
		}
		QGuiApplication::clipboard()->setText( szText );
	} );
	menu.exec( m_pList->viewport()->mapToGlobal( at ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::SetSearch( const QString &szSearch )
{
	if ( m_pSearch->text() != szSearch )
	{
		m_pSearch->setText( szSearch );
		return; // textChanged comes back here
	}
	const QString szText = szSearch.trimmed();
	const auto contains = [ &szText ]( std::string_view sz ) { return Qs( sz ).contains( szText, Qt::CaseInsensitive ); };
	for ( int i = 0; i < m_pList->topLevelItemCount(); i++ )
	{
		QTreeWidgetItem *pItem = m_pList->topLevelItem( i );
		const auto &entity = m_Shown[ static_cast<std::size_t>( pItem->data( ENTITY_COLUMN_INDEX, ROLE_SHOWN_INDEX ).toInt() ) ].m_Entity;
		const bool bShow = szText.isEmpty() || std::ranges::any_of( entity.m_KeyValues, [ & ]( const auto &pair ) { return contains( pair.first ) || contains( pair.second ); } );
		pItem->setHidden( !bShow );
	}
	// The keyvalues shown always belong to an entity in the list.
	if ( m_pList->currentItem() && m_pList->currentItem()->isHidden() )
	{
		m_pList->setCurrentItem( nullptr );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CEntityView::GetShownCount() const
{
	int nShown = 0;
	for ( int i = 0; i < m_pList->topLevelItemCount(); i++ )
	{
		nShown += !m_pList->topLevelItem( i )->isHidden();
	}
	return nShown;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::SetStripperShown( bool bShown )
{
	m_pStripper->setChecked( bShown );
}

//-----------------------------------------------------------------------------
// Purpose: By the entity's index in the map (an added one's: the map's count plus its place).
//-----------------------------------------------------------------------------
void CEntityView::SelectEntity( int nIndex )
{
	for ( int i = 0; i < m_pList->topLevelItemCount(); i++ )
	{
		if ( m_pList->topLevelItem( i )->data( ENTITY_COLUMN_INDEX, Qt::UserRole ).toInt() == nIndex )
		{
			m_pList->setCurrentItem( m_pList->topLevelItem( i ) );
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: An entity's pairs, with what Stripper replaced or inserted marked and what it deleted
//          left at the end, struck out.
//-----------------------------------------------------------------------------
void CEntityView::ShowKeyValues( int nShown )
{
	if ( nShown < 0 || nShown >= static_cast<int>( m_Shown.size() ) )
	{
		m_pKeyValues->setRowCount( 0 );
		return;
	}
	const auto &shown = m_Shown[ static_cast<std::size_t>( nShown ) ];
	const auto &keyValues = shown.m_Entity.m_KeyValues;
	m_pKeyValues->setRowCount( static_cast<int>( keyValues.size() + shown.m_Deleted.size() ) );
	const auto setRow = [ this ]( int nRow, const std::string &szKey, const std::string &szValue, const QColor &colour, bool bStruck ) {
		// Outputs separate their parts with an escape character in newer games; shown as commas.
		QString szShown = Qs( szValue );
		szShown.replace( QChar( 0x1b ), u',' );
		for ( int nColumn = 0; nColumn < 2; nColumn++ )
		{
			auto *pItem = new QTableWidgetItem( nColumn == 0 ? Qs( szKey ) : szShown );
			if ( colour.isValid() )
			{
				QFont font = pItem->font();
				font.setStrikeOut( bStruck );
				font.setBold( !bStruck );
				pItem->setFont( font );
				pItem->setForeground( colour );
			}
			m_pKeyValues->setItem( nRow, nColumn, pItem );
		}
	};
	int nRow = 0;
	for ( const auto &[ key, value ] : keyValues )
	{
		const bool bChanged = std::ranges::any_of( shown.m_ChangedKeys, [ &key ]( const std::string &sz ) { return QString::fromStdString( sz ).compare( Qs( key ), Qt::CaseInsensitive ) == 0; } );
		setRow( nRow++, key, value, bChanged ? MODIFIED_COLOUR : QColor{}, false );
	}
	for ( const auto &[ key, value ] : shown.m_Deleted )
	{
		setRow( nRow++, key, value, REMOVED_COLOUR, true );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CLumpView::CLumpView( const xv::bsp::CBsp &bsp, QWidget *pParent )
	: QWidget( pParent )
{
	auto *pTree = new QTreeWidget;
	pTree->setColumnCount( LUMP_COLUMN_COUNT );
	pTree->setHeaderLabels( { tr( "#" ), tr( "Lump" ), tr( "Size" ), tr( "Offset" ), tr( "Version" ), tr( "Compressed" ) } );
	pTree->setRootIsDecorated( false );
	pTree->setUniformRowHeights( true );
	pTree->setSortingEnabled( true );
	int nUsed = 0;
	for ( const auto &lump : bsp.GetLumps() )
	{
		auto *pItem = new CNumberItem( pTree );
		pItem->setText( LUMP_COLUMN_INDEX, QString::number( lump.m_nIndex ) );
		pItem->setData( LUMP_COLUMN_INDEX, Qt::UserRole, lump.m_nIndex );
		pItem->setText( LUMP_COLUMN_NAME, QString::fromStdString( xv::bsp::LumpName( lump.m_nIndex ) ) );
		pItem->setText( LUMP_COLUMN_SIZE, QLocale{}.formattedDataSize( lump.m_nLength ) );
		pItem->setData( LUMP_COLUMN_SIZE, Qt::UserRole, static_cast<qlonglong>( lump.m_nLength ) );
		pItem->setText( LUMP_COLUMN_OFFSET, lump.m_nLength ? QString::number( lump.m_nOffset ) : QString{} );
		pItem->setData( LUMP_COLUMN_OFFSET, Qt::UserRole, static_cast<qlonglong>( lump.m_nOffset ) );
		pItem->setText( LUMP_COLUMN_VERSION, QString::number( lump.m_nVersion ) );
		pItem->setData( LUMP_COLUMN_VERSION, Qt::UserRole, static_cast<qlonglong>( lump.m_nVersion ) );
		pItem->setText( LUMP_COLUMN_COMPRESSED, lump.m_nUncompressed ? tr( "yes, %1 unpacked" ).arg( QLocale{}.formattedDataSize( lump.m_nUncompressed ) ) : QString{} );
		for ( int nColumn : { LUMP_COLUMN_INDEX, LUMP_COLUMN_SIZE, LUMP_COLUMN_OFFSET, LUMP_COLUMN_VERSION } )
		{
			pItem->setTextAlignment( nColumn, Qt::AlignRight | Qt::AlignVCenter );
		}
		if ( lump.m_nLength == 0 )
		{
			// Empty lumps stay listed (their slot exists) but greyed.
			for ( int nColumn = 0; nColumn < LUMP_COLUMN_COUNT; nColumn++ )
			{
				pItem->setForeground( nColumn, pTree->palette().brush( QPalette::Disabled, QPalette::Text ) );
			}
		}
		nUsed += lump.m_nLength != 0;
	}
	pTree->sortByColumn( LUMP_COLUMN_INDEX, Qt::AscendingOrder );
	pTree->setColumnWidth( LUMP_COLUMN_INDEX, 50 );
	pTree->setColumnWidth( LUMP_COLUMN_NAME, 260 );
	pTree->setColumnWidth( LUMP_COLUMN_SIZE, 100 );
	pTree->setColumnWidth( LUMP_COLUMN_OFFSET, 100 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 6, 6, 6, 0 );
	pLayout->addWidget( new QLabel( tr( "BSP version %1, map revision %2. %3 of %4 lumps hold data." ).arg( bsp.GetVersion() ).arg( bsp.GetRevision() ).arg( nUsed ).arg( xv::bsp::LUMP_COUNT ) ) );
	pLayout->addWidget( pTree, 1 );
}

//-----------------------------------------------------------------------------
// Purpose: An archived config is previewed from its bytes and cannot overwrite a loose file.
//-----------------------------------------------------------------------------
void CEntityView::ReadConfigData( const std::filesystem::path &config, const QString &szText )
{
	m_bConfigReadOnly = true;
	m_szConfigData = szText;
	EditConfig( config );
	m_pConfigText->setReadOnly( true );
	findChild<QPushButton *>( QStringLiteral( "entityStripperChoose" ) )->setEnabled( false );
	findChild<QPushButton *>( QStringLiteral( "entityStripperSave" ) )->setText( tr( "Save As…" ) );
}

//-----------------------------------------------------------------------------
// Purpose: Export a config to a loose file, where Stripper can read it.
//-----------------------------------------------------------------------------
bool CEntityView::SaveConfigAs()
{
	const QString szPath = AskSaveFile( this, tr( "Save Stripper Config" ), QString::fromStdU16String( m_ConfigPath.filename().u16string() ), tr( "Stripper configs (*.cfg);;All files (*)" ) );
	if ( szPath.isEmpty() )
		return false;
	QSaveFile file( szPath );
	const QByteArray bytes = GetConfigText().toUtf8();
	if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() || !file.commit() )
	{
		QMessageBox::critical( this, tr( "Stripper Config" ), tr( "Could not write %1:\n%2" ).arg( szPath, file.errorString() ) );
		return false;
	}
	return true;
}
