//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MaterialForm.h.
//
//===========================================================================//
#include "MaterialForm.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QColorDialog>
#include <QCoreApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "InterfaceMode.h"
#include "Language.h"
#include "material/Parameters.h"
#include "util/Numbers.h"

namespace kv1 = xv::kv1;
using xv::material::ParameterKind_t;

namespace
{

// What a row holds, beyond its text.
constexpr int ROLE_PATH = Qt::UserRole + 1;  // indices from the file's first block down
constexpr int ROLE_KIND = Qt::UserRole + 2;  // a ParameterKind_t, or KIND_SHADER / KIND_BLOCK
constexpr int ROLE_KEY = Qt::UserRole + 3;   // the key as written
constexpr int ROLE_VALUE = Qt::UserRole + 4; // the value as written
constexpr int ROLE_CHAIN = Qt::UserRole + 5; // the keys down to it, lowercase: "proxies/sine"
constexpr int KIND_SHADER = -1;
constexpr int KIND_BLOCK = -2;

// Proxies offered from the menu, with the settings each starts with.
struct ProxyStart_t
{
	const char *m_pszName;
	std::vector<std::pair<const char *, const char *>> m_Settings;
};
const ProxyStart_t PROXIES[] = {
	{ "AnimatedTexture", { { "animatedtexturevar", "$basetexture" }, { "animatedtextureframenumvar", "$frame" }, { "animatedtextureframerate", "10" } } },
	{ "TextureScroll", { { "texturescrollvar", "$basetexturetransform" }, { "texturescrollrate", "0.25" }, { "texturescrollangle", "90" } } },
	{ "Sine", { { "sinemin", "0" }, { "sinemax", "1" }, { "sineperiod", "2" }, { "resultvar", "$alpha" } } },
	{ "LinearRamp", { { "rate", "1" }, { "initialvalue", "0" }, { "resultvar", "$frame" } } },
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<int> ToPath( const QVariant &value )
{
	std::vector<int> path;
	for ( const QVariant &index : value.toList() )
	{
		path.push_back( index.toInt() );
	}
	return path;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QVariantList ToVariant( const std::vector<int> &path )
{
	QVariantList list;
	for ( const int nIndex : path )
	{
		list << nIndex;
	}
	return list;
}

//-----------------------------------------------------------------------------
// Purpose: A path as text, to remember which blocks are folded.
//-----------------------------------------------------------------------------
QString PathKey( const std::vector<int> &path )
{
	QStringList parts;
	for ( const int nIndex : path )
	{
		parts << QString::number( nIndex );
	}
	return parts.join( u'/' );
}

//-----------------------------------------------------------------------------
// Purpose: The node a path leads to, or null.
//-----------------------------------------------------------------------------
kv1::CNode *NodeAt( kv1::CDocument &document, const std::vector<int> &path )
{
	if ( path.empty() || path.front() < 0 || static_cast<std::size_t>( path.front() ) >= document.m_Roots.size() )
	{
		return nullptr;
	}
	kv1::CNode *pNode = &document.m_Roots[ static_cast<std::size_t>( path.front() ) ];
	for ( std::size_t i = 1; i < path.size(); i++ )
	{
		if ( path[ i ] < 0 || static_cast<std::size_t>( path[ i ] ) >= pNode->GetChildren().size() )
		{
			return nullptr;
		}
		pNode = &pNode->Child( static_cast<std::size_t>( path[ i ] ) );
	}
	return pNode;
}

//-----------------------------------------------------------------------------
// Purpose: The index of a block's child with a key, or -1.
//-----------------------------------------------------------------------------
int ChildIndex( const kv1::CNode &block, const QString &szKey )
{
	const std::string szWanted = szKey.toStdString();
	for ( std::size_t i = 0; i < block.GetChildren().size(); i++ )
	{
		if ( kv1::EqualsIgnoreCase( block.Child( i ).GetKey(), szWanted ) )
		{
			return static_cast<int>( i );
		}
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: A colour value as a colour: "[r g b]" from 0 to 1 (linear in the file, shown as is),
//          "{r g b}" from 0 to 255, or one number for all three.
//-----------------------------------------------------------------------------
std::optional<QColor> ColourOf( const QString &szValue )
{
	const bool bBytes = szValue.contains( u'{' );
	QString szNumbers = szValue;
	szNumbers.remove( u'[' ).remove( u']' ).remove( u'{' ).remove( u'}' );
	std::vector<float> numbers;
	for ( const QString &szPart : szNumbers.split( u' ', Qt::SkipEmptyParts ) )
	{
		const auto number = xv::ParseNumber<float>( szPart.toStdString() );
		if ( !number )
		{
			return std::nullopt;
		}
		numbers.push_back( bBytes ? *number / 255.0f : *number );
	}
	if ( numbers.size() == 1 )
	{
		numbers.resize( 3, numbers.front() );
	}
	if ( numbers.size() != 3 )
	{
		return std::nullopt;
	}
	return QColor::fromRgbF( std::clamp( numbers[ 0 ], 0.0f, 1.0f ), std::clamp( numbers[ 1 ], 0.0f, 1.0f ), std::clamp( numbers[ 2 ], 0.0f, 1.0f ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QIcon Swatch( const QColor &colour )
{
	QPixmap pixmap( 16, 16 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	painter.setPen( QColor( 0, 0, 0, 120 ) );
	painter.setBrush( colour );
	painter.drawRect( 0, 0, 15, 15 );
	return QIcon( pixmap );
}

// The list, taking undo and redo for the text and Delete for removing a row.
class CFormTree : public QTreeWidget
{
public:
	explicit CFormTree( CMaterialForm *pForm )
		: m_pForm( pForm )
	{}

protected:
	bool event( QEvent *pEvent ) override
	{
		// Before the window's Undo takes the keys (that one undoes the texture).
		if ( pEvent->type() == QEvent::ShortcutOverride )
		{
			auto *pKey = static_cast<QKeyEvent *>( pEvent );
			if ( pKey->matches( QKeySequence::Undo ) || pKey->matches( QKeySequence::Redo ) )
			{
				pEvent->accept();
				return true;
			}
		}
		return QTreeWidget::event( pEvent );
	}
	void keyPressEvent( QKeyEvent *pEvent ) override
	{
		if ( pEvent->matches( QKeySequence::Undo ) )
		{
			emit m_pForm->UndoRequested();
			return;
		}
		if ( pEvent->matches( QKeySequence::Redo ) )
		{
			emit m_pForm->RedoRequested();
			return;
		}
		if ( pEvent->key() == Qt::Key_Delete && state() != EditingState && currentItem() )
		{
			const int nKind = currentItem()->data( 0, ROLE_KIND ).toInt();
			if ( nKind != KIND_SHADER )
			{
				const std::vector<int> path = ToPath( currentItem()->data( 0, ROLE_PATH ) );
				QTimer::singleShot( 0, m_pForm, [ pForm = m_pForm, path ] {
					if ( QTreeWidgetItem *pItem = pForm->GetTree()->currentItem(); pItem && ToPath( pItem->data( 0, ROLE_PATH ) ) == path )
					{
						pForm->RemoveParameter( pItem->data( 0, ROLE_KEY ).toString() );
					}
				} );
			}
			return;
		}
		QTreeWidget::keyPressEvent( pEvent );
	}

private:
	CMaterialForm *m_pForm;
};

// The box for each value: what suits its kind.
class CValueDelegate : public QStyledItemDelegate
{
public:
	explicit CValueDelegate( CMaterialForm *pForm )
		: QStyledItemDelegate( pForm ),
		  m_pForm( pForm )
	{}

	QWidget *createEditor( QWidget *pParent, const QStyleOptionViewItem &, const QModelIndex &index ) const override
	{
		const int nKind = index.data( ROLE_KIND ).toInt();
		if ( index.column() == 0 )
		{
			return nKind == KIND_SHADER ? nullptr : new QLineEdit( pParent );
		}
		if ( nKind == KIND_BLOCK || nKind == static_cast<int>( ParameterKind_t::PARAMETER_KIND_BOOL ) )
		{
			return nullptr;
		}
		const QString szValue = index.data( ROLE_VALUE ).toString();
		const xv::particles::CNames &names = ParticleNames();
		const auto searchBox = [ this, pParent ]( const std::vector<Choice_t> &choices ) {
			QComboBox *pBox = MakeSearchBox( choices, pParent );
			// Picking from the list is the edit.
			auto *pSelf = const_cast<CValueDelegate *>( this );
			connect( pBox->completer(), qOverload<const QString &>( &QCompleter::activated ), pBox, [ pSelf, pBox ]( const QString &szText ) {
				pBox->setEditText( szText );
				QTimer::singleShot( 0, pBox, [ pSelf, pBox ] {
					if ( pBox->isVisible() )
					{
						emit pSelf->commitData( pBox );
						emit pSelf->closeEditor( pBox );
					}
				} );
			} );
			return pBox;
		};
		if ( nKind == KIND_SHADER )
		{
			std::vector<Choice_t> shaders;
			for ( const std::string &szShader : MaterialParameters().GetShaders() )
			{
				shaders.push_back( { Qs( szShader ), Qs( names.Shader( szShader ).m_szMeaning ) } );
			}
			return searchBox( shaders );
		}
		switch ( static_cast<ParameterKind_t>( nKind ) )
		{
			case ParameterKind_t::PARAMETER_KIND_SURFACE:
				return searchBox( m_pForm->GetSurfaceProps() );
			case ParameterKind_t::PARAMETER_KIND_CHOICE:
			{
				std::vector<Choice_t> choices;
				for ( const xv::particles::NamedValue_t &choice : names.Choices( index.data( ROLE_KEY ).toString().toStdString() ) )
				{
					choices.push_back( { Qs( choice.m_szValue ), Qs( choice.m_szName ), Qs( choice.m_szValue ) } );
				}
				if ( choices.empty() )
				{
					break;
				}
				auto *pBox = new QComboBox( pParent );
				FillListBox( pBox, choices );
				auto *pSelf = const_cast<CValueDelegate *>( this );
				connect( pBox, &QComboBox::activated, pBox, [ pSelf, pBox ] {
					QTimer::singleShot( 0, pBox, [ pSelf, pBox ] {
						if ( pBox->isVisible() && !pBox->view()->isVisible() )
						{
							emit pSelf->commitData( pBox );
							emit pSelf->closeEditor( pBox );
						}
					} );
				} );
				return pBox;
			}
			case ParameterKind_t::PARAMETER_KIND_NUMBER:
				if ( xv::ParseNumber<double>( szValue.toStdString() ) )
				{
					auto *pSpin = new QDoubleSpinBox( pParent );
					pSpin->setLocale( QLocale::c() );
					pSpin->setRange( -1e6, 1e6 );
					pSpin->setDecimals( 4 );
					pSpin->setSingleStep( 0.05 );
					return pSpin;
				}
				break;
			case ParameterKind_t::PARAMETER_KIND_INTEGER:
				if ( xv::ParseNumber<int>( szValue.toStdString() ) )
				{
					auto *pSpin = new QSpinBox( pParent );
					pSpin->setRange( -1000000, 1000000 );
					return pSpin;
				}
				break;
			case ParameterKind_t::PARAMETER_KIND_TEXTURE:
			{
				auto *pEdit = new QLineEdit( pParent );
				auto *pCompleter = new QCompleter( m_pForm->GetTextureNames(), pEdit );
				pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
				pCompleter->setFilterMode( Qt::MatchContains );
				pCompleter->setMaxVisibleItems( 14 );
				pEdit->setCompleter( pCompleter );
				return pEdit;
			}
			default: break;
		}
		return new QLineEdit( pParent );
	}

	void setEditorData( QWidget *pEditor, const QModelIndex &index ) const override
	{
		const QString szText = index.data( index.column() == 0 ? ROLE_KEY : ROLE_VALUE ).toString();
		if ( auto *pBox = qobject_cast<QComboBox *>( pEditor ) )
		{
			if ( pBox->isEditable() )
			{
				pBox->setCurrentIndex( -1 );
				pBox->setEditText( szText );
				QTimer::singleShot( 0, pBox, [ pBox ] { RevealChoices( pBox ); } );
			}
			else
			{
				pBox->setCurrentIndex( std::max( 0, pBox->findData( szText ) ) );
				QTimer::singleShot( 0, pBox, [ pBox ] {
					if ( pBox->isVisible() )
					{
						pBox->showPopup();
					}
				} );
			}
		}
		else if ( auto *pSpin = qobject_cast<QDoubleSpinBox *>( pEditor ) )
		{
			pSpin->setValue( xv::ParseNumber<double>( szText.toStdString() ).value_or( 0.0 ) );
		}
		else if ( auto *pWhole = qobject_cast<QSpinBox *>( pEditor ) )
		{
			pWhole->setValue( xv::ParseNumber<int>( szText.toStdString() ).value_or( 0 ) );
		}
		else if ( auto *pEdit = qobject_cast<QLineEdit *>( pEditor ) )
		{
			pEdit->setText( szText );
			pEdit->selectAll();
		}
	}

	void setModelData( QWidget *pEditor, QAbstractItemModel *, const QModelIndex &index ) const override
	{
		QString szText;
		if ( auto *pBox = qobject_cast<QComboBox *>( pEditor ) )
		{
			szText = pBox->isEditable() ? pBox->currentText() : pBox->currentData().toString();
		}
		else if ( auto *pSpin = qobject_cast<QDoubleSpinBox *>( pEditor ) )
		{
			szText = QString::number( pSpin->value() );
		}
		else if ( auto *pWhole = qobject_cast<QSpinBox *>( pEditor ) )
		{
			szText = QString::number( pWhole->value() );
		}
		else if ( auto *pEdit = qobject_cast<QLineEdit *>( pEditor ) )
		{
			szText = pEdit->text();
		}
		m_pForm->CommitEdit( index, szText );
	}

private:
	CMaterialForm *m_pForm;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: existing: the keys the block already has, left out of the list.
//-----------------------------------------------------------------------------
CAddParameterDialog::CAddParameterDialog( const QString &szShader, const QStringList &existing, QWidget *pParent )
	: QDialog( pParent )
{
	setWindowTitle( tr( "Add Parameter" ) );
	const xv::particles::CNames &names = ParticleNames();
	const bool bSimple = CInterfaceMode::Instance().IsSimple();
	m_pSearch = new QLineEdit;
	m_pSearch->setObjectName( QStringLiteral( "parameterSearch" ) );
	m_pSearch->setPlaceholderText( tr( "Search, or type a new $parameter" ) );
	m_pSearch->setClearButtonEnabled( true );
	m_pList = new QTreeWidget;
	m_pList->setObjectName( QStringLiteral( "parameterList" ) );
	m_pList->setHeaderHidden( true );
	m_pList->setColumnCount( 2 );
	m_pList->setRootIsDecorated( false );
	m_pList->setUniformRowHeights( true );
	m_pList->header()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
	m_pList->header()->setStretchLastSection( true );

	QFont bold = font();
	bold.setBold( true );
	const auto group = [ this, &bold ]( const QString &szTitle ) {
		auto *pGroup = new QTreeWidgetItem( m_pList, { szTitle } );
		pGroup->setFont( 0, bold );
		pGroup->setFirstColumnSpanned( true );
		pGroup->setFlags( Qt::ItemIsEnabled );
		pGroup->setExpanded( true );
		return pGroup;
	};
	const xv::material::CParameterTable &table = MaterialParameters();
	QTreeWidgetItem *pFor = group( szShader.isEmpty() ? tr( "Parameters" ) : tr( "Used by %1" ).arg( szShader ) );
	QTreeWidgetItem *pOthers = group( tr( "Used by other shaders" ) );
	for ( const xv::material::ParameterDef_t &parameter : table.GetParameters() )
	{
		const QString szKey = Qs( parameter.m_szKey );
		if ( existing.contains( szKey, Qt::CaseInsensitive ) )
		{
			continue;
		}
		const xv::particles::PlainName_t plain = names.MaterialParameter( parameter.m_szKey );
		auto *pItem = new QTreeWidgetItem( xv::material::CParameterTable::IsFor( parameter, szShader.toStdString() ) || szShader.isEmpty() ? pFor : pOthers,
			{ bSimple ? Qs( plain.m_szName ) : szKey, Qs( plain.m_szMeaning ) } );
		pItem->setData( 0, Qt::UserRole, szKey );
		pItem->setToolTip( 0, bSimple ? szKey : Qs( plain.m_szName ) );
		pItem->setToolTip( 1, Qs( plain.m_szMeaning ) );
		pItem->setForeground( 1, palette().color( QPalette::PlaceholderText ) );
	}
	pOthers->setHidden( pOthers->childCount() == 0 );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pButtons->button( QDialogButtonBox::Ok )->setText( tr( "Add" ) );
	connect( pButtons, &QDialogButtonBox::accepted, this, [ this ] {
		if ( !GetKey().isEmpty() )
		{
			accept();
		}
	} );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( m_pSearch );
	pLayout->addWidget( m_pList, 1 );
	pLayout->addWidget( pButtons );
	resize( 620, 520 );

	connect( m_pSearch, &QLineEdit::textChanged, this, &CAddParameterDialog::Filter );
	connect( m_pSearch, &QLineEdit::returnPressed, pButtons, &QDialogButtonBox::accepted );
	connect( m_pList, &QTreeWidget::itemActivated, this, [ this ]( QTreeWidgetItem *pItem ) {
		if ( !pItem->data( 0, Qt::UserRole ).toString().isEmpty() )
		{
			m_pList->setCurrentItem( pItem );
			accept();
		}
	} );
	m_pSearch->installEventFilter( this );
	Filter();
}

//-----------------------------------------------------------------------------
// Purpose: The arrows move through the list while typing.
//-----------------------------------------------------------------------------
bool CAddParameterDialog::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject == m_pSearch && pEvent->type() == QEvent::KeyPress )
	{
		const int nKey = static_cast<QKeyEvent *>( pEvent )->key();
		if ( nKey == Qt::Key_Up || nKey == Qt::Key_Down || nKey == Qt::Key_PageUp || nKey == Qt::Key_PageDown )
		{
			QCoreApplication::sendEvent( m_pList, pEvent );
			return true;
		}
	}
	return QDialog::eventFilter( pObject, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: Shows the parameters whose key, name or meaning has the search in it.
//-----------------------------------------------------------------------------
void CAddParameterDialog::Filter()
{
	const QString szSearch = m_pSearch->text().trimmed();
	QTreeWidgetItem *pFirst = nullptr;
	for ( int g = 0; g < m_pList->topLevelItemCount(); g++ )
	{
		QTreeWidgetItem *pGroup = m_pList->topLevelItem( g );
		int nShown = 0;
		for ( int i = 0; i < pGroup->childCount(); i++ )
		{
			QTreeWidgetItem *pItem = pGroup->child( i );
			const bool bShow = szSearch.isEmpty() || pItem->data( 0, Qt::UserRole ).toString().contains( szSearch, Qt::CaseInsensitive ) || pItem->text( 0 ).contains( szSearch, Qt::CaseInsensitive ) || pItem->text( 1 ).contains( szSearch, Qt::CaseInsensitive );
			pItem->setHidden( !bShow );
			nShown += bShow;
			if ( bShow && !pFirst )
			{
				pFirst = pItem;
			}
		}
		pGroup->setHidden( nShown == 0 );
	}
	m_pList->setCurrentItem( pFirst );
}

//-----------------------------------------------------------------------------
// Purpose: The key picked, or a $ or % one typed that isn't listed.
//-----------------------------------------------------------------------------
QString CAddParameterDialog::GetKey() const
{
	const QString szTyped = m_pSearch->text().trimmed();
	if ( const QTreeWidgetItem *pItem = m_pList->currentItem(); pItem && !pItem->isHidden() && !pItem->data( 0, Qt::UserRole ).toString().isEmpty() )
	{
		// A typed key exactly as one listed under another name still counts as typed.
		const QString szKey = pItem->data( 0, Qt::UserRole ).toString();
		if ( szTyped.startsWith( u'$' ) || szTyped.startsWith( u'%' ) )
		{
			return szTyped.compare( szKey, Qt::CaseInsensitive ) == 0 || !szKey.contains( szTyped, Qt::CaseInsensitive ) ? szTyped : szKey;
		}
		return szKey;
	}
	return szTyped.startsWith( u'$' ) || szTyped.startsWith( u'%' ) ? szTyped : QString{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialForm::CMaterialForm( QWidget *pParent )
	: QWidget( pParent )
{
	m_pTree = new CFormTree( this );
	m_pTree->setObjectName( QStringLiteral( "materialForm" ) );
	m_pTree->setColumnCount( 2 );
	m_pTree->setHeaderLabels( { tr( "Parameter" ), tr( "Value" ) } );
	m_pTree->setUniformRowHeights( true );
	m_pTree->setIconSize( { 16, 16 } );
	m_pTree->setItemDelegate( new CValueDelegate( this ) );
	m_pTree->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );
	m_pTree->setContextMenuPolicy( Qt::CustomContextMenu );
	m_pTree->setAllColumnsShowFocus( true );
	// Names take what they need, up to a point; the values get the rest.
	m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Interactive );
	m_pTree->header()->setStretchLastSection( true );
	m_pBlocked = new QLabel;
	m_pBlocked->setObjectName( QStringLiteral( "materialFormBlocked" ) );
	m_pBlocked->setWordWrap( true );
	m_pBlocked->hide();
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( m_pBlocked );
	pLayout->addWidget( m_pTree, 1 );

	connect( m_pTree, &QTreeWidget::customContextMenuRequested, this, &CMaterialForm::ShowMenu );
	// A click on a value edits it; a switch flips.
	connect( m_pTree, &QTreeWidget::itemClicked, this, [ this ]( QTreeWidgetItem *pItem, int nColumn ) {
		const int nKind = pItem->data( 0, ROLE_KIND ).toInt();
		if ( nColumn == 1 && nKind != KIND_BLOCK && nKind != static_cast<int>( ParameterKind_t::PARAMETER_KIND_BOOL ) )
		{
			m_pTree->editItem( pItem, 1 );
		}
	} );
	connect( m_pTree, &QTreeWidget::itemChanged, this, [ this ]( QTreeWidgetItem *pItem, int nColumn ) {
		// The tick on the left: in the preview or not.
		if ( !m_bFilling && nColumn == 0 && pItem->data( 0, ROLE_KIND ).toInt() != KIND_SHADER )
		{
			const QString szChain = pItem->data( 0, ROLE_CHAIN ).toString();
			const bool bShown = pItem->checkState( 0 ) == Qt::Checked;
			if ( bShown == m_Hidden.contains( szChain ) )
			{
				if ( bShown )
				{
					m_Hidden.remove( szChain );
				}
				else
				{
					m_Hidden.insert( szChain );
				}
				// After the click is done with the row: the preview's redraw refills the list.
				QTimer::singleShot( 0, this, [ this ] {
					emit PreviewTogglesChanged();
					Fill();
				} );
			}
			return;
		}
		if ( m_bFilling || nColumn != 1 || pItem->data( 0, ROLE_KIND ).toInt() != static_cast<int>( ParameterKind_t::PARAMETER_KIND_BOOL ) )
		{
			return;
		}
		const QString szValue = pItem->checkState( 1 ) == Qt::Checked ? QStringLiteral( "1" ) : QStringLiteral( "0" );
		if ( szValue != pItem->data( 0, ROLE_VALUE ).toString() )
		{
			// After the click is done with the row.
			QTimer::singleShot( 0, this, [ this, path = PathOf( pItem ), szValue ] {
				kv1::CDocument document = m_Parsed.m_Document;
				if ( kv1::CNode *pNode = NodeAt( document, path ); pNode && pNode->SetValue( szValue.toStdString() ) )
				{
					Apply( document, path );
				}
			} );
		}
	} );
	connect( m_pTree, &QTreeWidget::itemCollapsed, this, [ this ]( QTreeWidgetItem *pItem ) {
		if ( !m_bFilling )
		{
			m_Collapsed.insert( PathKey( PathOf( pItem ) ) );
		}
	} );
	connect( m_pTree, &QTreeWidget::itemExpanded, this, [ this ]( QTreeWidgetItem *pItem ) {
		if ( !m_bFilling )
		{
			m_Collapsed.remove( PathKey( PathOf( pItem ) ) );
		}
	} );
	connect( &CInterfaceMode::Instance(), &CInterfaceMode::Changed, this, &CMaterialForm::Fill );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialForm::SetTextureIcons( std::function<QIcon( const QString &, QString * )> pfnIcon )
{
	m_pfnIcon = std::move( pfnIcon );
	// Later: this comes while the list may be in the middle of a click on one of its rows.
	QTimer::singleShot( 0, this, &CMaterialForm::Fill );
}

//-----------------------------------------------------------------------------
// Purpose: Shows a material's text.
//-----------------------------------------------------------------------------
void CMaterialForm::SetText( const QString &szText )
{
	if ( szText == m_szText && !m_Parsed.m_Document.m_Roots.empty() )
	{
		return;
	}
	m_szText = szText;
	Fill();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QStringList CMaterialForm::GetTextureNames()
{
	if ( !m_bHaveTextureNames && m_pfnTextureNames )
	{
		m_TextureNames = m_pfnTextureNames();
		m_bHaveTextureNames = true;
	}
	return m_TextureNames;
}

//-----------------------------------------------------------------------------
// Purpose: The index of the file's first block, the material's own; -1 when there is none.
//-----------------------------------------------------------------------------
int CMaterialForm::RootIndex() const
{
	const auto &roots = m_Parsed.m_Document.m_Roots;
	const auto it = std::ranges::find_if( roots, &kv1::CNode::IsBlock );
	return it == roots.end() ? -1 : static_cast<int>( it - roots.begin() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMaterialForm::Shader() const
{
	const int nRoot = RootIndex();
	return nRoot < 0 ? QString{} : Qs( m_Parsed.m_Document.m_Roots[ static_cast<std::size_t>( nRoot ) ].GetKey() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<int> CMaterialForm::PathOf( const QTreeWidgetItem *pItem ) const
{
	return pItem ? ToPath( pItem->data( 0, ROLE_PATH ) ) : std::vector<int>{};
}

//-----------------------------------------------------------------------------
// Purpose: The row for a path (the shader's row for the block itself), or null.
//-----------------------------------------------------------------------------
QTreeWidgetItem *CMaterialForm::ItemAt( const std::vector<int> &path ) const
{
	std::vector<QTreeWidgetItem *> items;
	for ( int i = 0; i < m_pTree->topLevelItemCount(); i++ )
	{
		items.push_back( m_pTree->topLevelItem( i ) );
	}
	while ( !items.empty() )
	{
		QTreeWidgetItem *pItem = items.back();
		items.pop_back();
		if ( PathOf( pItem ) == path )
		{
			return pItem;
		}
		for ( int i = 0; i < pItem->childCount(); i++ )
		{
			items.push_back( pItem->child( i ) );
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The text without what is unticked, for the preview.
//-----------------------------------------------------------------------------
QString CMaterialForm::GetPreviewText() const
{
	if ( m_Hidden.isEmpty() )
	{
		return m_szText;
	}
	kv1::CDocument document = kv1::Parse( m_szText.toStdString() ).m_Document;
	const std::function<void( kv1::CNode &, const QString & )> prune = [ & ]( kv1::CNode &block, const QString &szChain ) {
		for ( std::size_t i = block.GetChildren().size(); i-- > 0; )
		{
			const QString szChild = ( szChain.isEmpty() ? QString{} : szChain + u'/' ) + Qs( block.Child( i ).GetKey() ).toLower();
			if ( m_Hidden.contains( szChild ) )
			{
				block.RemoveChild( i );
			}
			else if ( block.Child( i ).IsBlock() )
			{
				prune( block.Child( i ), szChild );
			}
		}
	};
	const auto root = std::ranges::find_if( document.m_Roots, &kv1::CNode::IsBlock );
	if ( root != document.m_Roots.end() )
	{
		prune( *root, {} );
	}
	return Qs( kv1::Write( document ) );
}

//-----------------------------------------------------------------------------
// Purpose: The row of a parameter of the material's own block, by key; null when it has none.
//-----------------------------------------------------------------------------
QTreeWidgetItem *CMaterialForm::FindItem( const QString &szKey ) const
{
	for ( int i = 0; i < m_pTree->topLevelItemCount(); i++ )
	{
		QTreeWidgetItem *pItem = m_pTree->topLevelItem( i );
		if ( pItem->data( 0, ROLE_KIND ).toInt() != KIND_SHADER && pItem->data( 0, ROLE_KEY ).toString().compare( szKey, Qt::CaseInsensitive ) == 0 )
		{
			return pItem;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The list from the text, keeping the picked row, the folded blocks and the scroll.
//-----------------------------------------------------------------------------
void CMaterialForm::Fill()
{
	const std::vector<int> current = PathOf( m_pTree->currentItem() );
	const int nScroll = m_pTree->verticalScrollBar()->value();
	m_bFilling = true;
	m_pTree->clear();
	m_Parsed = kv1::Parse( m_szText.toStdString() );
	const int nRoot = RootIndex();
	const bool bBlocked = m_Parsed.HasErrors() || nRoot < 0;
	m_pBlocked->setText( nRoot < 0 ? tr( "There is no shader block yet; start the text with a shader's name and { }." )
								   : tr( "The text has a mistake the list can't work round; fix it in the text (see Messages), then edit here again." ) );
	m_pBlocked->setVisible( bBlocked );
	m_pTree->setEnabled( !bBlocked );
	if ( nRoot >= 0 )
	{
		const kv1::CNode &root = m_Parsed.m_Document.m_Roots[ static_cast<std::size_t>( nRoot ) ];
		const xv::particles::PlainName_t shader = ParticleNames().Shader( root.GetKey() );
		auto *pShader = new QTreeWidgetItem( m_pTree, { tr( "Shader" ), Qs( root.GetKey() ) } );
		pShader->setData( 0, ROLE_PATH, ToVariant( { nRoot } ) );
		pShader->setData( 0, ROLE_KIND, KIND_SHADER );
		pShader->setData( 0, ROLE_KEY, Qs( root.GetKey() ) );
		pShader->setData( 0, ROLE_VALUE, Qs( root.GetKey() ) );
		pShader->setToolTip( 1, Qs( shader.m_szMeaning ) );
		pShader->setFlags( pShader->flags() | Qt::ItemIsEditable );
		QFont bold = m_pTree->font();
		bold.setBold( true );
		pShader->setFont( 1, bold );
		for ( std::size_t i = 0; i < root.GetChildren().size(); i++ )
		{
			AddItems( nullptr, root.Child( i ), { nRoot, static_cast<int>( i ) } );
		}
	}
	m_pTree->resizeColumnToContents( 0 );
	m_pTree->setColumnWidth( 0, std::min( m_pTree->columnWidth( 0 ) + 12, std::max( 140, m_pTree->width() * 2 / 5 ) ) );
	if ( QTreeWidgetItem *pCurrent = ItemAt( current ) )
	{
		m_pTree->setCurrentItem( pCurrent );
	}
	m_pTree->verticalScrollBar()->setValue( nScroll );
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose: A row for a node, and rows under it for a block's children.
//-----------------------------------------------------------------------------
void CMaterialForm::AddItems( QTreeWidgetItem *pParent, const kv1::CNode &node, std::vector<int> path )
{
	if ( !node.IsPair() && !node.IsBlock() )
	{
		return;
	}
	const bool bSimple = CInterfaceMode::Instance().IsSimple();
	const xv::particles::CNames &names = ParticleNames();
	const QString szKey = Qs( node.GetKey() );
	auto *pItem = pParent ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( m_pTree );
	pItem->setData( 0, ROLE_PATH, ToVariant( path ) );
	pItem->setData( 0, ROLE_KEY, szKey );
	pItem->setFlags( pItem->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable );
	// Its tick takes it out of the preview only; the file keeps it.
	const QString szChain = ( pParent ? pParent->data( 0, ROLE_CHAIN ).toString() + u'/' : QString{} ) + szKey.toLower();
	pItem->setData( 0, ROLE_CHAIN, szChain );
	const bool bHidden = m_Hidden.contains( szChain );
	pItem->setCheckState( 0, bHidden ? Qt::Unchecked : Qt::Checked );
	if ( bHidden )
	{
		QFont italic = m_pTree->font();
		italic.setItalic( true );
		pItem->setFont( 0, italic );
		pItem->setFont( 1, italic );
	}
	const QString szCondition = node.GetCondition() ? QStringLiteral( " %1" ).arg( Qs( *node.GetCondition() ) ) : QString{};
	if ( node.IsBlock() )
	{
		pItem->setData( 0, ROLE_KIND, KIND_BLOCK );
		pItem->setText( 0, szKey + szCondition );
		pItem->setText( 1, tr( "%n setting(s)", nullptr, static_cast<int>( node.GetChildren().size() ) ) );
		pItem->setForeground( 1, palette().color( QPalette::PlaceholderText ) );
		for ( std::size_t i = 0; i < node.GetChildren().size(); i++ )
		{
			std::vector<int> childPath = path;
			childPath.push_back( static_cast<int>( i ) );
			AddItems( pItem, node.Child( i ), childPath );
		}
		pItem->setExpanded( !m_Collapsed.contains( PathKey( path ) ) );
		return;
	}

	// A parameter, unless it is a proxy's setting.
	const bool bInProxies = pParent && pParent->parent() && pParent->parent()->data( 0, ROLE_KEY ).toString().compare( u"proxies", Qt::CaseInsensitive ) == 0;
	const xv::material::ParameterDef_t *pDef = bInProxies ? nullptr : MaterialParameters().Find( node.GetKey() );
	const auto nKind = pDef ? pDef->m_nKind : ParameterKind_t::PARAMETER_KIND_TEXT;
	const QString szValue = Qs( node.GetValue() );
	pItem->setData( 0, ROLE_KIND, static_cast<int>( nKind ) );
	pItem->setData( 0, ROLE_VALUE, szValue );
	const xv::particles::PlainName_t plain = names.MaterialParameter( node.GetKey() );
	const QString szMeaning = Qs( plain.m_szMeaning );
	pItem->setText( 0, ( bSimple && pDef ? Qs( plain.m_szName ) : szKey ) + szCondition );
	pItem->setToolTip( 0, ( bSimple && pDef ? szKey : Qs( plain.m_szName ) ) + ( szMeaning.isEmpty() ? QString{} : u'\n' + szMeaning ) );
	if ( !pDef && !bInProxies && ( szKey.startsWith( u'$' ) || szKey.startsWith( u'%' ) ) )
	{
		pItem->setForeground( 0, palette().color( QPalette::PlaceholderText ) );
		pItem->setToolTip( 0, tr( "Not a parameter this editor knows; the shader may still use it." ) );
	}
	pItem->setToolTip( 1, szMeaning );
	switch ( nKind )
	{
		case ParameterKind_t::PARAMETER_KIND_BOOL:
			pItem->setCheckState( 1, szValue != u"0" && !szValue.isEmpty() ? Qt::Checked : Qt::Unchecked );
			if ( szValue != u"0" && szValue != u"1" )
			{
				pItem->setText( 1, szValue );
			}
			break;
		case ParameterKind_t::PARAMETER_KIND_CHOICE:
		{
			QString szShown = szValue;
			for ( const xv::particles::NamedValue_t &choice : names.Choices( node.GetKey() ) )
			{
				if ( Qs( choice.m_szValue ) == szValue )
				{
					szShown = QStringLiteral( "%1 · %2" ).arg( szValue, Qs( choice.m_szName ) );
				}
			}
			pItem->setText( 1, szShown );
			break;
		}
		case ParameterKind_t::PARAMETER_KIND_COLOR:
			pItem->setText( 1, szValue );
			if ( const auto colour = ColourOf( szValue ) )
			{
				pItem->setIcon( 1, Swatch( *colour ) );
			}
			break;
		case ParameterKind_t::PARAMETER_KIND_TEXTURE:
			pItem->setText( 1, szValue );
			if ( m_pfnIcon )
			{
				QString szDescription;
				if ( const QIcon icon = m_pfnIcon( szKey, &szDescription ); !icon.isNull() )
				{
					pItem->setIcon( 1, icon );
				}
				if ( !szDescription.isEmpty() )
				{
					pItem->setToolTip( 1, szDescription + ( szMeaning.isEmpty() ? QString{} : u'\n' + szMeaning ) );
				}
			}
			break;
		default:
			pItem->setText( 1, szValue );
			break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A row's key or value as edited.
//-----------------------------------------------------------------------------
void CMaterialForm::CommitEdit( const QModelIndex &index, const QString &szText )
{
	const std::vector<int> path = ToPath( index.siblingAtColumn( 0 ).data( ROLE_PATH ) );
	const int nKind = index.siblingAtColumn( 0 ).data( ROLE_KIND ).toInt();
	kv1::CDocument document = m_Parsed.m_Document;
	kv1::CNode *pNode = NodeAt( document, path );
	if ( !pNode )
	{
		return;
	}
	const bool bKey = index.column() == 0 || nKind == KIND_SHADER;
	const QString szNew = bKey ? szText.trimmed() : szText;
	if ( ( bKey && ( szNew.isEmpty() || szNew == Qs( pNode->GetKey() ) ) ) || ( !bKey && szNew == Qs( pNode->GetValue() ) ) )
	{
		return;
	}
	if ( bKey ? !pNode->SetKey( szNew.toStdString() ) : !pNode->SetValue( szNew.toStdString() ) )
	{
		return; // a double quote can't be written
	}
	Apply( document, path );
}

//-----------------------------------------------------------------------------
// Purpose: Writes the edited document as the text and says so; refills the list after.
//-----------------------------------------------------------------------------
void CMaterialForm::Apply( const kv1::CDocument &document, const std::vector<int> &select )
{
	m_szText = Qs( kv1::Write( document ) );
	m_Parsed = kv1::Parse( m_szText.toStdString() );
	emit TextEdited( m_szText );
	// After the editor that made the edit has closed.
	QTimer::singleShot( 0, this, [ this, select ] {
		Fill();
		if ( QTreeWidgetItem *pItem = select.empty() ? nullptr : ItemAt( select ) )
		{
			m_pTree->setCurrentItem( pItem );
			m_pTree->scrollToItem( pItem );
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Sets a parameter of the material's own block, adding it if missing.
//-----------------------------------------------------------------------------
bool CMaterialForm::SetParameter( const QString &szKey, const QString &szValue )
{
	const int nRoot = RootIndex();
	if ( nRoot < 0 || m_Parsed.HasErrors() )
	{
		return false;
	}
	kv1::CDocument document = m_Parsed.m_Document;
	kv1::CNode &root = document.m_Roots[ static_cast<std::size_t>( nRoot ) ];
	const int nAt = ChildIndex( root, szKey );
	if ( nAt >= 0 )
	{
		if ( !root.Child( static_cast<std::size_t>( nAt ) ).SetValue( szValue.toStdString() ) )
		{
			return false;
		}
		Apply( document, { nRoot, nAt } );
		return true;
	}
	root.AppendChild( kv1::CNode::MakePair( szKey.toStdString(), szValue.toStdString() ) );
	Apply( document, { nRoot, static_cast<int>( root.GetChildren().size() ) - 1 } );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Removes a parameter (or block) of the picked row's block, else of the material's own.
//-----------------------------------------------------------------------------
bool CMaterialForm::RemoveParameter( const QString &szKey )
{
	const int nRoot = RootIndex();
	if ( nRoot < 0 || m_Parsed.HasErrors() )
	{
		return false;
	}
	kv1::CDocument document = m_Parsed.m_Document;
	// The picked row, when it is the one named; else the material's own block.
	std::vector<int> path = PathOf( m_pTree->currentItem() );
	if ( path.size() < 2 || m_pTree->currentItem()->data( 0, ROLE_KEY ).toString().compare( szKey, Qt::CaseInsensitive ) != 0 )
	{
		const int nAt = ChildIndex( document.m_Roots[ static_cast<std::size_t>( nRoot ) ], szKey );
		if ( nAt < 0 )
		{
			return false;
		}
		path = { nRoot, nAt };
	}
	std::vector<int> parentPath( path.begin(), path.end() - 1 );
	kv1::CNode *pParent = NodeAt( document, parentPath );
	if ( !pParent )
	{
		return false;
	}
	pParent->RemoveChild( static_cast<std::size_t>( path.back() ) );
	// The row that takes its place stays picked.
	std::vector<int> next = path;
	if ( static_cast<std::size_t>( next.back() ) >= pParent->GetChildren().size() )
	{
		next = pParent->GetChildren().empty() ? parentPath : std::vector<int>{};
		if ( next.empty() )
		{
			next = path;
			next.back()--;
		}
	}
	Apply( document, next );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMaterialForm::SetShader( const QString &szShader )
{
	const int nRoot = RootIndex();
	if ( nRoot < 0 || m_Parsed.HasErrors() || szShader.trimmed().isEmpty() )
	{
		return false;
	}
	kv1::CDocument document = m_Parsed.m_Document;
	if ( !document.m_Roots[ static_cast<std::size_t>( nRoot ) ].SetKey( szShader.trimmed().toStdString() ) )
	{
		return false;
	}
	Apply( document, { nRoot } );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Adds a parameter with the value it usually starts with, then edits that value.
//-----------------------------------------------------------------------------
bool CMaterialForm::AddParameter( const QString &szKey, const std::vector<int> &blockPath )
{
	const int nRoot = RootIndex();
	if ( nRoot < 0 || m_Parsed.HasErrors() || szKey.trimmed().isEmpty() )
	{
		return false;
	}
	kv1::CDocument document = m_Parsed.m_Document;
	const std::vector<int> path = blockPath.empty() ? std::vector<int>{ nRoot } : blockPath;
	kv1::CNode *pBlock = NodeAt( document, path );
	if ( !pBlock || !pBlock->IsBlock() )
	{
		return false;
	}
	std::vector<int> added = path;
	if ( const int nAt = ChildIndex( *pBlock, szKey ); nAt >= 0 )
	{
		added.push_back( nAt ); // already there: go to it
		if ( QTreeWidgetItem *pItem = ItemAt( added ) )
		{
			m_pTree->setCurrentItem( pItem );
			m_pTree->editItem( pItem, 1 );
		}
		return true;
	}
	const xv::material::ParameterDef_t *pDef = MaterialParameters().Find( szKey.trimmed().toStdString() );
	pBlock->AppendChild( kv1::CNode::MakePair( szKey.trimmed().toStdString(), pDef ? pDef->m_szStart : std::string{} ) );
	added.push_back( static_cast<int>( pBlock->GetChildren().size() ) - 1 );
	Apply( document, added );
	// Then on to its value, unless it is a switch (already on).
	if ( !pDef || pDef->m_nKind != ParameterKind_t::PARAMETER_KIND_BOOL )
	{
		QTimer::singleShot( 0, this, [ this, added ] {
			if ( QTreeWidgetItem *pItem = ItemAt( added ) )
			{
				m_pTree->editItem( pItem, 1 );
			}
		} );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Adds a proxy, with the settings it usually needs, to the material's Proxies block.
//-----------------------------------------------------------------------------
bool CMaterialForm::AddProxy( const QString &szProxy )
{
	const int nRoot = RootIndex();
	if ( nRoot < 0 || m_Parsed.HasErrors() || szProxy.trimmed().isEmpty() )
	{
		return false;
	}
	kv1::CDocument document = m_Parsed.m_Document;
	kv1::CNode &root = document.m_Roots[ static_cast<std::size_t>( nRoot ) ];
	int nProxies = ChildIndex( root, QStringLiteral( "Proxies" ) );
	if ( nProxies < 0 || !root.Child( static_cast<std::size_t>( nProxies ) ).IsBlock() )
	{
		root.AppendChild( kv1::CNode::MakeBlock( "Proxies" ) );
		nProxies = static_cast<int>( root.GetChildren().size() ) - 1;
	}
	kv1::CNode &proxies = root.Child( static_cast<std::size_t>( nProxies ) );
	kv1::CNode &proxy = proxies.AppendChild( kv1::CNode::MakeBlock( szProxy.trimmed().toStdString() ) );
	for ( const ProxyStart_t &start : PROXIES )
	{
		if ( szProxy.compare( QString::fromLatin1( start.m_pszName ), Qt::CaseInsensitive ) == 0 )
		{
			for ( const auto &[ pszKey, pszValue ] : start.m_Settings )
			{
				proxy.AppendChild( kv1::CNode::MakePair( pszKey, pszValue ) );
			}
		}
	}
	m_Collapsed.remove( PathKey( { nRoot, nProxies } ) );
	Apply( document, { nRoot, nProxies, static_cast<int>( proxies.GetChildren().size() ) - 1 } );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Asks which parameter to add to the block picked in the list (or the material's own).
//-----------------------------------------------------------------------------
void CMaterialForm::ChooseParameterToAdd()
{
	const int nRoot = RootIndex();
	if ( nRoot < 0 || m_Parsed.HasErrors() )
	{
		return;
	}
	std::vector<int> block{ nRoot };
	if ( const QTreeWidgetItem *pItem = m_pTree->currentItem(); pItem && pItem->data( 0, ROLE_KIND ).toInt() != KIND_SHADER )
	{
		block = PathOf( pItem );
		if ( pItem->data( 0, ROLE_KIND ).toInt() != KIND_BLOCK )
		{
			block.pop_back();
		}
	}
	kv1::CDocument document = m_Parsed.m_Document;
	const kv1::CNode *pBlock = NodeAt( document, block );
	if ( !pBlock )
	{
		return;
	}
	QStringList existing;
	for ( const kv1::CNode &child : pBlock->GetChildren() )
	{
		existing << Qs( child.GetKey() );
	}
	CAddParameterDialog dialog( Shader(), existing, this );
	if ( dialog.exec() == QDialog::Accepted )
	{
		AddParameter( dialog.GetKey(), block );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Picks the row written on a line of the text.
//-----------------------------------------------------------------------------
bool CMaterialForm::SelectLine( int nLine )
{
	const std::string szText = m_szText.toStdString();
	kv1::CDocument document = m_Parsed.m_Document;
	std::vector<QTreeWidgetItem *> items;
	for ( int i = m_pTree->topLevelItemCount() - 1; i >= 0; i-- )
	{
		items.push_back( m_pTree->topLevelItem( i ) );
	}
	while ( !items.empty() )
	{
		QTreeWidgetItem *pItem = items.back();
		items.pop_back();
		if ( const kv1::CNode *pNode = NodeAt( document, PathOf( pItem ) ) )
		{
			const std::size_t nOffset = std::min( pNode->GetKeyToken().m_nOffset, szText.size() );
			if ( 1 + std::count( szText.begin(), szText.begin() + static_cast<std::ptrdiff_t>( nOffset ), '\n' ) == nLine )
			{
				m_pTree->setCurrentItem( pItem );
				m_pTree->scrollToItem( pItem );
				m_pTree->setFocus();
				return true;
			}
		}
		for ( int i = pItem->childCount() - 1; i >= 0; i-- )
		{
			items.push_back( pItem->child( i ) );
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: A colour picked for a colour parameter, written as the file had it ([0-1] or {0-255}).
//-----------------------------------------------------------------------------
void CMaterialForm::PickColour( QTreeWidgetItem *pItem )
{
	const QString szValue = pItem->data( 0, ROLE_VALUE ).toString();
	const std::vector<int> path = PathOf( pItem );
	const QColor colour = QColorDialog::getColor( ColourOf( szValue ).value_or( Qt::white ), this, tr( "Pick a Colour" ) );
	if ( !colour.isValid() )
	{
		return;
	}
	const QString szNew = szValue.contains( u'{' ) ? QStringLiteral( "{%1 %2 %3}" ).arg( colour.red() ).arg( colour.green() ).arg( colour.blue() )
												   : QStringLiteral( "[%1 %2 %3]" ).arg( colour.redF(), 0, 'g', 3 ).arg( colour.greenF(), 0, 'g', 3 ).arg( colour.blueF(), 0, 'g', 3 );
	kv1::CDocument document = m_Parsed.m_Document;
	if ( kv1::CNode *pNode = NodeAt( document, path ); pNode && pNode->SetValue( szNew.toStdString() ) )
	{
		Apply( document, path );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Right-click: add a parameter or a proxy, pick a colour, rename or remove.
//-----------------------------------------------------------------------------
void CMaterialForm::ShowMenu( const QPoint &at )
{
	if ( m_Parsed.HasErrors() || RootIndex() < 0 )
	{
		return;
	}
	QTreeWidgetItem *pItem = m_pTree->itemAt( at );
	if ( pItem )
	{
		m_pTree->setCurrentItem( pItem );
	}
	const int nKind = pItem ? pItem->data( 0, ROLE_KIND ).toInt() : KIND_SHADER;
	QMenu menu( this );
	menu.addAction( tr( "Add Parameter…" ), this, &CMaterialForm::ChooseParameterToAdd );
	QMenu *pProxies = menu.addMenu( tr( "Add Proxy" ) );
	for ( const ProxyStart_t &start : PROXIES )
	{
		const QString szName = QString::fromLatin1( start.m_pszName );
		pProxies->addAction( szName, this, [ this, szName ] { AddProxy( szName ); } );
	}
	pProxies->addSeparator();
	pProxies->addAction( tr( "Other…" ), this, [ this ] {
		bool bOk = false;
		const QString szName = QInputDialog::getText( this, tr( "Add Proxy" ), tr( "Proxy name:" ), QLineEdit::Normal, {}, &bOk );
		if ( bOk )
		{
			AddProxy( szName );
		}
	} );
	if ( pItem )
	{
		menu.addSeparator();
		if ( nKind == static_cast<int>( ParameterKind_t::PARAMETER_KIND_COLOR ) )
		{
			menu.addAction( tr( "Pick Colour…" ), this, [ this, pItem ] { PickColour( pItem ); } );
		}
		if ( nKind != KIND_BLOCK && nKind != static_cast<int>( ParameterKind_t::PARAMETER_KIND_BOOL ) )
		{
			menu.addAction( tr( "Edit Value" ), this, [ this, pItem ] { m_pTree->editItem( pItem, 1 ); } );
		}
		if ( nKind != KIND_SHADER )
		{
			menu.addAction( tr( "Rename" ), this, [ this, pItem ] { m_pTree->editItem( pItem, 0 ); } );
			QAction *pRemove = menu.addAction( tr( "Remove" ), this, [ this, szKey = pItem->data( 0, ROLE_KEY ).toString() ] { RemoveParameter( szKey ); } );
			pRemove->setShortcut( QKeySequence::Delete );
		}
	}
	menu.exec( m_pTree->viewport()->mapToGlobal( at ) );
}
