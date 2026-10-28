//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Filter Control's two trees of visgroups, kept in step with what the document
//          shows.
//
//===========================================================================//
#include "map/MapFilterControl.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

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

	connect( pMark, &QPushButton::clicked, this, &CMapFilterControl::Mark );
	connect( pShowAll, &QPushButton::clicked, this, [ this ] { m_Document.ShowAll(); } );
	connect( m_pUser, &QTreeWidget::itemChanged, this, &CMapFilterControl::ItemChanged );
	connect( m_pAuto, &QTreeWidget::itemChanged, this, &CMapFilterControl::ItemChanged );
	connect( &m_Document, &CMapDocument::Loaded, this, &CMapFilterControl::Rebuild );
	// An edit makes a new scene with automatic visgroups of its own: the old ones the items
	// pointed at go away with the old scene.
	connect( &m_Document, &CMapDocument::SceneEdited, this, &CMapFilterControl::Rebuild );
	connect( &m_Document, &CMapDocument::VisibilityChanged, this, &CMapFilterControl::UpdateStates );
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
	UpdateStates();
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
