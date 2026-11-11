//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapFogPanel.h.
//
//===========================================================================//
#include "MapFogPanel.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "map/Lighting.h"
#include "map/MapEditor.h"
#include "map/MapEntityFields.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A square of the fog's colour (and its second colour beside it when it blends).
//-----------------------------------------------------------------------------
QIcon Swatch( const xv::map::Fog_t &fog )
{
	const auto colour = []( const xv::CVec3 &vec ) { return QColor::fromRgbF( vec.m_flX, vec.m_flY, vec.m_flZ ); };
	QPixmap pixmap( 16, 16 );
	pixmap.fill( colour( fog.m_vecColor ) );
	if ( fog.m_bBlend )
	{
		QPainter painter( &pixmap );
		painter.fillRect( 8, 0, 8, 16, colour( fog.m_vecColor2 ) );
	}
	return QIcon( pixmap );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapFogPanel::CMapFogPanel( CMapEditor &editor, QWidget *pParent )
	: QWidget( pParent ),
	  m_Editor( editor )
{
	auto *pLayout = new QVBoxLayout( this );
	m_pList = new QListWidget;
	m_pList->setObjectName( QStringLiteral( "mapFogList" ) );
	m_pList->setToolTip( tr( "The fog the textured 3D views show (with the Show bar's Fog on)" ) );
	pLayout->addWidget( m_pList, 1 );
	m_pList->setMinimumHeight( 60 );
	auto *pRow = new QHBoxLayout;
	m_pSelect = new QPushButton( tr( "Select" ) );
	m_pSelect->setObjectName( QStringLiteral( "mapFogSelect" ) );
	m_pSelect->setToolTip( tr( "Select the picked env_fog_controller" ) );
	pRow->addWidget( m_pSelect );
	pRow->addStretch( 1 );
	pLayout->addLayout( pRow );
	m_pNote = new QLabel;
	m_pNote->setWordWrap( true );
	pLayout->addWidget( m_pNote );

	// The picked controller's keys (with "as the map starts", the one that starts on).
	const std::vector<EntityField_t> fogKeys{ { "fogenable", {}, {}, {} }, { "fogcolor", {}, {}, {} }, { "fogcolor2", {}, {}, {} }, { "fogdir", {}, {}, {} }, { "fogstart", {}, {}, {} },
		{ "fogend", {}, {}, {} }, { "fogmaxdensity", {}, {}, {} }, { "fogradial", {}, {}, {} }, { "farz", {}, {}, {} }, { "fogblend", {}, {}, {} }, { "use_angles", {}, {}, {} } };
	m_pFields = new CMapEntityFields( m_Editor, { "env_fog_controller" }, fogKeys );
	m_pFields->SetSelectButton( false ); // the list's Select does it
	pLayout->addWidget( MakeFoldSection( tr( "Fog Values" ), m_pFields, QStringLiteral( "mapFogValues" ), true ) );
	std::vector<EntityField_t> skyKeys = fogKeys;
	std::erase_if( skyKeys, []( const EntityField_t &field ) { return field.m_szKey == "farz"; } );
	m_pSkyFields = new CMapEntityFields( m_Editor, { "sky_camera" }, skyKeys );
	m_pSkySection = MakeFoldSection( tr( "3D Skybox Fog (sky_camera)" ), m_pSkyFields, QStringLiteral( "mapFogSky" ), false );
	pLayout->addWidget( m_pSkySection );
	connect( m_pSkyFields, &CMapEntityFields::Changed, this, [ this ] { m_pSkySection->setVisible( m_pSkyFields->GetEntity().has_value() ); } );
	pLayout->addStretch( 0 );

	connect( m_pList, &QListWidget::currentRowChanged, this, [ this ]( int nRow ) {
		if ( m_bFilling || nRow < 0 )
			return;
		m_Editor.SetFogController( m_pList->item( nRow )->data( Qt::UserRole ).toInt() );
	} );
	connect( m_pSelect, &QPushButton::clicked, this, [ this ] {
		const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
		const QListWidgetItem *pItem = m_pList->currentItem();
		const int nId = pItem ? pItem->data( Qt::UserRole ).toInt() : 0;
		if ( !pScene || nId == 0 )
			return;
		for ( const xv::map::FogController_t &controller : xv::map::ReadFogControllers( pScene->m_Map ) )
		{
			if ( controller.m_nId == nId )
				m_Editor.GetDocument().SetSelection( { pScene->EntityObject( controller.m_nEntity ) } );
		}
	} );
	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, &CMapFogPanel::Refresh );
	connect( &m_Editor.GetDocument(), &CMapDocument::Loaded, this, &CMapFogPanel::Refresh );
	connect( &m_Editor, &CMapEditor::FogControllerChanged, this, &CMapFogPanel::Refresh );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: "As the map starts" first (the first that starts on), then every controller with
//          its name, range and colour; a picked one that is gone falls back to the first.
//-----------------------------------------------------------------------------
void CMapFogPanel::Refresh()
{
	m_bFilling = true;
	m_pList->clear();
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	const std::vector<xv::map::FogController_t> controllers = pScene ? xv::map::ReadFogControllers( pScene->m_Map ) : std::vector<xv::map::FogController_t>{};
	auto *pStart = new QListWidgetItem( tr( "As the map starts" ), m_pList );
	pStart->setData( Qt::UserRole, 0 );
	pStart->setToolTip( tr( "The first env_fog_controller with fog on at the start" ) );
	int nCurrent = 0;
	for ( const xv::map::FogController_t &controller : controllers )
	{
		const QString szName = controller.m_szName.empty() ? tr( "(unnamed, id %1)" ).arg( controller.m_nId ) : QString::fromStdString( controller.m_szName );
		QString szText = tr( "%1: %2 to %3" ).arg( szName ).arg( std::lround( controller.m_Fog.m_flStart ) ).arg( std::lround( controller.m_Fog.m_flEnd ) );
		if ( !controller.m_bEnabled )
			szText += tr( " (off at the start)" );
		auto *pItem = new QListWidgetItem( Swatch( controller.m_Fog ), szText, m_pList );
		pItem->setData( Qt::UserRole, controller.m_nId );
		if ( controller.m_nId == m_Editor.GetSettings().m_nFogController )
			nCurrent = m_pList->count() - 1;
	}
	m_pList->setCurrentRow( nCurrent );
	m_pSelect->setEnabled( nCurrent > 0 );
	m_pNote->setText( tr( "The map has no env_fog_controller." ) );
	m_pNote->setVisible( controllers.empty() );
	// The fields follow the picked controller; at the map's start, the one that starts on.
	int nShown = m_pList->item( nCurrent )->data( Qt::UserRole ).toInt();
	if ( nShown == 0 )
	{
		const auto on = std::ranges::find_if( controllers, &xv::map::FogController_t::m_bEnabled );
		nShown = on != controllers.end() ? on->m_nId : 0;
	}
	m_pFields->SetEntityId( nShown );
	m_pSkySection->setVisible( m_pSkyFields->GetEntity().has_value() );
	m_bFilling = false;
	// The picked one deleted: back to the map's start.
	if ( nCurrent == 0 && m_Editor.GetSettings().m_nFogController != 0 )
		m_Editor.SetFogController( 0 );
}
