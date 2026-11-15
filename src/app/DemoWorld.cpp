//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoWorld.h.
//
//===========================================================================//
#include "DemoWorld.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>

#include "DemoViewer.h"
#include "GpuView.h"
#include "Graphics.h"
#include "ImageFiles.h"
#include "Preferences.h"
#include "bsp/Decompile.h"
#include "map/MapDocument.h"
#include "map/MapRenderer.h"
#include "map/MapResources.h"
#include "map/Raster.h"

namespace
{

using xv::demo::PlayerState_t;
using xv::map::Point_t;

// A player's figure: Source's hull is 32 wide and 72 tall (54 ducked); the figure is narrower
// front to back, so which way it faces shows, with a band at its eyes that sticks out ahead.
constexpr double FIGURE_HALF_WIDTH = 14.0;
constexpr double FIGURE_HALF_DEPTH = 9.0;
constexpr double FIGURE_HEIGHT = 72.0;
constexpr double FIGURE_DUCKED_HEIGHT = 54.0;
constexpr double VISOR_HALF_HEIGHT = 4.0;
constexpr double VISOR_REACH = 5.0;
constexpr double FLY_SPEED = 600.0;  // units a second
constexpr double FLY_FAST = 4.0;     // with Shift
constexpr double LOOK_SPEED = 0.25;  // degrees a pixel
constexpr double WHEEL_STEP = 96.0;  // units a notch
constexpr double CHASE_NEAREST = 48.0;
constexpr double CHASE_FURTHEST = 1024.0;
constexpr double CHASE_RISE = 24.0;  // above the eyes
constexpr double CLICK_SLACK = 4.0;  // pixels a press may move and still be a click
// Names aren't written for players further than this: a crowd across the map is a smear.
constexpr double NAME_DISTANCE = 4096.0;
constexpr double NAME_RISE = 10.0;   // over the figure's top
// The first look at the players: from this far back and up, per unit of their spread.
constexpr double START_BACK = 1.2;
constexpr double START_NEAREST = 400.0;
constexpr int FLY_INTERVAL = 16;

//-----------------------------------------------------------------------------
// Purpose: How tall a player's figure stands.
//-----------------------------------------------------------------------------
double FigureHeight( const PlayerState_t &player )
{
	return player.m_bDucked ? FIGURE_DUCKED_HEIGHT : FIGURE_HEIGHT;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a player has a figure: alive, and on a team that plays.
//-----------------------------------------------------------------------------
bool HasFigure( const PlayerState_t &player )
{
	return player.m_bAlive && player.m_nTeam >= 2;
}

//-----------------------------------------------------------------------------
// Purpose: A box's eight corners, turned about the vertical by a yaw: forward along the yaw,
//          from flBack to flFront; flHalfWidth either side; flBottom to flTop above the feet.
//          Corner i has its high side along forward, right and up for bits 0, 1 and 2.
//-----------------------------------------------------------------------------
std::array<Point_t, 8> TurnedBox( const xv::CVec3 &vecFeet, double flYaw, double flBack, double flFront, double flHalfWidth, double flBottom, double flTop )
{
	const double flRadians = flYaw * std::numbers::pi / 180.0;
	const Point_t forward{ std::cos( flRadians ), std::sin( flRadians ), 0.0 };
	const Point_t right{ forward[ 1 ], -forward[ 0 ], 0.0 };
	std::array<Point_t, 8> corners{};
	for ( std::size_t i = 0; i < 8; i++ )
	{
		const double flAlong = ( i & 1 ) ? flFront : flBack;
		const double flAcross = ( i & 2 ) ? flHalfWidth : -flHalfWidth;
		const double flUp = ( i & 4 ) ? flTop : flBottom;
		corners[ i ] = { vecFeet.m_flX + forward[ 0 ] * flAlong + right[ 0 ] * flAcross, vecFeet.m_flY + forward[ 1 ] * flAlong + right[ 1 ] * flAcross, vecFeet.m_flZ + flUp };
	}
	return corners;
}

// A turned box's faces, as corners (TurnedBox's numbering), with how bright each is drawn: the
// top lightest, the bottom darkest, the front lighter than the back, so the shape reads without
// any lighting.
struct BoxFace_t
{
	std::array<int, 4> m_Corners;
	float m_flShade;
};
constexpr std::array<BoxFace_t, 6> BOX_FACES{ {
	{ { 4, 5, 7, 6 }, 1.0f },  // top
	{ { 0, 2, 3, 1 }, 0.45f }, // bottom
	{ { 1, 3, 7, 5 }, 0.9f },  // front
	{ { 0, 4, 6, 2 }, 0.6f },  // back
	{ { 2, 6, 7, 3 }, 0.75f }, // right
	{ { 0, 1, 5, 4 }, 0.7f },  // left
} };
constexpr std::array<std::array<int, 2>, 12> BOX_EDGES{ { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } } };

//-----------------------------------------------------------------------------
// Purpose: A figure's two boxes: the body, and the band at its eyes.
//-----------------------------------------------------------------------------
std::array<std::array<Point_t, 8>, 2> FigureBoxes( const PlayerState_t &player )
{
	const double flHeight = FigureHeight( player );
	const double flEyes = player.m_bDucked ? xv::demo::PLAYER_DUCKED_EYE_HEIGHT : xv::demo::PLAYER_EYE_HEIGHT;
	return { TurnedBox( player.m_vecOrigin, player.m_flYaw, -FIGURE_HALF_DEPTH, FIGURE_HALF_DEPTH, FIGURE_HALF_WIDTH, 0.0, flHeight ),
		TurnedBox( player.m_vecOrigin, player.m_flYaw, FIGURE_HALF_DEPTH, FIGURE_HALF_DEPTH + VISOR_REACH, FIGURE_HALF_WIDTH - 2.0, flEyes - VISOR_HALF_HEIGHT, flEyes + VISOR_HALF_HEIGHT ) };
}

//-----------------------------------------------------------------------------
// Purpose: The recorder's camera at a tick and fractions of one: the packet's at or before
//          it, moved towards the next (unless that one is somewhere else altogether).
//-----------------------------------------------------------------------------
std::optional<xv::demo::View_t> RecorderView( const xv::demo::CDemo &demo, double flTick )
{
	const std::vector<xv::demo::View_t> &views = demo.GetViews();
	const auto after = std::upper_bound( views.begin(), views.end(), flTick, []( double flAt, const xv::demo::View_t &view ) { return flAt < view.m_nTick; } );
	if ( after == views.begin() )
	{
		return views.empty() ? std::nullopt : std::optional{ views.front() };
	}
	xv::demo::View_t view = *( after - 1 );
	if ( after != views.end() && after->m_nTick > view.m_nTick && Length( after->m_vecOrigin - view.m_vecOrigin ) < 256.0f )
	{
		const float flPart = static_cast<float>( ( flTick - view.m_nTick ) / ( after->m_nTick - view.m_nTick ) );
		view.m_vecOrigin = view.m_vecOrigin + ( after->m_vecOrigin - view.m_vecOrigin ) * flPart;
		view.m_vecAngles.m_flX += std::remainder( after->m_vecAngles.m_flX - view.m_vecAngles.m_flX, 360.0f ) * flPart;
		view.m_vecAngles.m_flY += std::remainder( after->m_vecAngles.m_flY - view.m_vecAngles.m_flY, 360.0f ) * flPart;
	}
	return view;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Counter-Strike's two sides are everyone's picture of 2 and 3; any other team is
//          grey.
//-----------------------------------------------------------------------------
QColor DemoTeamColor( int nTeam )
{
	switch ( nTeam )
	{
		case 2:
			return QColor( 232, 104, 64 );
		case 3:
			return QColor( 88, 152, 240 );
		default:
			return QColor( 176, 176, 176 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoWorldOverlay::CDemoWorldOverlay( CDemoWorld &world )
	: QWidget( &world ),
	  m_World( world )
{
	setAttribute( Qt::WA_TransparentForMouseEvents );
	setFocusPolicy( Qt::NoFocus );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorldOverlay::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	m_World.PaintLabels( painter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoWorld::CDemoWorld( CDemoViewer &viewer, QWidget *pParent )
	: QWidget( pParent ),
	  m_Viewer( viewer )
{
	setObjectName( QStringLiteral( "demoWorld" ) );
	setFocusPolicy( Qt::ClickFocus );
	setMinimumSize( 320, 200 );
	m_pDocument = new CMapDocument( this );
	m_pRenderData = std::make_unique<CMapRenderData>( *m_pDocument );
	m_pResources = new CMapResources( this );
	m_pRenderData->SetResources( m_pResources );
	connect( m_pDocument, &CMapDocument::Loaded, this, &CDemoWorld::MapLoaded );
	connect( m_pDocument, &CMapDocument::LoadFailed, this, [ this ]( const QString &szError ) { SetStatus( tr( "The map couldn't be read: %1" ).arg( szError ) ); } );
	connect( m_pDocument, &CMapDocument::LoadProgress, this, [ this ]( int nPercent, const QString &szStage ) { SetStatus( tr( "Reading the map: %1 (%2%)" ).arg( szStage ).arg( nPercent ) ); } );
	connect( m_pResources, &CMapResources::Arrived, this, &CDemoWorld::ResourcesArrived );
	connect( m_pResources, &CMapResources::Finished, this, [ this ] {
		m_pRenderData->CompactEntityBoxes();
		SetStatus( {} );
	} );
	m_pFly = new QTimer( this );
	m_pFly->setInterval( FLY_INTERVAL );
	connect( m_pFly, &QTimer::timeout, this, &CDemoWorld::Fly );
	m_Camera.m_flYaw = 0.0;
	m_nMode = m_Viewer.GetDemo().HasCamera() ? DemoCamera_t::DEMO_CAMERA_RECORDER : DemoCamera_t::DEMO_CAMERA_FREE;
	m_Worker = std::thread( [ this ] { WorkerLoop(); } );
	SetUseGpu( CGraphics::Instance().IsGpu() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoWorld::~CDemoWorld()
{
	Stop();
	{
		const std::lock_guard lock( m_Mutex );
		m_bQuit = true;
	}
	m_Wake.notify_all();
	m_Worker.join();
}

//-----------------------------------------------------------------------------
// Purpose: The card draws in a widget of its own under the names' overlay; a card that fails
//          leaves the drawing to software from then on.
//-----------------------------------------------------------------------------
void CDemoWorld::SetUseGpu( bool bOn )
{
	delete m_pGpu;
	m_pGpu = nullptr;
	delete m_pOverlay;
	m_pOverlay = nullptr;
#ifdef VOMIT_TOOLS_GPU
	if ( bOn )
	{
		m_pGpu = new CGpuView( std::make_unique<CMapRenderer>( [ this ]( QSize size ) { return MakeFrame( size ); } ), this );
		m_pGpu->setAttribute( Qt::WA_TransparentForMouseEvents );
		m_pGpu->setFocusPolicy( Qt::NoFocus );
		m_pGpu->setGeometry( rect() );
		m_pGpu->show();
		connect( m_pGpu, &QRhiWidget::renderFailed, this, [ this ] { QTimer::singleShot( 0, this, [ this ] {
																		 SetUseGpu( false );
																		 // Software draws from decoded textures, which the card didn't need.
																		 if ( HasMap() && !m_pResources->IsDecoded() )
																			 MapLoaded();
																	 } ); } );
		m_pOverlay = new CDemoWorldOverlay( *this );
		m_pOverlay->setGeometry( rect() );
		m_pOverlay->show();
		m_pOverlay->raise();
		return;
	}
#else
	(void)bOn;
#endif
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetStatus( const QString &szStatus )
{
	if ( szStatus == m_szStatus )
	{
		return;
	}
	m_szStatus = szStatus;
	emit StatusChanged();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CDemoWorld::HasMap() const
{
	return m_pDocument->IsLoaded();
}

//-----------------------------------------------------------------------------
// Purpose: Reading a big map back takes seconds: it is done on a thread, and the text then
//          loaded as any map's. The map's packed files are mounted first, as the game mounts
//          them.
//-----------------------------------------------------------------------------
void CDemoWorld::LoadCompiledMap( std::vector<std::byte> bsp, std::optional<xv::vfs::CGameInfo> game, const QString &szName, const std::filesystem::path &disk )
{
	Stop();
	SetStatus( tr( "Reading %1…" ).arg( szName ) );
	if ( game && !disk.empty() )
	{
		game->m_SearchPaths.insert( game->m_SearchPaths.begin(), { { "game" }, disk.generic_string() } );
	}
	m_Game = game;
	m_pLoad = std::make_shared<int>( 0 );
	const std::weak_ptr<int> pLoad = m_pLoad;
	m_Decompiler = std::thread( [ this, pLoad, bsp = std::move( bsp ), game = std::move( game ), szName ]() mutable {
		std::string szError;
		auto text = xv::bsp::DecompileToVmf( bsp, nullptr, &szError );
		bsp = {};
		QMetaObject::invokeMethod(
			this, [ this, pLoad, text = std::move( text ), game = std::move( game ), szName, szError ]() mutable {
				if ( pLoad.expired() )
				{
					return;
				}
				if ( !text )
				{
					SetStatus( tr( "%1 couldn't be read: %2" ).arg( szName, QString::fromStdString( szError ) ) );
					return;
				}
				CMapDocument::Source_t source;
				source.m_szName = szName;
				source.m_Text.assign( text->begin(), text->end() );
				source.m_Game = std::move( game );
				for ( const QString &szFgd : CPreferences::Instance().GetMapPreferences().m_Fgds )
				{
					source.m_Fgds.push_back( std::filesystem::path( szFgd.toStdU16String() ) );
				}
				m_pDocument->Load( std::move( source ) );
			},
			Qt::QueuedConnection );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetNoMap( const QString &szWhy )
{
	Stop();
	SetStatus( szWhy );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::Stop()
{
	m_pLoad.reset();
	if ( m_Decompiler.joinable() )
	{
		m_Decompiler.join();
	}
	m_pDocument->Stop();
	m_pResources->Stop();
}

//-----------------------------------------------------------------------------
// Purpose: The map is in: its textures and models start arriving (decoded only when software
//          draws them; the card takes them as they are).
//-----------------------------------------------------------------------------
void CDemoWorld::MapLoaded()
{
	SetStatus( tr( "Reading the map's textures…" ) );
	const bool bDecoded = !m_pGpu || qEnvironmentVariableIsSet( "VOMIT_TOOLS_DECODE_TEXTURES" );
	m_pResources->Start( m_pDocument->GetSharedScene(), m_pDocument->GetGame(), CPreferences::Instance().GetMapPreferences().m_nTextureSize, bDecoded );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::ResourcesArrived()
{
	m_pRenderData->SetMaterials( m_pResources->GetMaterials(), m_pResources->GetTextures(), m_pResources->GetEntityMaterial() );
	m_pRenderData->SetSky( m_pResources->GetSky(), m_pResources->GetSkyTransforms() );
	m_pRenderData->SetEnvMap( m_pResources->GetEnvMap() );
	m_pRenderData->SetProps( m_pResources->GetProps(), m_pResources->GetPropObjects(), m_pResources->GetPropParts() );
	m_pRenderData->SetSprites( m_pResources->GetSprites() );
	m_pRenderData->SetOverlays( m_pResources->GetOverlays(), m_pResources->GetOverlayParts() );
	if ( !m_pResources->IsFinished() )
	{
		SetStatus( tr( "Reading the map's textures: %1 of %2" ).arg( m_pResources->GetLoaded() ).arg( m_pResources->GetTotal() ) );
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetCameraMode( DemoCamera_t nMode )
{
	if ( nMode == DemoCamera_t::DEMO_CAMERA_RECORDER && !m_Viewer.GetDemo().HasCamera() )
	{
		nMode = DemoCamera_t::DEMO_CAMERA_FREE;
	}
	if ( nMode == m_nMode )
	{
		return;
	}
	// The free camera carries on from where the last one looked.
	m_Camera = GetCamera();
	m_bPlaced = true;
	m_nMode = nMode;
	emit CameraModeChanged();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetFollowed( int nEntity )
{
	if ( nEntity == m_nFollowed )
	{
		return;
	}
	m_nFollowed = nEntity;
	emit FollowedChanged( nEntity );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingNames( bool bNames )
{
	m_bNames = bNames;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetSeeingThroughWalls( bool bThrough )
{
	m_bThroughWalls = bThrough;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetCamera( const xv::map::CViewCamera &camera )
{
	m_Camera = camera;
	m_bPlaced = true;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::LookAtPlayers()
{
	SetCameraMode( DemoCamera_t::DEMO_CAMERA_FREE );
	PlaceCamera();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: The first look: behind and above the middle of the players there at the playhead
//          (or the first tick that has any), facing them.
//-----------------------------------------------------------------------------
void CDemoWorld::PlaceCamera()
{
	std::vector<PlayerState_t> players = GetPlayersShown();
	if ( players.empty() )
	{
		for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
		{
			const auto first = std::ranges::find_if( track.m_Samples, []( const xv::demo::PlayerSample_t &sample ) { return sample.m_bThere && sample.m_bAlive && sample.m_nTeam >= 2; } );
			if ( first != track.m_Samples.end() )
			{
				PlayerState_t state;
				state.m_vecOrigin = first->m_vecOrigin;
				players.push_back( state );
			}
		}
	}
	if ( players.empty() )
	{
		// No players to look at: the middle of the map, once there is one.
		if ( const xv::map::CScene *pScene = m_pDocument->GetScene(); pScene && !pScene->m_Bounds.IsEmpty() )
		{
			const Point_t centre = pScene->m_Bounds.GetCenter();
			m_Camera.LookAt( { centre[ 0 ] - 512.0, centre[ 1 ] - 512.0, centre[ 2 ] + 384.0 }, centre );
			m_bPlaced = true;
		}
		return;
	}
	xv::map::Box_t box;
	for ( const PlayerState_t &player : players )
	{
		box.Add( xv::map::ToPoint( player.m_vecOrigin ) );
	}
	const Point_t centre = box.GetCenter();
	const Point_t size = box.GetSize();
	const double flBack = std::max( START_NEAREST, std::min( std::hypot( size[ 0 ], size[ 1 ] ), 2048.0 ) * START_BACK );
	m_Camera.LookAt( { centre[ 0 ] - flBack * 0.7, centre[ 1 ] - flBack * 0.7, centre[ 2 ] + flBack * 0.6 }, { centre[ 0 ], centre[ 1 ], centre[ 2 ] + 36.0 } );
	m_bPlaced = true;
}

//-----------------------------------------------------------------------------
// Purpose: Where this frame is seen from. A following camera whose player isn't there at the
//          playhead (dead, gone, out of the recorder's sight) stays where it last was.
//-----------------------------------------------------------------------------
xv::map::CViewCamera CDemoWorld::GetCamera()
{
	const double flTick = m_Viewer.GetPlayTick();
	switch ( m_nMode )
	{
		case DemoCamera_t::DEMO_CAMERA_RECORDER:
			if ( const auto view = RecorderView( m_Viewer.GetDemo(), flTick ) )
			{
				m_Camera.m_Eye = xv::map::ToPoint( view->m_vecOrigin );
				m_Camera.m_flPitch = view->m_vecAngles.m_flX;
				m_Camera.m_flYaw = view->m_vecAngles.m_flY;
				m_bPlaced = true;
			}
			break;
		case DemoCamera_t::DEMO_CAMERA_FIRST_PERSON:
		case DemoCamera_t::DEMO_CAMERA_CHASE:
			if ( const auto player = m_Viewer.GetPlayers().At( m_nFollowed, flTick ) )
			{
				m_Camera.m_Eye = xv::map::ToPoint( player->GetEye() );
				m_Camera.m_flPitch = player->m_flPitch;
				m_Camera.m_flYaw = player->m_flYaw;
				if ( m_nMode == DemoCamera_t::DEMO_CAMERA_CHASE )
				{
					const Point_t forward = m_Camera.GetForward();
					m_Camera.m_Eye = { m_Camera.m_Eye[ 0 ] - forward[ 0 ] * m_flChaseDistance, m_Camera.m_Eye[ 1 ] - forward[ 1 ] * m_flChaseDistance, m_Camera.m_Eye[ 2 ] - forward[ 2 ] * m_flChaseDistance + CHASE_RISE };
				}
				m_bPlaced = true;
			}
			break;
		case DemoCamera_t::DEMO_CAMERA_FREE:
			break;
	}
	if ( !m_bPlaced )
	{
		PlaceCamera();
	}
	return m_Camera;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::CViewCamera CDemoWorld::SizedCamera()
{
	xv::map::CViewCamera camera = GetCamera();
	camera.m_nWidth = std::max( 1, width() );
	camera.m_nHeight = std::max( 1, height() );
	return camera;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<PlayerState_t> CDemoWorld::GetPlayersShown() const
{
	std::vector<PlayerState_t> players = m_Viewer.GetPlayers().At( m_Viewer.GetPlayTick() );
	std::erase_if( players, []( const PlayerState_t &player ) { return !HasFigure( player ); } );
	return players;
}

//-----------------------------------------------------------------------------
// Purpose: The figures as one mesh of the players' material, which takes its colour from
//          the vertices (carried where a normal would be, as particles carry theirs). The
//          player whose eyes the camera is in has none.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CDemoWorld::BuildPlayerMesh( const std::vector<PlayerState_t> &players, std::uint32_t nMaterial ) const
{
	auto pMesh = std::make_shared<xv::map::CMapMesh>();
	for ( const PlayerState_t &player : players )
	{
		if ( m_nMode == DemoCamera_t::DEMO_CAMERA_FIRST_PERSON && player.m_nEntity == m_nFollowed )
		{
			continue;
		}
		const QColor color = DemoTeamColor( player.m_nTeam );
		const auto boxes = FigureBoxes( player );
		for ( std::size_t nBox = 0; nBox < boxes.size(); nBox++ )
		{
			// The band at the eyes is near white, so the face shows from far off.
			const std::array<float, 3> base = nBox == 0 ? std::array<float, 3>{ static_cast<float>( color.redF() ), static_cast<float>( color.greenF() ), static_cast<float>( color.blueF() ) } : std::array<float, 3>{ 0.95f, 0.95f, 0.9f };
			for ( const BoxFace_t &face : BOX_FACES )
			{
				const auto nBase = static_cast<std::uint32_t>( pMesh->m_Vertices.size() );
				for ( const int nCorner : face.m_Corners )
				{
					xv::map::MeshVertex_t vertex;
					vertex.m_vecPosition = xv::map::ToVec3( boxes[ nBox ][ static_cast<std::size_t>( nCorner ) ] );
					vertex.m_vecNormal = { base[ 0 ] * face.m_flShade, base[ 1 ] * face.m_flShade, base[ 2 ] * face.m_flShade };
					vertex.m_flAlpha = 1.0f;
					pMesh->m_Vertices.push_back( vertex );
				}
				pMesh->m_Indices.insert( pMesh->m_Indices.end(), { nBase, nBase + 1, nBase + 2, nBase, nBase + 2, nBase + 3 } );
			}
		}
	}
	if ( pMesh->m_Indices.empty() )
	{
		return nullptr;
	}
	pMesh->m_Batches.push_back( { nMaterial, ~0u, 0, static_cast<std::uint32_t>( pMesh->m_Indices.size() ) } );
	return pMesh;
}

//-----------------------------------------------------------------------------
// Purpose: The map as the game shows it rather than as Hammer does: no tool textures, no
//          boxes for point entities, the sky behind; then the players.
//-----------------------------------------------------------------------------
MapFrame_t CDemoWorld::MakeFrame( QSize size )
{
	xv::map::CViewCamera camera = GetCamera();
	camera.m_nWidth = std::max( 1, size.width() );
	camera.m_nHeight = std::max( 1, size.height() );
	xv::map::RasterOptions_t options;
	options.m_Shade.m_nMode = xv::map::RasterMode_t::RASTER_MODE_TEXTURED_SHADED;
	options.m_bSelectionEdges = false;
	MapViewSettings_t settings;
	settings.m_bShowGrid = false;
	settings.m_bToolTextures = false;
	settings.m_bNodraw = false;
	settings.m_bHelpers = false;
	settings.m_bParticles = false;
	settings.m_bDetailProps = false;
	MapFrame_t frame = m_pRenderData->Snapshot( camera, options, settings );
	frame.m_pEntityBaseMesh = nullptr;
	frame.m_pEntityParts = nullptr;
	frame.m_pEntityPreviewMesh = nullptr;
	frame.m_pModelBoxMesh = nullptr;
	// The players need the map's material table to put theirs after; before the map is in,
	// the names alone show where they are.
	if ( frame.m_pScene && frame.m_pMaterials )
	{
		if ( frame.m_pMaterials.get() != m_pMaterialsFor )
		{
			m_pMaterialsFor = frame.m_pMaterials.get();
			auto pMaterials = std::make_shared<std::vector<xv::map::RasterMaterial_t>>( *frame.m_pMaterials );
			xv::map::RasterMaterial_t material;
			material.m_Shade.m_bUnlit = true;
			material.m_Shade.m_bVertexColor = true;
			material.m_bNoCull = true;
			pMaterials->push_back( material );
			m_pMaterials = std::move( pMaterials );
		}
		frame.m_pParticleMesh = BuildPlayerMesh( GetPlayersShown(), static_cast<std::uint32_t>( m_pMaterials->size() - 1 ) );
		frame.m_pMaterials = m_pMaterials;
	}
	return frame;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::Redraw()
{
	if ( m_pGpu )
	{
		m_pGpu->update();
		if ( m_pOverlay )
		{
			m_pOverlay->update();
		}
		return;
	}
	// Software draws half as fine: a whole map at the widget's size is slow to fill.
	const QSize size = ( QSizeF( this->size() ) * devicePixelRatioF() ).toSize() / 2;
	if ( size.isEmpty() || !isVisible() )
	{
		update();
		return;
	}
	MapFrame_t frame = MakeFrame( size );
	{
		const std::lock_guard lock( m_Mutex );
		m_Pending = std::move( frame );
	}
	m_Wake.notify_all();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::WorkerLoop()
{
	xv::texture::CImage image;
	for ( ;; )
	{
		MapFrame_t frame;
		{
			std::unique_lock lock( m_Mutex );
			m_Wake.wait( lock, [ this ] { return m_bQuit || m_Pending.has_value(); } );
			if ( m_bQuit )
			{
				return;
			}
			frame = std::move( *m_Pending );
			m_Pending.reset();
		}
		xv::map::RenderView( frame.MakeScene(), frame.m_Camera, frame.m_Options, image );
		QImage picture = ToQImage( image );
		QMetaObject::invokeMethod(
			this, [ this, picture = std::move( picture ) ]() mutable {
				m_Image = std::move( picture );
				update();
			},
			Qt::QueuedConnection );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QImage CDemoWorld::RenderNow( QSize size )
{
	const MapFrame_t frame = MakeFrame( size );
	xv::texture::CImage image;
	xv::map::RenderView( frame.MakeScene(), frame.m_Camera, frame.m_Options, image );
	return ToQImage( image );
}

//-----------------------------------------------------------------------------
// Purpose: A figure is clicked when the point is inside its body's outline on screen.
//-----------------------------------------------------------------------------
int CDemoWorld::PlayerAt( QPointF at )
{
	const xv::map::CViewCamera camera = SizedCamera();
	int nBest = 0;
	double flBestDepth = 2.0;
	for ( const PlayerState_t &player : GetPlayersShown() )
	{
		QRectF bounds;
		double flDepth = 2.0;
		bool bWhole = true;
		const auto boxes = FigureBoxes( player );
		for ( const Point_t &corner : boxes[ 0 ] )
		{
			const auto projected = camera.Project( corner );
			if ( !projected )
			{
				bWhole = false;
				break;
			}
			const QPointF point( ( *projected )[ 0 ], ( *projected )[ 1 ] );
			bounds = bounds.isNull() ? QRectF( point, QSizeF( 0.01, 0.01 ) ) : bounds.united( QRectF( point, QSizeF( 0.01, 0.01 ) ) );
			flDepth = std::min( flDepth, ( *projected )[ 2 ] );
		}
		if ( bWhole && bounds.adjusted( -2, -2, 2, 2 ).contains( at ) && flDepth < flBestDepth )
		{
			flBestDepth = flDepth;
			nBest = player.m_nEntity;
		}
	}
	return nBest;
}

//-----------------------------------------------------------------------------
// Purpose: Over the picture: each player's name above its figure; the figures' edges where
//          they are to show through walls (and always while there is no map to hide them);
//          and what the map is doing, while it is read.
//-----------------------------------------------------------------------------
void CDemoWorld::PaintLabels( QPainter &painter )
{
	const xv::map::CViewCamera camera = SizedCamera();
	painter.setRenderHint( QPainter::Antialiasing );
	const std::vector<PlayerState_t> players = GetPlayersShown();
	const bool bEdges = m_bThroughWalls || !HasMap();
	QFont font = painter.font();
	font.setBold( true );
	painter.setFont( font );
	const QFontMetrics metrics( font );
	for ( const PlayerState_t &player : players )
	{
		if ( m_nMode == DemoCamera_t::DEMO_CAMERA_FIRST_PERSON && player.m_nEntity == m_nFollowed )
		{
			continue;
		}
		const QColor color = DemoTeamColor( player.m_nTeam );
		if ( bEdges )
		{
			painter.setPen( QPen( color, player.m_nEntity == m_nFollowed ? 2.0 : 1.0 ) );
			for ( const auto &box : FigureBoxes( player ) )
			{
				for ( const auto &edge : BOX_EDGES )
				{
					if ( const auto line = camera.ProjectLine( box[ static_cast<std::size_t>( edge[ 0 ] ) ], box[ static_cast<std::size_t>( edge[ 1 ] ) ] ) )
					{
						painter.drawLine( QPointF( line->first[ 0 ], line->first[ 1 ] ), QPointF( line->second[ 0 ], line->second[ 1 ] ) );
					}
				}
			}
		}
		if ( !m_bNames || !player.m_pIdentity )
		{
			continue;
		}
		const Point_t top{ player.m_vecOrigin.m_flX, player.m_vecOrigin.m_flY, player.m_vecOrigin.m_flZ + FigureHeight( player ) + NAME_RISE };
		const Point_t away = xv::map::Subtract( top, camera.m_Eye );
		if ( xv::map::Dot( away, away ) > NAME_DISTANCE * NAME_DISTANCE && player.m_nEntity != m_nFollowed )
		{
			continue;
		}
		const auto at = camera.Project( top );
		if ( !at || ( *at )[ 0 ] < -200 || ( *at )[ 0 ] > width() + 200 || ( *at )[ 1 ] < -20 || ( *at )[ 1 ] > height() + 20 )
		{
			continue;
		}
		const QString szName = QString::fromUtf8( player.m_pIdentity->m_szName );
		const QPointF origin( ( *at )[ 0 ] - metrics.horizontalAdvance( szName ) / 2.0, ( *at )[ 1 ] - metrics.descent() );
		// Dark behind the letters, so a name reads over any wall.
		QPainterPath path;
		path.addText( origin, font, szName );
		painter.setPen( QPen( QColor( 0, 0, 0, 200 ), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
		painter.setBrush( Qt::NoBrush );
		painter.drawPath( path );
		painter.setPen( Qt::NoPen );
		painter.setBrush( player.m_nEntity == m_nFollowed ? QColor( 255, 255, 255 ) : color.lighter( 130 ) );
		painter.drawPath( path );
	}
	if ( !m_szStatus.isEmpty() )
	{
		font.setBold( false );
		painter.setFont( font );
		const QRect box = QFontMetrics( font ).boundingRect( rect().adjusted( 12, 10, -12, -10 ), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_szStatus );
		painter.fillRect( box.adjusted( -6, -4, 6, 4 ), QColor( 0, 0, 0, 160 ) );
		painter.setPen( Qt::white );
		painter.drawText( box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_szStatus );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::paintEvent( QPaintEvent * )
{
	if ( m_pGpu )
	{
		return; // the card draws, the overlay labels
	}
	QPainter painter( this );
	if ( m_Image.isNull() )
	{
		painter.fillRect( rect(), QColor( 30, 30, 30 ) );
	}
	else
	{
		painter.drawImage( rect(), m_Image );
	}
	PaintLabels( painter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::resizeEvent( QResizeEvent * )
{
	if ( m_pGpu )
	{
		m_pGpu->setGeometry( rect() );
	}
	if ( m_pOverlay )
	{
		m_pOverlay->setGeometry( rect() );
		m_pOverlay->raise();
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Turning or moving a camera that follows something makes it the hand's.
//-----------------------------------------------------------------------------
void CDemoWorld::TakeCamera()
{
	if ( m_nMode != DemoCamera_t::DEMO_CAMERA_FREE )
	{
		SetCameraMode( DemoCamera_t::DEMO_CAMERA_FREE );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::mousePressEvent( QMouseEvent *pEvent )
{
	setFocus();
	m_LastMouse = m_PressAt = pEvent->pos();
	m_bDragged = false;
}

//-----------------------------------------------------------------------------
// Purpose: A drag with either button turns the camera.
//-----------------------------------------------------------------------------
void CDemoWorld::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( !( pEvent->buttons() & ( Qt::LeftButton | Qt::RightButton ) ) )
	{
		return;
	}
	if ( !m_bDragged && ( pEvent->pos() - m_PressAt ).manhattanLength() <= CLICK_SLACK )
	{
		return;
	}
	m_bDragged = true;
	TakeCamera();
	const QPoint delta = pEvent->pos() - m_LastMouse;
	m_LastMouse = pEvent->pos();
	m_Camera.m_flYaw = std::remainder( m_Camera.m_flYaw - delta.x() * LOOK_SPEED, 360.0 );
	m_Camera.m_flPitch = std::clamp( m_Camera.m_flPitch + delta.y() * LOOK_SPEED, -89.0, 89.0 );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: A click on a player follows it from behind; on the one already followed, looks
//          through its eyes.
//-----------------------------------------------------------------------------
void CDemoWorld::mouseReleaseEvent( QMouseEvent *pEvent )
{
	if ( m_bDragged || pEvent->button() != Qt::LeftButton )
	{
		return;
	}
	if ( const int nEntity = PlayerAt( pEvent->position() ) )
	{
		const bool bAgain = nEntity == m_nFollowed && m_nMode == DemoCamera_t::DEMO_CAMERA_CHASE;
		SetFollowed( nEntity );
		SetCameraMode( bAgain ? DemoCamera_t::DEMO_CAMERA_FIRST_PERSON : DemoCamera_t::DEMO_CAMERA_CHASE );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The wheel moves the free camera along its view, and the chasing one nearer or
//          further.
//-----------------------------------------------------------------------------
void CDemoWorld::wheelEvent( QWheelEvent *pEvent )
{
	const double flSteps = pEvent->angleDelta().y() / 120.0;
	if ( m_nMode == DemoCamera_t::DEMO_CAMERA_CHASE )
	{
		m_flChaseDistance = std::clamp( m_flChaseDistance * std::pow( 0.85, flSteps ), CHASE_NEAREST, CHASE_FURTHEST );
	}
	else
	{
		TakeCamera();
		const Point_t forward = m_Camera.GetForward();
		m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( forward, flSteps * WHEEL_STEP ) );
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: W, A, S and D fly the camera, E and Q raise and lower it, F finds the players;
//          the rest are the viewer's (Space, the arrows).
//-----------------------------------------------------------------------------
void CDemoWorld::keyPressEvent( QKeyEvent *pEvent )
{
	switch ( pEvent->key() )
	{
		case Qt::Key_W:
		case Qt::Key_A:
		case Qt::Key_S:
		case Qt::Key_D:
		case Qt::Key_Q:
		case Qt::Key_E:
			if ( !pEvent->isAutoRepeat() && std::ranges::find( m_Held, pEvent->key() ) == m_Held.end() )
			{
				m_Held.push_back( pEvent->key() );
				TakeCamera();
				if ( !m_pFly->isActive() )
				{
					m_FlyClock.start();
					m_pFly->start();
				}
			}
			break;
		case Qt::Key_F:
			LookAtPlayers();
			break;
		default:
			QWidget::keyPressEvent( pEvent );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::keyReleaseEvent( QKeyEvent *pEvent )
{
	if ( pEvent->isAutoRepeat() )
	{
		return;
	}
	std::erase( m_Held, pEvent->key() );
	if ( m_Held.empty() )
	{
		m_pFly->stop();
	}
	QWidget::keyReleaseEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::focusOutEvent( QFocusEvent *pEvent )
{
	m_Held.clear();
	m_pFly->stop();
	QWidget::focusOutEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::Fly()
{
	const double flSeconds = std::min( static_cast<double>( m_FlyClock.restart() ) / 1000.0, 0.1 );
	const double flStep = FLY_SPEED * flSeconds * ( QGuiApplication::keyboardModifiers() & Qt::ShiftModifier ? FLY_FAST : 1.0 );
	const Point_t forward = m_Camera.GetForward();
	const Point_t right = m_Camera.GetRight();
	Point_t move{};
	for ( const int nKey : m_Held )
	{
		switch ( nKey )
		{
			case Qt::Key_W:
				move = xv::map::Add( move, forward );
				break;
			case Qt::Key_S:
				move = xv::map::Subtract( move, forward );
				break;
			case Qt::Key_D:
				move = xv::map::Add( move, right );
				break;
			case Qt::Key_A:
				move = xv::map::Subtract( move, right );
				break;
			case Qt::Key_E:
				move[ 2 ] += 1.0;
				break;
			case Qt::Key_Q:
				move[ 2 ] -= 1.0;
				break;
			default:
				break;
		}
	}
	m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( move, flStep ) );
	Redraw();
}
