//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapEditor.h.
//
//===========================================================================//
#include "MapEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QAction>
#include <QActionGroup>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>

#include "map/MapConstants.h"
#include "map/MapFilterControl.h"
#include "map/MapPanes.h"
#include "map/MapProperties.h"
#include "map/MapResources.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"

namespace
{

enum class MapIcon_t
{
	MAP_ICON_SELECT,
	MAP_ICON_MAGNIFY,
	MAP_ICON_CAMERA,
};

//-----------------------------------------------------------------------------
// Purpose: Our own pictures for the tool bar (Hammer's are Valve's to keep).
//-----------------------------------------------------------------------------
QIcon MakeIcon( MapIcon_t nIcon, const QColor &color )
{
	QPixmap pixmap( 32, 32 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	painter.setRenderHint( QPainter::Antialiasing );
	painter.setPen( QPen{ color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin } );
	switch ( nIcon )
	{
		case MapIcon_t::MAP_ICON_SELECT:
		{
			QPainterPath arrow;
			arrow.moveTo( 9, 5 );
			arrow.lineTo( 9, 25 );
			arrow.lineTo( 14, 20 );
			arrow.lineTo( 18, 28 );
			arrow.lineTo( 21, 27 );
			arrow.lineTo( 17, 19 );
			arrow.lineTo( 24, 19 );
			arrow.closeSubpath();
			painter.drawPath( arrow );
			break;
		}
		case MapIcon_t::MAP_ICON_MAGNIFY:
			painter.drawEllipse( QPointF{ 13, 13 }, 7.5, 7.5 );
			painter.drawLine( QPointF{ 18.5, 18.5 }, QPointF{ 27, 27 } );
			break;
		case MapIcon_t::MAP_ICON_CAMERA:
			painter.drawRoundedRect( QRectF{ 4, 10, 18, 13 }, 2, 2 );
			painter.drawPolygon( QPolygonF{ { QPointF{ 22, 14 }, QPointF{ 29, 10 }, QPointF{ 29, 23 }, QPointF{ 22, 19 } } } );
			break;
	}
	return QIcon{ pixmap };
}

//-----------------------------------------------------------------------------
// Purpose: A power of two the grid can be, nearest to a map's saved spacing.
//-----------------------------------------------------------------------------
int GridSizeFor( int nSpacing )
{
	int nSize = 1;
	while ( nSize < nSpacing && nSize < mapstyle::GRID_LARGEST )
	{
		nSize *= 2;
	}
	return std::clamp( nSize, mapstyle::GRID_SMALLEST, mapstyle::GRID_LARGEST );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapEditor::CMapEditor( const QString &szPath, bool bFromGame, QWidget *pParent )
	: QWidget( pParent ),
	  m_szPath( szPath ),
	  m_bFromGame( bFromGame )
{
	setObjectName( QStringLiteral( "mapEditor" ) );
	m_pDocument = new CMapDocument( this );
	m_pRenderData = std::make_unique<CMapRenderData>( *m_pDocument );
	m_pResources = new CMapResources( this );
	connect( m_pResources, &CMapResources::Arrived, this, &CMapEditor::ResourcesArrived );
	connect( m_pResources, &CMapResources::Finished, this, &CMapEditor::UpdateGameStatus );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 0 );
	BuildToolBars( pLayout );

	// Hammer's default panes: the camera top left, then top, front and side.
	m_Panes = {
		new CMapPane( *this, MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED ),
		new CMapPane( *this, MapViewType_t::MAP_VIEW_TYPE_TOP ),
		new CMapPane( *this, MapViewType_t::MAP_VIEW_TYPE_FRONT ),
		new CMapPane( *this, MapViewType_t::MAP_VIEW_TYPE_SIDE ),
	};
	m_pSplitter = new CMapQuadSplitter( m_Panes );
	m_pSplitter->setObjectName( QStringLiteral( "mapPanes" ) );
	m_pLoading = new QLabel( tr( "Reading the map…" ), m_pSplitter );
	m_pLoading->setObjectName( QStringLiteral( "mapLoading" ) );
	m_pLoading->setAlignment( Qt::AlignCenter );
	m_pLoading->setStyleSheet( QStringLiteral( "QLabel { color: white; background: rgba(0, 0, 0, 160); padding: 12px; border-radius: 4px; }" ) );

	// Hammer docks the Filter Control at the side and opens Object Properties as a window of
	// its own; here both share the side, so the selection's keys are always in view.
	m_pSidePanel = new QSplitter( Qt::Vertical );
	m_pSidePanel->setObjectName( QStringLiteral( "mapSidePanel" ) );
	m_pSidePanel->setMinimumWidth( 260 );
	m_pSidePanel->setChildrenCollapsible( false );
	auto section = [ this ]( const QString &szTitle, QWidget *pContent ) {
		auto *pBox = new QWidget;
		auto *pBoxLayout = new QVBoxLayout( pBox );
		pBoxLayout->setContentsMargins( 4, 4, 4, 4 );
		auto *pTitle = new QLabel( szTitle );
		QFont font = pTitle->font();
		font.setBold( true );
		pTitle->setFont( font );
		pBoxLayout->addWidget( pTitle );
		pBoxLayout->addWidget( pContent, 1 );
		m_pSidePanel->addWidget( pBox );
	};
	m_pFilter = new CMapFilterControl( *m_pDocument );
	m_pProperties = new CMapProperties( *m_pDocument );
	section( tr( "Filter Control" ), m_pFilter );
	section( tr( "Object Properties" ), m_pProperties );
	m_pSidePanel->setStretchFactor( 0, 2 );
	m_pSidePanel->setStretchFactor( 1, 3 );

	auto *pMiddle = new QSplitter( Qt::Horizontal );
	pMiddle->addWidget( m_pSplitter );
	pMiddle->addWidget( m_pSidePanel );
	pMiddle->setStretchFactor( 0, 1 );
	pMiddle->setStretchFactor( 1, 0 );
	pMiddle->setSizes( { 1000, 280 } );

	auto *pCenter = new QHBoxLayout;
	pCenter->setContentsMargins( 0, 0, 0, 0 );
	pCenter->setSpacing( 0 );
	auto *pTools = new QToolBar;
	pTools->setObjectName( QStringLiteral( "mapTools" ) );
	pTools->setOrientation( Qt::Vertical );
	pTools->setIconSize( QSize{ 24, 24 } );
	m_pTools = new QActionGroup( this );
	const QColor iconColor = palette().buttonText().color();
	const struct
	{
		MapTool_t m_nTool;
		MapIcon_t m_nIcon;
		QString m_szName;
		MapAction_t m_nKey;
	} tools[] = {
		{ MapTool_t::MAP_TOOL_SELECT, MapIcon_t::MAP_ICON_SELECT, tr( "Selection Tool" ), MapAction_t::MAP_ACTION_TOOL_SELECT },
		{ MapTool_t::MAP_TOOL_MAGNIFY, MapIcon_t::MAP_ICON_MAGNIFY, tr( "Magnify" ), MapAction_t::MAP_ACTION_TOOL_MAGNIFY },
		{ MapTool_t::MAP_TOOL_CAMERA, MapIcon_t::MAP_ICON_CAMERA, tr( "Camera" ), MapAction_t::MAP_ACTION_TOOL_CAMERA },
	};
	for ( const auto &tool : tools )
	{
		QAction *pAction = pTools->addAction( MakeIcon( tool.m_nIcon, iconColor ), tool.m_szName );
		pAction->setCheckable( true );
		pAction->setChecked( tool.m_nTool == m_nTool );
		pAction->setToolTip( QStringLiteral( "%1 (%2)" ).arg( tool.m_szName, MapActionKey( tool.m_nKey ).toString( QKeySequence::NativeText ) ) );
		pAction->setData( static_cast<int>( tool.m_nTool ) );
		m_pTools->addAction( pAction );
	}
	connect( m_pTools, &QActionGroup::triggered, this, [ this ]( QAction *pAction ) { SetTool( static_cast<MapTool_t>( pAction->data().toInt() ) ); } );
	pCenter->addWidget( pTools );
	pCenter->addWidget( pMiddle, 1 );
	pLayout->addLayout( pCenter, 1 );

	// Hammer's status bar: what is selected, its size, the cursor, the zoom, the grid.
	auto *pStatus = new QWidget;
	pStatus->setObjectName( QStringLiteral( "mapStatus" ) );
	auto *pStatusLayout = new QHBoxLayout( pStatus );
	pStatusLayout->setContentsMargins( 6, 2, 6, 2 );
	auto label = [ & ]( const char *pszName, int nMinimum ) {
		auto *pLabel = new QLabel;
		pLabel->setObjectName( QString::fromLatin1( pszName ) );
		pLabel->setMinimumWidth( nMinimum );
		pLabel->setFrameShape( QFrame::StyledPanel );
		pLabel->setContentsMargins( 4, 0, 4, 0 );
		return pLabel;
	};
	m_pStatusSelection = label( "mapStatusSelection", 200 );
	m_pStatusSize = label( "mapStatusSize", 140 );
	m_pStatusCursor = label( "mapStatusCursor", 120 );
	m_pStatusZoom = label( "mapStatusZoom", 90 );
	m_pStatusGrid = label( "mapStatusGrid", 160 );
	m_pStatusGame = label( "mapStatusGame", 0 );
	// The two that stretch share what room is left and cut their text short rather than widen
	// the window past the screen; their tool tips hold all of it.
	m_pStatusSelection->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
	m_pStatusGame->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
	pStatusLayout->addWidget( m_pStatusSelection, 1 );
	pStatusLayout->addWidget( m_pStatusGame, 1 );
	pStatusLayout->addWidget( m_pStatusSize );
	pStatusLayout->addWidget( m_pStatusCursor );
	pStatusLayout->addWidget( m_pStatusZoom );
	pStatusLayout->addWidget( m_pStatusGrid );
	pLayout->addWidget( pStatus );

	connect( m_pDocument, &CMapDocument::LoadProgress, this, [ this ]( int nPercent, const QString &szStage ) {
		m_pLoading->setText( tr( "Reading the map… %1% (%2)" ).arg( nPercent ).arg( szStage ) );
		m_pLoading->adjustSize();
		m_pLoading->move( ( m_pSplitter->width() - m_pLoading->width() ) / 2, ( m_pSplitter->height() - m_pLoading->height() ) / 2 );
	} );
	connect( m_pDocument, &CMapDocument::Loaded, this, &CMapEditor::Loaded );
	connect( m_pDocument, &CMapDocument::LoadFailed, this, [ this ]( const QString &szError ) {
		m_pLoading->setText( tr( "This map can't be read:\n%1" ).arg( szError ) );
		m_pLoading->adjustSize();
		m_pLoading->move( ( m_pSplitter->width() - m_pLoading->width() ) / 2, ( m_pSplitter->height() - m_pLoading->height() ) / 2 );
	} );
	auto repaint = [ this ] {
		for ( CMapView *pView : GetViews() )
		{
			if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
			{
				p3D->Redraw();
			}
			pView->update();
		}
		UpdateStatus();
	};
	connect( m_pDocument, &CMapDocument::SelectionChanged, this, repaint );
	connect( m_pDocument, &CMapDocument::VisibilityChanged, this, repaint );
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapEditor::~CMapEditor()
{
	StopWork();
	// The views first, while everything they report to is still here.
	delete m_pSplitter;
	m_pSplitter = nullptr;
	m_Panes = {};
	m_pActiveView = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The bar across the top: Hammer's selection modes and view toggles.
//-----------------------------------------------------------------------------
void CMapEditor::BuildToolBars( QVBoxLayout *pLayout )
{
	auto *pBar = new QToolBar;
	pBar->setObjectName( QStringLiteral( "mapToolBar" ) );
	pBar->setToolButtonStyle( Qt::ToolButtonTextOnly );
	m_pModes = new QActionGroup( this );
	const struct
	{
		MapSelectMode_t m_nMode;
		QString m_szName;
		QString m_szTip;
	} modes[] = {
		{ MapSelectMode_t::MAP_SELECT_MODE_GROUPS, tr( "Groups" ), tr( "Clicking picks whole groups" ) },
		{ MapSelectMode_t::MAP_SELECT_MODE_OBJECTS, tr( "Objects" ), tr( "Clicking picks an entity with its solids, or a world solid" ) },
		{ MapSelectMode_t::MAP_SELECT_MODE_SOLIDS, tr( "Solids" ), tr( "Clicking picks single solids, even inside entities" ) },
	};
	for ( const auto &mode : modes )
	{
		QAction *pAction = pBar->addAction( mode.m_szName );
		pAction->setCheckable( true );
		pAction->setToolTip( mode.m_szTip );
		pAction->setChecked( mode.m_nMode == m_pDocument->GetSelectMode() );
		pAction->setData( static_cast<int>( mode.m_nMode ) );
		pAction->setObjectName( QStringLiteral( "mapSelectMode%1" ).arg( static_cast<int>( mode.m_nMode ) ) );
		m_pModes->addAction( pAction );
	}
	connect( m_pModes, &QActionGroup::triggered, this, [ this ]( QAction *pAction ) { m_pDocument->SetSelectMode( static_cast<MapSelectMode_t>( pAction->data().toInt() ) ); } );
	pBar->addSeparator();
	auto command = [ this, pBar ]( const QString &szName, MapAction_t nAction, bool bCheckable = false, bool bChecked = false ) {
		QAction *pAction = pBar->addAction( szName );
		pAction->setToolTip( QStringLiteral( "%1 (%2)" ).arg( szName, MapActionKey( nAction ).toString( QKeySequence::NativeText ) ) );
		pAction->setCheckable( bCheckable );
		pAction->setChecked( bChecked );
		connect( pAction, &QAction::triggered, this, [ this, nAction ] { RunAction( nAction, m_pActiveView ); } );
		return pAction;
	};
	command( tr( "Smaller Grid" ), MapAction_t::MAP_ACTION_GRID_SMALLER );
	command( tr( "Bigger Grid" ), MapAction_t::MAP_ACTION_GRID_LARGER );
	command( tr( "Grid" ), MapAction_t::MAP_ACTION_TOGGLE_GRID, true, m_Settings.m_bShowGrid )->setObjectName( QStringLiteral( "mapToggleGrid" ) );
	pBar->addSeparator();
	command( tr( "Models in 2D" ), MapAction_t::MAP_ACTION_TOGGLE_MODELS_2D, true, m_Settings.m_bModels2D );
	command( tr( "Entity Names" ), MapAction_t::MAP_ACTION_TOGGLE_NAMES, true, m_Settings.m_bNames );
	command( tr( "Helpers" ), MapAction_t::MAP_ACTION_TOGGLE_HELPERS, true, m_Settings.m_bHelpers );
	command( tr( "Tool Textures" ), MapAction_t::MAP_ACTION_TOGGLE_TOOL_TEXTURES, true, m_Settings.m_bToolTextures );
	command( tr( "3D Skybox" ), MapAction_t::MAP_ACTION_TOGGLE_SKYBOX_3D, true, m_Settings.m_bSkybox3D );
	pBar->addSeparator();
	command( tr( "Center on Selection" ), MapAction_t::MAP_ACTION_CENTER_2D );
	command( tr( "Go to Brush…" ), MapAction_t::MAP_ACTION_GO_TO_BRUSH );
	command( tr( "Autosize Views" ), MapAction_t::MAP_ACTION_AUTOSIZE );
	pLayout->addWidget( pBar );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::Load( CMapDocument::Source_t source )
{
	m_pLoading->show();
	m_pLoading->raise();
	m_pDocument->Load( std::move( source ) );
}

//-----------------------------------------------------------------------------
// Purpose: The map is built: the views draw it and the grid takes the map's own settings.
//-----------------------------------------------------------------------------
void CMapEditor::Loaded()
{
	const xv::map::CScene &scene = *m_pDocument->GetScene();
	m_Settings.m_nGridSize = GridSizeFor( scene.m_Map.m_ViewSettings.m_nGridSpacing );
	m_Settings.m_bShowGrid = scene.m_Map.m_ViewSettings.m_bShowGrid;
	m_Settings.m_bSnapToGrid = scene.m_Map.m_ViewSettings.m_bSnapToGrid;
	m_pLoading->hide();
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
		{
			p2D->Rebuild();
			p2D->Fit();
		}
		else if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->ResetCamera();
		}
		pView->update();
	}
	if ( auto *pGrid = findChild<QAction *>( QStringLiteral( "mapToggleGrid" ) ) )
	{
		pGrid->setChecked( m_Settings.m_bShowGrid );
	}
	m_pResources->Start( m_pDocument->GetSharedScene(), m_pDocument->GetGame(), 1024 );
	UpdateGameStatus();
	std::size_t nErrors = 0;
	for ( const xv::map::Diagnostic_t &diagnostic : scene.m_Map.m_Diagnostics )
	{
		nErrors += diagnostic.m_nSeverity == xv::map::Diagnostic_t::Severity_t::SEVERITY_ERROR ? 1 : 0;
	}
	emit StatusMessage( tr( "%1: %2 solids, %3 entities%4" ).arg( GetTabTitle() ).arg( scene.m_Map.m_Solids.size() ).arg( scene.m_Map.m_Entities.size() ).arg( nErrors ? tr( ", %n problem(s) reading it", nullptr, static_cast<int>( nErrors ) ) : QString{} ) );
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetTool( MapTool_t nTool )
{
	m_nTool = nTool;
	for ( QAction *pAction : m_pTools->actions() )
	{
		pAction->setChecked( pAction->data().toInt() == static_cast<int>( nTool ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapPane *CMapEditor::GetPane( int nIndex ) const
{
	return m_Panes[ static_cast<std::size_t>( nIndex ) ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<CMapView *> CMapEditor::GetViews() const
{
	std::vector<CMapView *> views;
	for ( CMapPane *pPane : m_Panes )
	{
		if ( pPane && pPane->GetView() )
		{
			views.push_back( pPane->GetView() );
		}
	}
	return views;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapPane *CMapEditor::PaneOf( const CMapView *pView ) const
{
	for ( CMapPane *pPane : m_Panes )
	{
		if ( pPane->GetView() == pView )
		{
			return pPane;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SetActiveView( CMapView *pView )
{
	m_pActiveView = pView;
	if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
	{
		ShowZoom( p2D->GetZoom() );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ViewTypeChanged( CMapView *pView )
{
	if ( CMapPane *pPane = PaneOf( pView ) )
	{
		pPane->UpdateCaption();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ShowCursor( const QString &szAt )
{
	m_pStatusCursor->setText( szAt );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::ShowZoom( double flZoom )
{
	m_pStatusZoom->setText( tr( "Zoom: %1" ).arg( flZoom, 0, 'f', flZoom < 1.0 ? 3 : 2 ) );
}

//-----------------------------------------------------------------------------
// Purpose: The selection as Hammer's status bar puts it, and its size.
//-----------------------------------------------------------------------------
void CMapEditor::UpdateStatus()
{
	m_pStatusGrid->setText( tr( "Snap: %1 Grid: %2" ).arg( m_Settings.m_bSnapToGrid ? tr( "On" ) : tr( "Off" ) ).arg( m_Settings.m_nGridSize ) );
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	if ( !pScene || selection.empty() )
	{
		m_pStatusSelection->clear();
		m_pStatusSize->clear();
		return;
	}
	// One object, or one entity with its solids, is named; more are counted.
	std::vector<int> entities;
	bool bWorld = false;
	for ( const std::uint32_t n : selection )
	{
		const int nEntity = pScene->GetObjectEntity( n );
		if ( nEntity < 0 )
		{
			bWorld = true;
		}
		else if ( std::ranges::find( entities, nEntity ) == entities.end() )
		{
			entities.push_back( nEntity );
		}
	}
	if ( !bWorld && entities.size() == 1 )
	{
		const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ static_cast<std::size_t>( entities.front() ) ];
		const std::string_view szClass = entity.GetClassname();
		const std::string_view szName = entity.GetValue( "targetname" );
		QString szText = QString::fromUtf8( szClass.data(), static_cast<qsizetype>( szClass.size() ) );
		if ( !szName.empty() )
		{
			szText += QStringLiteral( " - " ) + QString::fromUtf8( szName.data(), static_cast<qsizetype>( szName.size() ) );
		}
		m_pStatusSelection->setText( szText );
	}
	else if ( bWorld && entities.empty() && selection.size() == 1 )
	{
		m_pStatusSelection->setText( tr( "solid (id %1)" ).arg( pScene->GetObjectId( selection.front() ) ) );
	}
	else
	{
		m_pStatusSelection->setText( tr( "%n object(s) selected", nullptr, static_cast<int>( selection.size() ) ) );
	}
	m_pStatusSelection->setToolTip( m_pStatusSelection->text() );
	const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
	if ( !bounds.IsEmpty() )
	{
		const xv::map::Point_t size = bounds.GetSize();
		m_pStatusSize->setText( QStringLiteral( "%1w %2l %3h" ).arg( std::lround( size[ 0 ] ) ).arg( std::lround( size[ 1 ] ) ).arg( std::lround( size[ 2 ] ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::SettingsChanged()
{
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->Redraw();
		}
		pView->update();
	}
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's Go to Brush Number: selects the solid (or entity) with that id.
//-----------------------------------------------------------------------------
void CMapEditor::GoToBrush()
{
	const xv::map::CScene *pScene = m_pDocument->GetScene();
	if ( !pScene )
	{
		return;
	}
	bool bOk = false;
	const int nId = QInputDialog::getInt( this, tr( "Go to Brush Number" ), tr( "Solid or entity id:" ), 0, 0, std::numeric_limits<int>::max(), 1, &bOk );
	if ( !bOk )
	{
		return;
	}
	for ( std::uint32_t n = 0; n < pScene->GetObjectCount(); n++ )
	{
		if ( pScene->GetObjectId( n ) == nId )
		{
			m_pDocument->SetSelection( m_pDocument->Expand( n ) );
			RunAction( MapAction_t::MAP_ACTION_CENTER_2D, m_pActiveView );
			RunAction( MapAction_t::MAP_ACTION_CENTER_3D, m_pActiveView );
			return;
		}
	}
	emit StatusMessage( tr( "No solid or entity has the id %1." ).arg( nId ) );
}

//-----------------------------------------------------------------------------
// Purpose: The commands that act on more than one view.
//-----------------------------------------------------------------------------
bool CMapEditor::RunAction( MapAction_t nAction, CMapView *pFrom )
{
	CMapPane *pPane = PaneOf( pFrom );
	auto switchTo = [ & ]( MapViewType_t nType ) {
		if ( pPane )
		{
			pPane->SetType( nType );
			pPane->GetView()->setFocus();
		}
		return true;
	};
	auto toggle = [ this ]( bool &bSetting ) {
		bSetting = !bSetting;
		SettingsChanged();
		return true;
	};
	const std::vector<std::uint32_t> &selection = m_pDocument->GetSelection();
	switch ( nAction )
	{
		case MapAction_t::MAP_ACTION_VIEW_TOP: return switchTo( MapViewType_t::MAP_VIEW_TYPE_TOP );
		case MapAction_t::MAP_ACTION_VIEW_SIDE: return switchTo( MapViewType_t::MAP_VIEW_TYPE_SIDE );
		case MapAction_t::MAP_ACTION_VIEW_FRONT: return switchTo( MapViewType_t::MAP_VIEW_TYPE_FRONT );
		case MapAction_t::MAP_ACTION_VIEW_TEXTURED: return switchTo( MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED );
		case MapAction_t::MAP_ACTION_VIEW_TEXTURED_SHADED: return switchTo( MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED_SHADED );
		case MapAction_t::MAP_ACTION_NEXT_PANE:
		case MapAction_t::MAP_ACTION_PREVIOUS_PANE:
		{
			const int nFrom = pPane ? m_pSplitter->IndexOf( pPane ) : 0;
			const int nTo = ( nFrom + ( nAction == MapAction_t::MAP_ACTION_NEXT_PANE ? 1 : 3 ) ) % 4;
			if ( m_pSplitter->GetMaximized() >= 0 )
			{
				m_pSplitter->SetMaximized( nTo );
			}
			m_Panes[ static_cast<std::size_t>( nTo ) ]->GetView()->setFocus();
			return true;
		}
		case MapAction_t::MAP_ACTION_GRID_SMALLER:
			m_Settings.m_nGridSize = std::max( mapstyle::GRID_SMALLEST, m_Settings.m_nGridSize / 2 );
			SettingsChanged();
			return true;
		case MapAction_t::MAP_ACTION_GRID_LARGER:
			m_Settings.m_nGridSize = std::min( mapstyle::GRID_LARGEST, m_Settings.m_nGridSize * 2 );
			SettingsChanged();
			return true;
		case MapAction_t::MAP_ACTION_TOGGLE_GRID:
			toggle( m_Settings.m_bShowGrid );
			if ( auto *pGrid = findChild<QAction *>( QStringLiteral( "mapToggleGrid" ) ) )
			{
				pGrid->setChecked( m_Settings.m_bShowGrid );
			}
			return true;
		case MapAction_t::MAP_ACTION_MAXIMIZE:
			m_pSplitter->SetMaximized( m_pSplitter->GetMaximized() >= 0 || !pPane ? -1 : m_pSplitter->IndexOf( pPane ) );
			if ( pFrom )
			{
				pFrom->setFocus();
			}
			return true;
		case MapAction_t::MAP_ACTION_AUTOSIZE:
			m_pSplitter->Autosize();
			return true;
		case MapAction_t::MAP_ACTION_CENTER_2D:
		case MapAction_t::MAP_ACTION_CENTER_3D:
		{
			const xv::map::Box_t bounds = m_pDocument->GetSelectionBounds();
			for ( CMapView *pView : GetViews() )
			{
				if ( Is2DView( pView->GetType() ) == ( nAction == MapAction_t::MAP_ACTION_CENTER_2D ) )
				{
					pView->CenterOn( bounds );
				}
			}
			return true;
		}
		case MapAction_t::MAP_ACTION_CLEAR_SELECTION:
		case MapAction_t::MAP_ACTION_ESCAPE:
			m_pDocument->ClearSelection();
			return true;
		case MapAction_t::MAP_ACTION_QUICK_HIDE:
			m_pDocument->QuickHide( selection );
			return true;
		case MapAction_t::MAP_ACTION_QUICK_HIDE_OTHERS:
			m_pDocument->QuickHideAllBut( selection );
			return true;
		case MapAction_t::MAP_ACTION_QUICK_UNHIDE:
			m_pDocument->QuickUnhideAll();
			return true;
		case MapAction_t::MAP_ACTION_TOGGLE_HELPERS: return toggle( m_Settings.m_bHelpers );
		case MapAction_t::MAP_ACTION_TOGGLE_MODELS_2D: return toggle( m_Settings.m_bModels2D );
		case MapAction_t::MAP_ACTION_TOGGLE_NAMES: return toggle( m_Settings.m_bNames );
		case MapAction_t::MAP_ACTION_TOGGLE_TOOL_TEXTURES: return toggle( m_Settings.m_bToolTextures );
		case MapAction_t::MAP_ACTION_TOGGLE_SKYBOX_3D: return toggle( m_Settings.m_bSkybox3D );
		case MapAction_t::MAP_ACTION_GO_TO_BRUSH:
			GoToBrush();
			return true;
		case MapAction_t::MAP_ACTION_PROPERTIES:
			ShowProperties();
			return true;
		case MapAction_t::MAP_ACTION_TOOL_SELECT:
			SetTool( MapTool_t::MAP_TOOL_SELECT );
			return true;
		case MapAction_t::MAP_ACTION_TOOL_MAGNIFY:
			SetTool( MapTool_t::MAP_TOOL_MAGNIFY );
			return true;
		case MapAction_t::MAP_ACTION_TOOL_CAMERA:
			SetTool( MapTool_t::MAP_TOOL_CAMERA );
			return true;
		case MapAction_t::MAP_ACTION_ZOOM_IN_ALL:
		case MapAction_t::MAP_ACTION_ZOOM_OUT_ALL:
			for ( CMapView *pView : GetViews() )
			{
				if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
				{
					p2D->Zoom( nAction == MapAction_t::MAP_ACTION_ZOOM_IN_ALL ? mapstyle::KEY_ZOOM_STEP : 1.0 / mapstyle::KEY_ZOOM_STEP );
				}
			}
			return true;
		default:
			return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapEditor::GetTabTitle() const
{
	return QFileInfo{ m_szPath }.fileName();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapEditor::GetSessionEntry() const
{
	return QStringLiteral( "map\t%1\t%2" ).arg( m_bFromGame ? QStringLiteral( "game" ) : QStringLiteral( "disk" ), m_szPath );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::StopWork()
{
	m_pDocument->Stop();
	m_pResources->Stop();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapEditor::GameChanged( const xv::vfs::CGameInfo *pGame )
{
	m_pDocument->SetGame( pGame ? std::optional<xv::vfs::CGameInfo>{ *pGame } : std::nullopt );
	if ( m_pDocument->IsLoaded() )
	{
		m_pResources->Start( m_pDocument->GetSharedScene(), m_pDocument->GetGame(), 1024 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: More textures: the 3D views draw with them.
//-----------------------------------------------------------------------------
void CMapEditor::ResourcesArrived()
{
	m_pRenderData->SetMaterials( m_pResources->GetMaterials(), m_pResources->GetTextures() );
	m_pRenderData->SetSky( m_pResources->GetSky() );
	m_pRenderData->SetProps( m_pResources->GetProps(), m_pResources->GetPropObjects() );
	m_pRenderData->SetSprites( m_pResources->GetSprites() );
	for ( CMapView *pView : GetViews() )
	{
		if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->Redraw();
		}
		else
		{
			// The props' models, once they come.
			pView->update();
		}
	}
	UpdateGameStatus();
}

//-----------------------------------------------------------------------------
// Purpose: Where entities' looks come from, and how far the textures have got.
//-----------------------------------------------------------------------------
void CMapEditor::UpdateGameStatus()
{
	const QString szFgd = m_pDocument->GetFgdSource();
	QString szText = szFgd.isEmpty() ? tr( "No game data: entities show as boxes" ) : tr( "Entities from %1" ).arg( szFgd );
	if ( !m_pDocument->GetGame() )
	{
		szText += tr( "; open the game to see textures" );
	}
	else if ( !m_pResources->IsFinished() )
	{
		szText += tr( "; textures %1 of %2" ).arg( m_pResources->GetLoaded() ).arg( m_pResources->GetTotal() );
	}
	else if ( m_pResources->GetModelsLoaded() < m_pResources->GetModelsTotal() )
	{
		// The others show as boxes.
		szText += tr( "; %1 of %2 models read" ).arg( m_pResources->GetModelsLoaded() ).arg( m_pResources->GetModelsTotal() );
	}
	m_pStatusGame->setText( szText );
	m_pStatusGame->setToolTip( szText );
}

//-----------------------------------------------------------------------------
// Purpose: Alt+Enter: Object Properties, brought out if the side was dragged shut.
//-----------------------------------------------------------------------------
void CMapEditor::ShowProperties()
{
	if ( auto *pMiddle = qobject_cast<QSplitter *>( m_pSidePanel->parentWidget() ) )
	{
		QList<int> sizes = pMiddle->sizes();
		if ( sizes.size() == 2 && sizes[ 1 ] == 0 )
		{
			const int nSide = std::max( m_pSidePanel->minimumWidth(), 280 );
			pMiddle->setSizes( { std::max( sizes[ 0 ] - nSide, 0 ), nSide } );
		}
	}
	m_pProperties->FocusKeys();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::map::CViewCamera> CMapEditor::GetCamera() const
{
	for ( CMapView *pView : GetViews() )
	{
		if ( const auto *p3D = qobject_cast<const CMapView3D *>( pView ) )
		{
			return p3D->GetCamera();
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The 2D views draw the camera, so they follow it.
//-----------------------------------------------------------------------------
void CMapEditor::CameraMoved( CMapView *pView )
{
	for ( CMapView *pOther : GetViews() )
	{
		if ( pOther != pView && Is2DView( pOther->GetType() ) )
		{
			pOther->update();
		}
	}
}
