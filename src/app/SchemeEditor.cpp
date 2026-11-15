//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SchemeEditor.h.
//
//===========================================================================//
#include "SchemeEditor.h"

#include <algorithm>
#include <fstream>
#include <set>

#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include "FileDialogs.h"
#include "vfs/Vfs.h"

namespace
{

// One edit of the text, undone by putting the old text back.
class CSchemeTextCommand : public QUndoCommand
{
public:
	CSchemeTextCommand( CSchemeEditor &editor, std::string szBefore, std::string szAfter, const QString &szWhat )
		: QUndoCommand( szWhat ),
		  m_Editor( editor ),
		  m_szBefore( std::move( szBefore ) ),
		  m_szAfter( std::move( szAfter ) )
	{
	}
	void undo() override { m_Editor.ApplyText( m_szBefore ); }
	void redo() override { m_Editor.ApplyText( m_szAfter ); }

private:
	CSchemeEditor &m_Editor;
	std::string m_szBefore;
	std::string m_szAfter;
};

//-----------------------------------------------------------------------------
// Purpose: A small square of a colour over a checker, so its alpha shows.
//-----------------------------------------------------------------------------
QIcon Swatch( const xv::hud::Color_t &color )
{
	QPixmap pixmap( 14, 14 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	painter.fillRect( 0, 0, 14, 14, Qt::white );
	painter.fillRect( 0, 0, 7, 7, QColor( 190, 190, 190 ) );
	painter.fillRect( 7, 7, 7, 7, QColor( 190, 190, 190 ) );
	painter.fillRect( 0, 0, 14, 14, QColor( color.m_nR, color.m_nG, color.m_nB, color.m_nA ) );
	painter.setPen( QColor( 0, 0, 0, 120 ) );
	painter.drawRect( 0, 0, 13, 13 );
	return QIcon( pixmap );
}

//-----------------------------------------------------------------------------
// Purpose: A path as one string, to remember which items were open and which was selected
//          across a refill.
//-----------------------------------------------------------------------------
QString PathKey( const CSchemeEditor::Path_t &path )
{
	QString szKey;
	for ( const std::size_t n : path )
		szKey += QString::number( n ) + u'/';
	return szKey;
}

//-----------------------------------------------------------------------------
// Purpose: szBase, or szBase with the first number after it that no child of the block has
//          for a key ("NewKey2"). A block whose children are numbered ("1", "2": a font's
//          entries, a border's lines) gets the next number.
//-----------------------------------------------------------------------------
std::string UniqueKey( const xv::kv1::CNode &block, std::string szBase )
{
	const auto has = [ & ]( const std::string &szKey ) {
		return std::ranges::any_of( block.GetChildren(), [ & ]( const xv::kv1::CNode &child ) { return xv::kv1::EqualsIgnoreCase( child.GetKey(), szKey ); } );
	};
	const bool bNumbered = !block.GetChildren().empty() && std::ranges::all_of( block.GetChildren(), []( const xv::kv1::CNode &child ) {
		const std::string szKey = child.GetKey();
		return !szKey.empty() && std::ranges::all_of( szKey, []( unsigned char ch ) { return std::isdigit( ch ) != 0; } );
	} );
	if ( bNumbered )
		szBase.clear();
	else if ( !has( szBase ) )
		return szBase;
	for ( int n = bNumbered ? 1 : 2;; n++ )
	{
		const std::string szKey = szBase + std::to_string( n );
		if ( !has( szKey ) )
			return szKey;
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A bar (filter, the screen the fonts are shown for, the edits) over a tab for each
//          block of the scheme.
//-----------------------------------------------------------------------------
CSchemeEditor::CSchemeEditor( const QString &szName, const std::string &szText, const std::filesystem::path &disk, bool bFromGame, QWidget *pParent )
	: QWidget( pParent ),
	  m_szName( szName ),
	  m_Disk( disk ),
	  m_bFromGame( bFromGame ),
	  m_Document( xv::kv1::Parse( szText ).m_Document )
{
	setObjectName( QStringLiteral( "schemeEditor" ) );
	m_Scheme = xv::hud::CScheme::FromDocument( m_Document );
	m_pUndo = new QUndoStack( this );
	connect( m_pUndo, &QUndoStack::cleanChanged, this, [ this ]( bool bClean ) { emit ModifiedChanged( !bClean ); } );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 4, 4, 4, 4 );
	auto *pBar = new QHBoxLayout;
	m_pFilter = new QLineEdit;
	m_pFilter->setObjectName( QStringLiteral( "schemeFilter" ) );
	m_pFilter->setPlaceholderText( tr( "Filter names and values" ) );
	m_pFilter->setClearButtonEnabled( true );
	connect( m_pFilter, &QLineEdit::textChanged, this, &CSchemeEditor::Fill );
	pBar->addWidget( m_pFilter, 1 );
	const auto button = [ & ]( const QString &szText, const QString &szObject, const QString &szTip ) {
		auto *pButton = new QPushButton( szText );
		pButton->setObjectName( szObject );
		pButton->setToolTip( szTip );
		pBar->addWidget( pButton );
		return pButton;
	};
	m_pAdd = button( tr( "Add" ), QStringLiteral( "schemeAdd" ), tr( "A new colour, setting, font, border or key, by the list shown and what is selected in it" ) );
	connect( m_pAdd, &QPushButton::clicked, this, [ this ] {
		const Section_t *pSection = GetCurrentSection();
		const Path_t target = GetAddTarget();
		if ( !pSection || target.empty() )
			return;
		const bool bTop = target == FindSection( pSection->m_szBlock );
		if ( bTop && pSection->m_szBlock == "Fonts" )
			AddFont( QStringLiteral( "NewFont" ) );
		else if ( bTop && pSection->m_szBlock == "Borders" )
			AddBlock( target, QStringLiteral( "NewBorder" ) );
		else if ( bTop && pSection->m_szBlock == "Colors" )
			AddPair( target, QStringLiteral( "NewColor" ), QStringLiteral( "255 255 255 255" ) );
		else if ( bTop && pSection->m_szBlock == "CustomFontFiles" )
			AddPair( target, QStringLiteral( "1" ), QStringLiteral( "resource/font.ttf" ) );
		else
			AddPair( target, QStringLiteral( "NewKey" ), QString{} );
	} );
	m_pAddBlock = button( tr( "Add Block" ), QStringLiteral( "schemeAddBlock" ), tr( "A new block in the one selected: another entry of a font, another side or line of a border" ) );
	connect( m_pAddBlock, &QPushButton::clicked, this, [ this ] {
		if ( const Path_t target = GetAddTarget(); !target.empty() )
			AddBlock( target, QStringLiteral( "NewBlock" ) );
	} );
	m_pDuplicate = button( tr( "Duplicate" ), QStringLiteral( "schemeDuplicate" ), tr( "A copy of what is selected, after it" ) );
	connect( m_pDuplicate, &QPushButton::clicked, this, [ this ] {
		if ( const QTreeWidget *pTree = GetCurrentTree(); pTree && pTree->currentItem() )
			DuplicateNode( PathOf( pTree->currentItem() ) );
	} );
	m_pRemove = button( tr( "Remove" ), QStringLiteral( "schemeRemove" ), tr( "Takes what is selected out of the file" ) );
	connect( m_pRemove, &QPushButton::clicked, this, [ this ] {
		if ( const QTreeWidget *pTree = GetCurrentTree(); pTree && pTree->currentItem() )
			RemoveNode( PathOf( pTree->currentItem() ) );
	} );
	m_pColor = button( tr( "Colour…" ), QStringLiteral( "schemeColor" ), tr( "Picks the selected colour, with its alpha" ) );
	connect( m_pColor, &QPushButton::clicked, this, &CSchemeEditor::PickColor );
	pBar->addWidget( new QLabel( tr( "Fonts on a screen" ) ) );
	m_pScreen = new QComboBox;
	m_pScreen->setObjectName( QStringLiteral( "schemeScreen" ) );
	m_pScreen->setToolTip( tr( "A scheme's fonts have entries for ranges of screen heights; the Fonts list shows the one each font gets on this screen" ) );
	for ( const int nTall : { 480, 600, 768, 900, 1024, 1080, 1200, 1440, 2160 } )
		m_pScreen->addItem( tr( "%1 high" ).arg( nTall ), nTall );
	m_pScreen->setCurrentIndex( m_pScreen->findData( m_nScreenTall ) );
	connect( m_pScreen, &QComboBox::currentIndexChanged, this, [ this ] { SetScreenTall( m_pScreen->currentData().toInt() ); } );
	pBar->addWidget( m_pScreen );
	pLayout->addLayout( pBar );

	m_pTabs = new QTabWidget;
	m_pTabs->setObjectName( QStringLiteral( "schemeTabs" ) );
	m_pTabs->setDocumentMode( true );
	const auto addSection = [ this ]( const char *pszBlock, const QString &szTab, const QString &szObject, const QString &szValue ) {
		auto *pTree = new QTreeWidget;
		pTree->setObjectName( szObject );
		// The last column: the systems a line is for, when it has a condition ([$WIN32]).
		pTree->setHeaderLabels( std::string_view( pszBlock ) == "Fonts" ? QStringList{ tr( "Name" ), szValue, tr( "On this screen" ), tr( "Only on" ) } : QStringList{ tr( "Name" ), szValue, tr( "Only on" ) } );
		pTree->headerItem()->setToolTip( pTree->columnCount() - 1, tr( "A line with a condition is read only on the systems named: [$WIN32] on Windows, [$POSIX] on Linux and macOS, [$X360] on the console. The ones not read on this system are greyed" ) );
		pTree->setContextMenuPolicy( Qt::CustomContextMenu );
		pTree->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked );
		pTree->setAlternatingRowColors( true );
		connect( pTree, &QTreeWidget::itemChanged, this, &CSchemeEditor::ItemChanged );
		connect( pTree, &QTreeWidget::currentItemChanged, this, &CSchemeEditor::UpdateButtons );
		connect( pTree, &QTreeWidget::customContextMenuRequested, this, [ this, pTree ]( const QPoint &at ) { ShowMenu( pTree, at ); } );
		m_pTabs->addTab( pTree, szTab );
		m_Sections.push_back( { pszBlock, pTree } );
	};
	addSection( "Colors", tr( "Colours" ), QStringLiteral( "schemeColors" ), tr( "Value" ) );
	addSection( "BaseSettings", tr( "Settings" ), QStringLiteral( "schemeSettings" ), tr( "Value" ) );
	addSection( "Fonts", tr( "Fonts" ), QStringLiteral( "schemeFonts" ), tr( "Value" ) );
	addSection( "Borders", tr( "Borders" ), QStringLiteral( "schemeBorders" ), tr( "Value" ) );
	addSection( "CustomFontFiles", tr( "Font Files" ), QStringLiteral( "schemeFontFiles" ), tr( "Value" ) );
	addSection( "", tr( "Whole File" ), QStringLiteral( "schemeAll" ), tr( "Value" ) );
	connect( m_pTabs, &QTabWidget::currentChanged, this, &CSchemeEditor::UpdateButtons );
	pLayout->addWidget( m_pTabs, 1 );

	Fill();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CSchemeEditor::GetText() const
{
	return xv::kv1::Write( m_Document );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSchemeEditor::IsModified() const
{
	return !m_pUndo->isClean();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const xv::kv1::CNode *CSchemeEditor::NodeAt( const Path_t &path ) const
{
	return NodeAt( const_cast<xv::kv1::CDocument &>( m_Document ), path );
}

//-----------------------------------------------------------------------------
// Purpose: Down from the root the path starts with.
//-----------------------------------------------------------------------------
xv::kv1::CNode *CSchemeEditor::NodeAt( xv::kv1::CDocument &document, const Path_t &path ) const
{
	if ( path.empty() || path.front() >= document.m_Roots.size() )
		return nullptr;
	xv::kv1::CNode *pNode = &document.m_Roots[ path.front() ];
	for ( std::size_t n = 1; n < path.size(); n++ )
	{
		if ( !pNode->IsBlock() || path[ n ] >= pNode->GetChildren().size() )
			return nullptr;
		pNode = &pNode->Child( path[ n ] );
	}
	return pNode;
}

//-----------------------------------------------------------------------------
// Purpose: The Scheme block's first block of that name with no condition against it.
//-----------------------------------------------------------------------------
CSchemeEditor::Path_t CSchemeEditor::FindSection( std::string_view szSection ) const
{
	for ( std::size_t nRoot = 0; nRoot < m_Document.m_Roots.size(); nRoot++ )
	{
		const xv::kv1::CNode &root = m_Document.m_Roots[ nRoot ];
		if ( !root.IsBlock() || !xv::kv1::EqualsIgnoreCase( root.GetKey(), "Scheme" ) )
			continue;
		if ( szSection.empty() )
			return { nRoot };
		for ( std::size_t n = 0; n < root.GetChildren().size(); n++ )
		{
			const xv::kv1::CNode &child = root.Child( n );
			if ( child.IsBlock() && !child.GetCondition() && xv::kv1::EqualsIgnoreCase( child.GetKey(), szSection ) )
				return { nRoot, n };
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSchemeEditor::SetNodeValue( const Path_t &path, const QString &szValue )
{
	xv::kv1::CDocument document = m_Document;
	xv::kv1::CNode *pNode = NodeAt( document, path );
	if ( !pNode || !pNode->IsPair() || pNode->GetValue() == szValue.toStdString() || !pNode->SetValue( szValue.toStdString() ) )
		return false;
	Edit( document, tr( "Set %1" ).arg( QString::fromStdString( pNode->GetKey() ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSchemeEditor::SetNodeKey( const Path_t &path, const QString &szKey )
{
	xv::kv1::CDocument document = m_Document;
	xv::kv1::CNode *pNode = NodeAt( document, path );
	if ( !pNode || path.size() < 2 || szKey.trimmed().isEmpty() || pNode->GetKey() == szKey.toStdString() )
		return false;
	const QString szBefore = QString::fromStdString( pNode->GetKey() );
	if ( !pNode->SetKey( szKey.toStdString() ) )
		return false;
	Edit( document, tr( "Rename %1" ).arg( szBefore ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Not the roots themselves: a scheme without its Scheme block is no scheme.
//-----------------------------------------------------------------------------
bool CSchemeEditor::RemoveNode( const Path_t &path )
{
	xv::kv1::CDocument document = m_Document;
	if ( path.size() < 2 || !NodeAt( document, path ) )
		return false;
	Path_t parent = path;
	parent.pop_back();
	const QString szKey = QString::fromStdString( NodeAt( document, path )->GetKey() );
	NodeAt( document, parent )->RemoveChild( path.back() );
	Edit( document, tr( "Remove %1" ).arg( szKey ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSchemeEditor::DuplicateNode( const Path_t &path )
{
	xv::kv1::CDocument document = m_Document;
	if ( path.size() < 2 || !NodeAt( document, path ) )
		return false;
	Path_t parent = path;
	parent.pop_back();
	xv::kv1::CNode *pParent = NodeAt( document, parent );
	xv::kv1::CNode copy = pParent->Child( path.back() );
	const std::string szKey = copy.GetKey();
	if ( !copy.SetKey( UniqueKey( *pParent, szKey + "Copy" ) ) )
		return false;
	pParent->InsertChild( path.back() + 1, std::move( copy ), xv::kv1::Layout_t::LAYOUT_KEEP );
	Edit( document, tr( "Duplicate %1" ).arg( QString::fromStdString( szKey ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSchemeEditor::AddPair( const Path_t &parent, const QString &szKey, const QString &szValue )
{
	xv::kv1::CDocument document = m_Document;
	xv::kv1::CNode *pParent = NodeAt( document, parent );
	if ( !pParent || !pParent->IsBlock() )
		return false;
	const std::string szUnique = UniqueKey( *pParent, szKey.toStdString() );
	pParent->AppendChild( xv::kv1::CNode::MakePair( szUnique, szValue.toStdString() ) );
	Edit( document, tr( "Add %1" ).arg( QString::fromStdString( szUnique ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSchemeEditor::AddBlock( const Path_t &parent, const QString &szKey )
{
	xv::kv1::CDocument document = m_Document;
	xv::kv1::CNode *pParent = NodeAt( document, parent );
	if ( !pParent || !pParent->IsBlock() )
		return false;
	const std::string szUnique = UniqueKey( *pParent, szKey.toStdString() );
	pParent->AppendChild( xv::kv1::CNode::MakeBlock( szUnique ) );
	Edit( document, tr( "Add %1" ).arg( QString::fromStdString( szUnique ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A font as the game's schemes write one: an entry "1" with a face, a height and a
//          weight.
//-----------------------------------------------------------------------------
bool CSchemeEditor::AddFont( const QString &szName )
{
	xv::kv1::CDocument document = m_Document;
	xv::kv1::CNode *pFonts = NodeAt( document, FindSection( "Fonts" ) );
	if ( !pFonts )
		return false;
	const std::string szUnique = UniqueKey( *pFonts, szName.toStdString() );
	xv::kv1::CNode &font = pFonts->AppendChild( xv::kv1::CNode::MakeBlock( szUnique ) );
	xv::kv1::CNode &entry = font.AppendChild( xv::kv1::CNode::MakeBlock( "1" ) );
	entry.AppendChild( xv::kv1::CNode::MakePair( "name", "Verdana" ) );
	entry.AppendChild( xv::kv1::CNode::MakePair( "tall", "12" ) );
	entry.AppendChild( xv::kv1::CNode::MakePair( "weight", "500" ) );
	entry.AppendChild( xv::kv1::CNode::MakePair( "antialias", "1" ) );
	Edit( document, tr( "Add %1" ).arg( QString::fromStdString( szUnique ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSchemeEditor::SetScreenTall( int nTall )
{
	m_nScreenTall = nTall;
	if ( const int nIndex = m_pScreen->findData( nTall ); nIndex >= 0 && nIndex != m_pScreen->currentIndex() )
	{
		m_pScreen->setCurrentIndex( nIndex ); // comes back here through the signal
		return;
	}
	Fill();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSchemeEditor::Path_t CSchemeEditor::PathOf( const QTreeWidgetItem *pItem )
{
	Path_t path;
	if ( pItem )
	{
		for ( const QVariant &step : pItem->data( 0, Qt::UserRole ).toList() )
			path.push_back( static_cast<std::size_t>( step.toULongLong() ) );
	}
	return path;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QTreeWidget *CSchemeEditor::GetCurrentTree() const
{
	return qobject_cast<QTreeWidget *>( m_pTabs->currentWidget() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CSchemeEditor::Section_t *CSchemeEditor::GetCurrentSection() const
{
	const auto found = std::ranges::find( m_Sections, GetCurrentTree(), &Section_t::m_pTree );
	return found != m_Sections.end() ? &*found : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSchemeEditor::Path_t CSchemeEditor::GetAddTarget() const
{
	const Section_t *pSection = GetCurrentSection();
	if ( !pSection )
		return {};
	if ( const QTreeWidgetItem *pItem = pSection->m_pTree->currentItem() )
	{
		Path_t path = PathOf( pItem );
		const xv::kv1::CNode *pNode = NodeAt( path );
		if ( pNode && !pNode->IsBlock() )
			path.pop_back();
		// In the lists of plain names (colours, settings, font files) a new one goes to the
		// list, whatever is selected.
		if ( pNode && path.size() >= 2 )
			return path;
	}
	return FindSection( pSection->m_szBlock );
}

//-----------------------------------------------------------------------------
// Purpose: Every list again, what was open and selected in each kept.
//-----------------------------------------------------------------------------
void CSchemeEditor::Fill()
{
	m_bFilling = true;
	for ( const Section_t &section : m_Sections )
	{
		std::set<QString> open;
		const auto remember = [ & ]( const auto &self, const QTreeWidgetItem *pItem ) -> void {
			if ( pItem->isExpanded() )
				open.insert( PathKey( PathOf( pItem ) ) );
			for ( int i = 0; i < pItem->childCount(); i++ )
				self( self, pItem->child( i ) );
		};
		for ( int i = 0; i < section.m_pTree->topLevelItemCount(); i++ )
			remember( remember, section.m_pTree->topLevelItem( i ) );
		const QString szCurrent = section.m_pTree->currentItem() ? PathKey( PathOf( section.m_pTree->currentItem() ) ) : QString{};
		const bool bFirst = section.m_pTree->topLevelItemCount() == 0;
		FillTree( section );
		const auto restore = [ & ]( const auto &self, QTreeWidgetItem *pItem ) -> void {
			const QString szKey = PathKey( PathOf( pItem ) );
			// The whole file starts with its Scheme block open.
			pItem->setExpanded( open.contains( szKey ) || ( bFirst && section.m_szBlock.empty() && !pItem->parent() ) || !m_pFilter->text().trimmed().isEmpty() );
			if ( !szCurrent.isEmpty() && szKey == szCurrent )
				section.m_pTree->setCurrentItem( pItem );
			for ( int i = 0; i < pItem->childCount(); i++ )
				self( self, pItem->child( i ) );
		};
		for ( int i = 0; i < section.m_pTree->topLevelItemCount(); i++ )
			restore( restore, section.m_pTree->topLevelItem( i ) );
		section.m_pTree->resizeColumnToContents( 0 );
		section.m_pTree->setColumnWidth( 0, std::clamp( section.m_pTree->columnWidth( 0 ) + 12, 160, 420 ) );
		for ( int n = 1; n + 1 < section.m_pTree->columnCount(); n++ )
			section.m_pTree->setColumnWidth( n, section.m_szBlock == "Fonts" && n == 2 ? 200 : 260 );
	}
	m_bFilling = false;
	UpdateButtons();
}

//-----------------------------------------------------------------------------
// Purpose: A section's list: what is in each of the Scheme block's blocks of its name (the
//          whole file: its roots).
//-----------------------------------------------------------------------------
void CSchemeEditor::FillTree( const Section_t &section )
{
	section.m_pTree->clear();
	section.m_pTree->setRootIsDecorated( section.m_szBlock != "Colors" && section.m_szBlock != "BaseSettings" && section.m_szBlock != "CustomFontFiles" );
	for ( std::size_t nRoot = 0; nRoot < m_Document.m_Roots.size(); nRoot++ )
	{
		const xv::kv1::CNode &root = m_Document.m_Roots[ nRoot ];
		if ( !root.IsBlock() )
			continue;
		if ( section.m_szBlock.empty() )
		{
			auto *pItem = new QTreeWidgetItem( section.m_pTree, { QString::fromStdString( root.GetKey() ) } );
			pItem->setData( 0, Qt::UserRole, QVariantList{ static_cast<qulonglong>( nRoot ) } );
			AddItems( section, pItem, root, { nRoot }, 1 );
			continue;
		}
		if ( !xv::kv1::EqualsIgnoreCase( root.GetKey(), "Scheme" ) )
			continue;
		for ( std::size_t n = 0; n < root.GetChildren().size(); n++ )
		{
			const xv::kv1::CNode &child = root.Child( n );
			if ( child.IsBlock() && xv::kv1::EqualsIgnoreCase( child.GetKey(), section.m_szBlock ) )
				AddItems( section, nullptr, child, { nRoot, n }, 0 );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A block's children as items under pParent (the list itself when null), those the
//          filter leaves: a key by its name or value, a block by its name or anything in
//          it. Values that are colours get their swatch; a font, the face it gets.
//-----------------------------------------------------------------------------
void CSchemeEditor::AddItems( const Section_t &section, QTreeWidgetItem *pParent, const xv::kv1::CNode &block, const Path_t &path, int nDepth )
{
	const QString szFilter = m_pFilter->text().trimmed();
	const auto matches = [ & ]( const auto &self, const xv::kv1::CNode &node ) -> bool {
		if ( szFilter.isEmpty() || QString::fromStdString( node.GetKey() ).contains( szFilter, Qt::CaseInsensitive ) || QString::fromStdString( node.GetValue() ).contains( szFilter, Qt::CaseInsensitive ) )
			return true;
		return std::ranges::any_of( node.GetChildren(), [ & ]( const xv::kv1::CNode &child ) { return self( self, child ); } );
	};
	for ( std::size_t n = 0; n < block.GetChildren().size(); n++ )
	{
		const xv::kv1::CNode &child = block.Child( n );
		if ( ( !child.IsPair() && !child.IsBlock() ) || !matches( matches, child ) )
			continue;
		Path_t childPath = path;
		childPath.push_back( n );
		QVariantList steps;
		for ( const std::size_t nStep : childPath )
			steps.append( static_cast<qulonglong>( nStep ) );
		const QString szKey = QString::fromStdString( child.GetKey() );
		const auto condition = child.GetCondition();
		auto *pItem = pParent ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( section.m_pTree );
		pItem->setText( 0, szKey );
		pItem->setData( 0, Qt::UserRole, steps );
		pItem->setFlags( pItem->flags() | Qt::ItemIsEditable );
		if ( condition )
		{
			// Which systems it is for, in its own column; greyed when this one isn't among them.
			const int nColumn = section.m_pTree->columnCount() - 1;
			pItem->setText( nColumn, QString::fromStdString( xv::hud::DescribeCondition( *condition ) ) );
			pItem->setToolTip( nColumn, QString::fromUtf8( condition->data(), static_cast<qsizetype>( condition->size() ) ) );
			if ( !xv::hud::ConditionHolds( *condition ) )
			{
				for ( int n = 0; n <= nColumn; n++ )
					pItem->setForeground( n, palette().placeholderText() );
			}
		}
		if ( child.IsPair() )
		{
			pItem->setText( 1, QString::fromStdString( child.GetValue() ) );
			const bool bFace = xv::kv1::EqualsIgnoreCase( child.GetKey(), "name" ) || xv::kv1::EqualsIgnoreCase( child.GetKey(), "font" );
			if ( const auto color = bFace ? std::nullopt : m_Scheme.Resolve( child.GetValue() ) )
				pItem->setIcon( 1, Swatch( *color ) );
			continue;
		}
		if ( section.m_szBlock == "Fonts" && nDepth == 0 )
		{
			// The face it gets on the screen picked, and its name in that face.
			if ( const auto font = m_Scheme.ResolveFont( child.GetKey(), { m_nScreenTall * 16 / 9, m_nScreenTall, true } ) )
			{
				pItem->setText( 1, tr( "%1, %2 px" ).arg( QString::fromStdString( font->m_szFace ) ).arg( font->m_nPixels ) );
				QFont sample( QString::fromStdString( font->m_szFace ) );
				sample.setPixelSize( std::clamp( font->m_nPixels, 11, 18 ) );
				if ( font->m_nWeight >= 100 )
					sample.setWeight( static_cast<QFont::Weight>( std::clamp( font->m_nWeight, 100, 900 ) ) );
				sample.setItalic( font->m_bItalic );
				// A sample beside it, not the name itself: an icon font's name would be icons.
				pItem->setText( 2, QStringLiteral( "AaBbCc 0123456789" ) );
				pItem->setFont( 2, sample );
			}
			else
			{
				pItem->setText( 1, tr( "none for this screen" ) );
			}
			pItem->setForeground( 1, palette().placeholderText() );
		}
		AddItems( section, pItem, child, childPath, nDepth + 1 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A name or a value typed in a list goes into the file; what can't (a block has no
//          value, an empty name) is put back as it was.
//-----------------------------------------------------------------------------
void CSchemeEditor::ItemChanged( QTreeWidgetItem *pItem, int nColumn )
{
	if ( m_bFilling )
		return;
	const Path_t path = PathOf( pItem );
	const bool bDone = nColumn == 0 ? SetNodeKey( path, pItem->text( 0 ) ) : nColumn == 1 && SetNodeValue( path, pItem->text( 1 ) );
	if ( !bDone )
		Fill();
}

//-----------------------------------------------------------------------------
// Purpose: The edits that apply to what is selected in the list shown.
//-----------------------------------------------------------------------------
void CSchemeEditor::UpdateButtons()
{
	const QTreeWidget *pTree = GetCurrentTree();
	const QTreeWidgetItem *pItem = pTree ? pTree->currentItem() : nullptr;
	const Path_t path = PathOf( pItem );
	const xv::kv1::CNode *pNode = NodeAt( path );
	const bool bOwn = pNode && path.size() >= 2;
	const Section_t *pSection = GetCurrentSection();
	m_pAdd->setEnabled( !GetAddTarget().empty() );
	m_pAddBlock->setEnabled( pSection && pSection->m_szBlock != "Colors" && pSection->m_szBlock != "BaseSettings" && pSection->m_szBlock != "CustomFontFiles" && !GetAddTarget().empty() );
	m_pDuplicate->setEnabled( bOwn );
	m_pRemove->setEnabled( bOwn );
	m_pColor->setEnabled( bOwn && pNode->IsPair() && m_Scheme.Resolve( pNode->GetValue() ).has_value() );
}

//-----------------------------------------------------------------------------
// Purpose: The selected value's colour picked in the colour window, written as the four
//          numbers. A value that names another colour becomes numbers of its own.
//-----------------------------------------------------------------------------
void CSchemeEditor::PickColor()
{
	const QTreeWidget *pTree = GetCurrentTree();
	const Path_t path = PathOf( pTree ? pTree->currentItem() : nullptr );
	const xv::kv1::CNode *pNode = NodeAt( path );
	if ( !pNode || !pNode->IsPair() )
		return;
	const xv::hud::Color_t now = m_Scheme.Resolve( pNode->GetValue() ).value_or( xv::hud::Color_t{} );
	const QColor picked = QColorDialog::getColor( QColor( now.m_nR, now.m_nG, now.m_nB, now.m_nA ), this, tr( "Colour of %1" ).arg( QString::fromStdString( pNode->GetKey() ) ), QColorDialog::ShowAlphaChannel );
	if ( picked.isValid() )
		SetNodeValue( path, QStringLiteral( "%1 %2 %3 %4" ).arg( picked.red() ).arg( picked.green() ).arg( picked.blue() ).arg( picked.alpha() ) );
}

//-----------------------------------------------------------------------------
// Purpose: The bar's edits, at the pointer.
//-----------------------------------------------------------------------------
void CSchemeEditor::ShowMenu( QTreeWidget *pTree, const QPoint &at )
{
	if ( QTreeWidgetItem *pItem = pTree->itemAt( at ) )
		pTree->setCurrentItem( pItem );
	UpdateButtons();
	QMenu menu( this );
	for ( QPushButton *pButton : { m_pAdd, m_pAddBlock, m_pDuplicate, m_pRemove, m_pColor } )
	{
		QAction *pAction = menu.addAction( pButton->text(), pButton, &QPushButton::click );
		pAction->setEnabled( pButton->isEnabled() );
	}
	menu.exec( pTree->viewport()->mapToGlobal( at ) );
}

//-----------------------------------------------------------------------------
// Purpose: One undo step from the file as it is to document.
//-----------------------------------------------------------------------------
void CSchemeEditor::Edit( const xv::kv1::CDocument &document, const QString &szWhat )
{
	const std::string szBefore = GetText();
	const std::string szAfter = xv::kv1::Write( document );
	if ( szBefore != szAfter )
		m_pUndo->push( new CSchemeTextCommand( *this, szBefore, szAfter, szWhat ) );
}

//-----------------------------------------------------------------------------
// Purpose: Reads the text again and shows it.
//-----------------------------------------------------------------------------
void CSchemeEditor::ApplyText( const std::string &szText )
{
	m_Document = xv::kv1::Parse( szText ).m_Document;
	m_Scheme = xv::hud::CScheme::FromDocument( m_Document );
	Fill();
	emit Changed();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CSchemeEditor::GetTabTitle() const
{
	const QString szFile = m_Disk.empty() ? m_szName.section( u'/', -1 ) : QString::fromStdU16String( m_Disk.filename().u16string() );
	return ( IsModified() ? QStringLiteral( "*" ) : QString{} ) + szFile;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CSchemeEditor::GetSessionEntry() const
{
	if ( m_bFromGame )
		return QStringLiteral( "scheme\tgame\t%1" ).arg( m_szName );
	if ( m_Disk.empty() )
		return {};
	return QStringLiteral( "scheme\tdisk\t%1" ).arg( QString::fromStdU16String( m_Disk.u16string() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Save, drop the changes, or keep the tab.
//-----------------------------------------------------------------------------
bool CSchemeEditor::ConfirmClose()
{
	if ( !IsModified() )
		return true;
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved Scheme" ), tr( "Save the changes to %1?" ).arg( GetTabTitle().remove( 0, 1 ) ), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );
	if ( nAnswer == QMessageBox::Save )
		return Save();
	return nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose: Back to its file; a game file (in a VPK, maybe) asks where instead.
//-----------------------------------------------------------------------------
bool CSchemeEditor::Save()
{
	if ( m_Disk.empty() || m_bFromGame )
		return SaveAs();
	return WriteTo( m_Disk );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSchemeEditor::SaveAs()
{
	const QString szStart = m_Disk.empty() ? m_szName.section( u'/', -1 ) : QString::fromStdU16String( m_Disk.u16string() );
	const QString szFile = AskSaveFile( this, tr( "Save Scheme" ), szStart, tr( "Resource files (*.res);;All files (*)" ) );
	if ( szFile.isEmpty() )
		return false;
	return WriteTo( std::filesystem::path( szFile.toStdU16String() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Writes the text as it is; the tab then belongs to that file.
//-----------------------------------------------------------------------------
bool CSchemeEditor::WriteTo( const std::filesystem::path &file )
{
	const std::string szText = GetText();
	std::ofstream stream( file, std::ios::binary | std::ios::trunc );
	if ( !stream || !stream.write( szText.data(), static_cast<std::streamsize>( szText.size() ) ) )
	{
		QMessageBox::warning( this, tr( "Cannot save" ), tr( "Could not write %1." ).arg( QString::fromStdU16String( file.u16string() ) ) );
		return false;
	}
	m_Disk = file;
	m_bFromGame = false;
	m_pUndo->setClean();
	emit ModifiedChanged( false );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A .res whose first block is the Scheme block.
//-----------------------------------------------------------------------------
bool CSchemeEditor::IsSchemeFile( const std::filesystem::path &file, std::string_view szText )
{
	if ( !xv::kv1::EqualsIgnoreCase( xv::vfs::ToUtf8( file.extension() ), ".res" ) )
		return false;
	const xv::kv1::CDocument document = xv::kv1::Parse( szText ).m_Document;
	for ( const xv::kv1::CNode &root : document.m_Roots )
	{
		if ( root.IsBlock() )
			return xv::kv1::EqualsIgnoreCase( root.GetKey(), "Scheme" );
	}
	return false;
}
