//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Filter Control's two trees of visgroups, kept in step with what the document
//          shows, and its list of cordons.
//
//===========================================================================//
#include "map/MapFilterControl.h"

#include <set>
#include <string>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "map/Cordon.h"
#include "map/MapDocument.h"
#include "map/Scene.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A small square of a visgroup's colour, as the tree shows it beside the name.
//-----------------------------------------------------------------------------
QIcon Swatch( const xv::map::Color_t &color )
{
	QPixmap pixmap( 12, 12 );
	pixmap.fill( QColor( color.m_nR, color.m_nG, color.m_nB ) );
	QPainter painter( &pixmap );
	painter.setPen( Qt::black );
	painter.drawRect( 0, 0, 11, 11 );
	return QIcon( pixmap );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QTreeWidget *MakeTree( const QString &szName )
{
	auto *pTree = new QTreeWidget;
	pTree->setObjectName( szName );
	pTree->setHeaderHidden( true );
	pTree->setColumnCount( 1 );
	pTree->setUniformRowHeights( true );
	return pTree;
}

//-----------------------------------------------------------------------------
// Purpose: A box as the Cordons tab lists it: its lowest corner and its highest.
//-----------------------------------------------------------------------------
QString BoxText( const xv::map::Box_t &box )
{
	auto point = []( const xv::map::Point_t &p ) { return QStringLiteral( "%1 %2 %3" ).arg( p[ 0 ], 0, 'g', 8 ).arg( p[ 1 ], 0, 'g', 8 ).arg( p[ 2 ], 0, 'g', 8 ); };
	return QStringLiteral( "(%1) – (%2)" ).arg( point( box.m_Mins ), point( box.m_Maxs ) );
}

//-----------------------------------------------------------------------------
// Purpose: A name no cordon has: "cordon" (Hammer's one), then "cordon2", "cordon3"...
//-----------------------------------------------------------------------------
std::string FreeCordonName( const std::vector<xv::map::Cordon_t> &cordons )
{
	std::set<std::string> taken;
	for ( const xv::map::Cordon_t &cordon : cordons )
	{
		taken.insert( std::string{ cordon.m_szName } );
	}
	std::string szName = "cordon";
	for ( int n = 2; taken.contains( szName ); n++ )
	{
		szName = "cordon" + std::to_string( n );
	}
	return szName;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapFilterControl::CMapFilterControl( CMapDocument &document, QWidget *pParent )
	: QWidget( pParent ),
	  m_Document( document )
{
	setObjectName( QStringLiteral( "mapFilterControl" ) );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pUser = MakeTree( QStringLiteral( "mapVisgroupsUser" ) );
	m_pAuto = MakeTree( QStringLiteral( "mapVisgroupsAuto" ) );
	m_pTabs = new QTabWidget;
	m_pTabs->setTabPosition( QTabWidget::South );
	m_pTabs->addTab( m_pUser, tr( "User" ) );
	m_pTabs->addTab( m_pAuto, tr( "Auto" ) );

	// Cordons: the switch for them all, the list, and its buttons.
	auto *pCordonPage = new QWidget;
	auto *pCordonLayout = new QVBoxLayout( pCordonPage );
	pCordonLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pCordonsOn = new QCheckBox( tr( "Cordon on" ) );
	m_pCordonsOn->setObjectName( QStringLiteral( "mapCordonsOn" ) );
	m_pCordonsOn->setToolTip( tr( "Toggle Cordon State: while it is on, what lies outside the ticked cordons is hidden and left out of compiles" ) );
	m_pCordons = MakeTree( QStringLiteral( "mapCordons" ) );
	m_pCordons->setToolTip( tr( "Tick a cordon to use it, double-click to rename it; Edit Cordon Bounds drags the box picked here" ) );
	auto *pCordonButtons = new QHBoxLayout;
	auto *pNew = new QPushButton( tr( "New" ) );
	pNew->setObjectName( QStringLiteral( "mapCordonsNew" ) );
	pNew->setToolTip( tr( "A new cordon round the selection, to drag into shape" ) );
	m_pAddBox = new QPushButton( tr( "Add Box" ) );
	m_pAddBox->setObjectName( QStringLiteral( "mapCordonsAddBox" ) );
	m_pAddBox->setToolTip( tr( "Another box for the picked cordon" ) );
	m_pDeleteCordon = new QPushButton( tr( "Delete" ) );
	m_pDeleteCordon->setObjectName( QStringLiteral( "mapCordonsDelete" ) );
	m_pDeleteCordon->setToolTip( tr( "Delete the picked box, or the cordon when it is its only one" ) );
	for ( QPushButton *pButton : { pNew, m_pAddBox, m_pDeleteCordon } )
	{
		pCordonButtons->addWidget( pButton );
	}
	pCordonButtons->addStretch( 1 );
	pCordonLayout->addWidget( m_pCordonsOn );
	pCordonLayout->addWidget( m_pCordons, 1 );
	pCordonLayout->addLayout( pCordonButtons );
	m_pTabs->addTab( pCordonPage, tr( "Cordons" ) );
	pLayout->addWidget( m_pTabs, 1 );

	auto *pButtons = new QHBoxLayout;
	auto *pMark = new QPushButton( tr( "Mark" ) );
	pMark->setObjectName( QStringLiteral( "mapVisgroupsMark" ) );
	pMark->setToolTip( tr( "Select the objects in the visgroup" ) );
	auto *pShowAll = new QPushButton( tr( "Show All" ) );
	pShowAll->setObjectName( QStringLiteral( "mapVisgroupsShowAll" ) );
	pButtons->addWidget( pMark );
	pButtons->addWidget( pShowAll );
	pButtons->addStretch( 1 );
	pLayout->addLayout( pButtons );

	// Mark and Show All are the visgroups' buttons.
	connect( m_pTabs, &QTabWidget::currentChanged, this, [ this, pMark, pShowAll ]( int nTab ) {
		pMark->setVisible( m_pTabs->widget( nTab ) != m_pCordons->parentWidget() );
		pShowAll->setVisible( pMark->isVisible() );
	} );
	connect( pMark, &QPushButton::clicked, this, &CMapFilterControl::Mark );
	connect( pShowAll, &QPushButton::clicked, this, [ this ] { m_Document.ShowAll(); } );
	connect( m_pUser, &QTreeWidget::itemChanged, this, &CMapFilterControl::ItemChanged );
	connect( m_pAuto, &QTreeWidget::itemChanged, this, &CMapFilterControl::ItemChanged );
	connect( &m_Document, &CMapDocument::Loaded, this, &CMapFilterControl::Rebuild );
	// An edit makes a new scene with automatic visgroups of its own: the old ones the items
	// pointed at go away with the old scene.
	connect( &m_Document, &CMapDocument::SceneEdited, this, [ this ] { m_Document.MeasureEditListener( tr( "Filter Control" ), [ this ] { Rebuild(); } ); } );
	connect( &m_Document, &CMapDocument::VisibilityChanged, this, &CMapFilterControl::UpdateStates );

	connect( pNew, &QPushButton::clicked, this, &CMapFilterControl::NewCordon );
	connect( m_pAddBox, &QPushButton::clicked, this, &CMapFilterControl::AddCordonBox );
	connect( m_pDeleteCordon, &QPushButton::clicked, this, &CMapFilterControl::DeleteCordon );
	connect( m_pCordons, &QTreeWidget::itemChanged, this, &CMapFilterControl::CordonChanged );
	connect( m_pCordonsOn, &QCheckBox::toggled, this, [ this ]( bool bOn ) {
		if ( const xv::map::CScene *pScene = m_Document.GetScene(); !m_bUpdating && pScene )
		{
			EditCordons( pScene->m_Map.m_Cordons, bOn, bOn ? tr( "Cordon On" ) : tr( "Cordon Off" ) );
		}
	} );
	// A click picks once the tree is done with it: what the pick sets off rebuilds the tree.
	connect( m_pCordons, &QTreeWidget::currentItemChanged, this, [ this ]( QTreeWidgetItem *pItem ) {
		if ( m_bUpdating || !pItem )
		{
			return;
		}
		QTreeWidgetItem *pTop = pItem->parent() ? pItem->parent() : pItem;
		const auto nCordon = static_cast<std::size_t>( m_pCordons->indexOfTopLevelItem( pTop ) );
		const auto nBox = pItem->parent() ? static_cast<std::size_t>( pTop->indexOfChild( pItem ) ) : std::size_t{ 0 };
		QMetaObject::invokeMethod( this, [ this, nCordon, nBox ] {
			if ( nCordon != m_nPickedCordon || nBox != m_nPickedBox )
			{
				emit CordonEditing();
				PickCordon( nCordon, nBox );
			} }, Qt::QueuedConnection );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFilterControl::Rebuild()
{
	m_bUpdating = true;
	m_pUser->clear();
	m_pAuto->clear();
	m_AutoGroups.clear();
	const Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
	if ( const xv::map::CScene *pScene = m_Document.GetScene() )
	{
		// Visgroups nest: each is placed under its parent once all exist.
		const std::vector<xv::map::Visgroup_t> &visgroups = pScene->m_Map.m_Visgroups;
		std::vector<QTreeWidgetItem *> items;
		for ( const xv::map::Visgroup_t &visgroup : visgroups )
		{
			auto *pItem = new QTreeWidgetItem( QStringList{ QString::fromUtf8( visgroup.m_szName.data(), static_cast<qsizetype>( visgroup.m_szName.size() ) ) } );
			pItem->setData( 0, Qt::UserRole, visgroup.m_nId );
			pItem->setIcon( 0, Swatch( visgroup.m_Color ) );
			pItem->setFlags( flags );
			pItem->setCheckState( 0, Qt::Checked );
			items.push_back( pItem );
		}
		for ( std::size_t i = 0; i < visgroups.size(); i++ )
		{
			const int nParent = visgroups[ i ].m_nParent;
			if ( nParent >= 0 && static_cast<std::size_t>( nParent ) < items.size() )
			{
				items[ static_cast<std::size_t>( nParent ) ]->addChild( items[ i ] );
			}
			else
			{
				m_pUser->addTopLevelItem( items[ i ] );
			}
		}
		m_pUser->expandAll();

		auto add = [ & ]( auto &self, const xv::map::AutoVisgroup_t &group, QTreeWidgetItem *pParent ) -> void {
			auto *pItem = new QTreeWidgetItem( QStringList{ QString::fromStdString( group.m_szName ) } );
			pItem->setData( 0, Qt::UserRole, static_cast<int>( m_AutoGroups.size() ) );
			pItem->setFlags( flags );
			pItem->setCheckState( 0, Qt::Checked );
			m_AutoGroups.push_back( &group );
			if ( pParent )
			{
				pParent->addChild( pItem );
			}
			else
			{
				m_pAuto->addTopLevelItem( pItem );
			}
			for ( const xv::map::AutoVisgroup_t &child : group.m_Children )
			{
				self( self, child, pItem );
			}
		};
		add( add, pScene->m_AutoVisgroups, nullptr );
		m_pAuto->expandToDepth( 0 );
	}
	m_bUpdating = false;
	RebuildCordons();
	UpdateStates();
}

//-----------------------------------------------------------------------------
// Purpose: A cordon's item holds its name (editable) and switch; under it, its boxes. The
//          pick stays where it was, or on the nearest still there.
//-----------------------------------------------------------------------------
void CMapFilterControl::RebuildCordons()
{
	m_bUpdating = true;
	m_pCordons->clear();
	const xv::map::CScene *pScene = m_Document.GetScene();
	const std::vector<xv::map::Cordon_t> none;
	const std::vector<xv::map::Cordon_t> &cordons = pScene ? pScene->m_Map.m_Cordons : none;
	for ( const xv::map::Cordon_t &cordon : cordons )
	{
		auto *pItem = new QTreeWidgetItem( QStringList{ QString::fromUtf8( cordon.m_szName.data(), static_cast<qsizetype>( cordon.m_szName.size() ) ) } );
		pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEditable );
		pItem->setCheckState( 0, cordon.m_bActive ? Qt::Checked : Qt::Unchecked );
		for ( const xv::map::Box_t &box : cordon.m_Boxes )
		{
			auto *pBox = new QTreeWidgetItem( QStringList{ BoxText( box ) } );
			pBox->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable );
			pItem->addChild( pBox );
		}
		m_pCordons->addTopLevelItem( pItem );
	}
	m_pCordons->expandAll();
	m_nPickedCordon = cordons.empty() ? 0 : std::min( m_nPickedCordon, cordons.size() - 1 );
	m_nPickedBox = cordons.empty() || cordons[ m_nPickedCordon ].m_Boxes.empty() ? 0 : std::min( m_nPickedBox, cordons[ m_nPickedCordon ].m_Boxes.size() - 1 );
	if ( QTreeWidgetItem *pTop = m_pCordons->topLevelItem( static_cast<int>( m_nPickedCordon ) ) )
	{
		QTreeWidgetItem *pBox = pTop->child( static_cast<int>( m_nPickedBox ) );
		m_pCordons->setCurrentItem( pBox && pTop->childCount() > 1 ? pBox : pTop );
	}
	m_pCordonsOn->setEnabled( !cordons.empty() );
	m_pCordonsOn->setChecked( pScene && pScene->m_Map.m_bCordonsActive );
	m_pAddBox->setEnabled( !cordons.empty() );
	m_pDeleteCordon->setEnabled( !cordons.empty() );
	m_bUpdating = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFilterControl::PickCordon( std::size_t nCordon, std::size_t nBox )
{
	m_nPickedCordon = nCordon;
	m_nPickedBox = nBox;
	RebuildCordons();
	emit CordonPicked();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFilterControl::EditCordons( std::vector<xv::map::Cordon_t> cordons, bool bActive, const QString &szName )
{
	if ( const xv::map::CScene *pScene = m_Document.GetScene() )
	{
		m_Document.Edit( xv::map::SetCordons( *pScene, std::move( cordons ), bActive ), szName );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A cordon's tick or name changed by hand. The edit waits for the tree to finish with
//          the item, which the edit's rebuilding deletes.
//-----------------------------------------------------------------------------
void CMapFilterControl::CordonChanged( QTreeWidgetItem *pItem )
{
	if ( m_bUpdating || pItem->parent() )
	{
		return;
	}
	const int nCordon = m_pCordons->indexOfTopLevelItem( pItem );
	const bool bActive = pItem->checkState( 0 ) == Qt::Checked;
	const std::string szName = pItem->text( 0 ).trimmed().toStdString();
	QMetaObject::invokeMethod( this, [ this, nCordon, bActive, szName ] {
		const xv::map::CScene *pScene = m_Document.GetScene();
		if ( !pScene || nCordon < 0 || static_cast<std::size_t>( nCordon ) >= pScene->m_Map.m_Cordons.size() )
		{
			RebuildCordons();
			return;
		}
		std::vector<xv::map::Cordon_t> cordons = pScene->m_Map.m_Cordons;
		xv::map::Cordon_t &cordon = cordons[ static_cast<std::size_t>( nCordon ) ];
		if ( bActive != cordon.m_bActive )
		{
			cordon.m_bActive = bActive;
			EditCordons( std::move( cordons ), pScene->m_Map.m_bCordonsActive, tr( "Switch Cordon" ) );
		}
		else if ( !szName.empty() && szName != cordon.m_szName )
		{
			// SetCordons stores the name; until then it is this string.
			cordon.m_szName = szName;
			EditCordons( std::move( cordons ), pScene->m_Map.m_bCordonsActive, tr( "Rename Cordon" ) );
		}
		else
		{
			RebuildCordons();
		} }, Qt::QueuedConnection );
}

//-----------------------------------------------------------------------------
// Purpose: The new cordon is on and picked, and its box wants dragging into shape.
//-----------------------------------------------------------------------------
void CMapFilterControl::NewCordon()
{
	// The box being dragged goes in first, which makes another scene.
	emit CordonEditing();
	const xv::map::CScene *pScene = m_Document.GetScene();
	const std::optional<xv::map::Box_t> box = m_pfnNewBox ? m_pfnNewBox() : std::nullopt;
	if ( !pScene || !box )
	{
		return;
	}
	std::vector<xv::map::Cordon_t> cordons = pScene->m_Map.m_Cordons;
	const std::string szName = FreeCordonName( cordons );
	xv::map::Cordon_t cordon;
	cordon.m_szName = szName;
	cordon.m_bActive = true;
	cordon.m_Boxes.push_back( *box );
	cordons.push_back( cordon );
	const std::size_t nCordon = cordons.size() - 1;
	EditCordons( std::move( cordons ), pScene->m_Map.m_bCordonsActive, tr( "New Cordon" ) );
	PickCordon( nCordon, 0 );
	emit CordonEditWanted();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFilterControl::AddCordonBox()
{
	// The box being dragged goes in first, which makes another scene.
	emit CordonEditing();
	const xv::map::CScene *pScene = m_Document.GetScene();
	const std::optional<xv::map::Box_t> box = m_pfnNewBox ? m_pfnNewBox() : std::nullopt;
	if ( !pScene || !box || m_nPickedCordon >= pScene->m_Map.m_Cordons.size() )
	{
		return;
	}
	std::vector<xv::map::Cordon_t> cordons = pScene->m_Map.m_Cordons;
	cordons[ m_nPickedCordon ].m_Boxes.push_back( *box );
	const std::size_t nCordon = m_nPickedCordon;
	const std::size_t nBox = cordons[ nCordon ].m_Boxes.size() - 1;
	EditCordons( std::move( cordons ), pScene->m_Map.m_bCordonsActive, tr( "Add Cordon Box" ) );
	PickCordon( nCordon, nBox );
	emit CordonEditWanted();
}

//-----------------------------------------------------------------------------
// Purpose: The last cordon gone turns the cordon off.
//-----------------------------------------------------------------------------
void CMapFilterControl::DeleteCordon()
{
	// The box being dragged goes in first, which makes another scene.
	emit CordonEditing();
	const xv::map::CScene *pScene = m_Document.GetScene();
	if ( !pScene || m_nPickedCordon >= pScene->m_Map.m_Cordons.size() )
	{
		return;
	}
	std::vector<xv::map::Cordon_t> cordons = pScene->m_Map.m_Cordons;
	std::vector<xv::map::Box_t> &boxes = cordons[ m_nPickedCordon ].m_Boxes;
	const QTreeWidgetItem *pCurrent = m_pCordons->currentItem();
	if ( pCurrent && pCurrent->parent() && boxes.size() > 1 && m_nPickedBox < boxes.size() )
	{
		boxes.erase( boxes.begin() + static_cast<std::ptrdiff_t>( m_nPickedBox ) );
		EditCordons( std::move( cordons ), pScene->m_Map.m_bCordonsActive, tr( "Delete Cordon Box" ) );
	}
	else
	{
		cordons.erase( cordons.begin() + static_cast<std::ptrdiff_t>( m_nPickedCordon ) );
		const bool bActive = pScene->m_Map.m_bCordonsActive && !cordons.empty();
		EditCordons( std::move( cordons ), bActive, tr( "Delete Cordon" ) );
	}
	PickCordon( m_nPickedCordon, m_nPickedBox );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> CMapFilterControl::MembersOf( const QTreeWidgetItem *pItem ) const
{
	if ( !pItem )
	{
		return {};
	}
	const int nData = pItem->data( 0, Qt::UserRole ).toInt();
	if ( pItem->treeWidget() == m_pUser )
	{
		return m_Document.GetVisgroupMembers( nData );
	}
	if ( nData < 0 || static_cast<std::size_t>( nData ) >= m_AutoGroups.size() )
	{
		return {};
	}
	return xv::map::AutoVisgroupMembers( *m_AutoGroups[ static_cast<std::size_t>( nData ) ] );
}

//-----------------------------------------------------------------------------
// Purpose: Each item's tick: all of its objects shown, none, or some.
//-----------------------------------------------------------------------------
void CMapFilterControl::UpdateStates()
{
	if ( !m_Document.GetScene() )
	{
		return;
	}
	m_bUpdating = true;
	for ( QTreeWidgetItemIterator it( m_pUser ); *it; ++it )
	{
		( *it )->setCheckState( 0, static_cast<Qt::CheckState>( m_Document.GetVisgroupState( ( *it )->data( 0, Qt::UserRole ).toInt() ) ) );
	}
	for ( QTreeWidgetItemIterator it( m_pAuto ); *it; ++it )
	{
		( *it )->setCheckState( 0, static_cast<Qt::CheckState>( m_Document.GetAutoState( MembersOf( *it ) ) ) );
	}
	m_bUpdating = false;
}

//-----------------------------------------------------------------------------
// Purpose: A tick changed by hand: shows or hides the group's objects.
//-----------------------------------------------------------------------------
void CMapFilterControl::ItemChanged( QTreeWidgetItem *pItem )
{
	if ( m_bUpdating )
	{
		return;
	}
	const bool bShown = pItem->checkState( 0 ) == Qt::Checked;
	if ( pItem->treeWidget() == m_pUser )
	{
		m_Document.SetVisgroupShown( pItem->data( 0, Qt::UserRole ).toInt(), bShown );
	}
	else
	{
		m_Document.SetAutoShown( MembersOf( pItem ), bShown );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFilterControl::Mark()
{
	const auto *pTree = qobject_cast<const QTreeWidget *>( m_pTabs->currentWidget() );
	if ( pTree && pTree->currentItem() )
	{
		m_Document.SetSelection( MembersOf( pTree->currentItem() ) );
	}
}
