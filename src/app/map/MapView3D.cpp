//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapView3D.h. The camera controls are Hammer's: the camera tool's drags, Z to
//          toggle mouselook (Space to hold it, Space and a drag for the camera tool's drags,
//          with mouselook on too), WASD and the arrows to fly, the wheel to step; with the
//          preference, both buttons together fly forward, back and sideways in any tool; Alt
//          and a right drag orbit the point under the pointer.
//
//===========================================================================//
#include "MapView3D.h"
#include "MapBrushEdit.h"
#include "MapEventClock.h"
#include <QThread>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <QApplication>
#include <QAbstractEventDispatcher>
#include <QGuiApplication>
#include <QCursor>
#include <QDateTime>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPolygonF>
#include <QTransform>
#include <QTimer>
#include <QWheelEvent>

#include "Preferences.h"
#include "DebugLog.h"
#include "map/Cordon.h"
#include "map/Fgd.h"
#include "kv1/Kv1.h"
#include "util/Numbers.h"
#include "map/Trace.h"
#include "map/Helpers.h"
#include "map/Instance.h"
#include "map/Lighting.h"
#include "map/MapSprinkle.h"
#include "map/DispEdit.h"
#include "map/Overlay.h"
#include "map/VmfValues.h"
#include "map/MapFaceEdit.h"
#include "map/MapPanes.h"
#include "map/MapView2D.h"
#include "map/MapConstants.h"
#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "map/MapScreenEffect.h"
#include "map/MapPhysics.h"
#include "DemoHud.h"
#include "Graphics.h"
#include "GpuView.h"
#ifdef VOMIT_TOOLS_GPU
#include <rhi/qrhi.h>
#endif
#include "map/MapRenderer.h"

//-----------------------------------------------------------------------------
// Purpose: Bind to the application's dispatcher; an event filter starts accounting before
//          handlers even on dispatchers whose awake signal follows event delivery.
//-----------------------------------------------------------------------------
CMapEventClock::CMapEventClock()
	: QObject( QCoreApplication::instance() )
{
	m_Wall.start();
	Awake();
	auto *pApplication = QCoreApplication::instance();
	auto *pDispatcher = QAbstractEventDispatcher::instance( pApplication->thread() );
	connect( pDispatcher, &QAbstractEventDispatcher::awake, this, &CMapEventClock::Awake, Qt::DirectConnection );
	connect( pDispatcher, &QAbstractEventDispatcher::aboutToBlock, this, &CMapEventClock::Block, Qt::DirectConnection );
	pApplication->installEventFilter( this );
}

//-----------------------------------------------------------------------------
// Purpose: Count work once, even when a handler enters another event loop.
//-----------------------------------------------------------------------------
void CMapEventClock::Awake()
{
	if ( !m_Busy.isValid() )
		m_Busy.start();
	const int nLevel = QThread::currentThread()->loopLevel();
	if ( nLevel > 1 && nLevel > m_nLastLoopLevel )
		++m_nNestedLoops;
	m_nLastLoopLevel = nLevel;
	m_nMaximumLoopLevel = std::max( m_nMaximumLoopLevel, nLevel );
}

//-----------------------------------------------------------------------------
// Purpose: Exclude time spent sleeping in both outer and nested dispatchers.
//-----------------------------------------------------------------------------
void CMapEventClock::Block()
{
	if ( m_Busy.isValid() )
	{
		m_flBusyMs += m_Busy.nsecsElapsed() / 1e6;
		m_Busy.invalidate();
	}
}

//-----------------------------------------------------------------------------
// Purpose: A handler must be timed from its beginning, before its dispatcher announces awake.
//-----------------------------------------------------------------------------
bool CMapEventClock::eventFilter( QObject *, QEvent * )
{
	Awake();
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Cumulative work, including the active handler, for measurements and regression tests.
//-----------------------------------------------------------------------------
double CMapEventClock::GetBusyTime() const
{
	return m_flBusyMs + ( m_Busy.isValid() ? m_Busy.nsecsElapsed() / 1e6 : 0.0 );
}

//-----------------------------------------------------------------------------
// Purpose: Report the most recently completed observation window without losing active work.
//-----------------------------------------------------------------------------
QString CMapEventClock::Describe()
{
	const double flWall = m_Wall.nsecsElapsed() / 1e6;
	const double flBusy = GetBusyTime();
	if ( flWall >= 1000 )
	{
		m_flLastBusy = flBusy - m_flWindowStart;
		m_flWindowStart = flBusy;
		m_flLastWall = flWall;
		m_Wall.restart();
	}
	return QStringLiteral( "event loop: %1 ms busy / %2 ms elapsed; %3 nested loops, depth %4" ).arg( m_flLastBusy, 0, 'f', 1 ).arg( m_flLastWall, 0, 'f', 1 ).arg( m_nNestedLoops ).arg( m_nMaximumLoopLevel );
}

//-----------------------------------------------------------------------------
// Purpose: One clock for every map pane, with the application's lifetime.
//-----------------------------------------------------------------------------
CMapEventClock &CMapEventClock::Get()
{
	static auto *pClock = new CMapEventClock;
	return *pClock;
}

namespace
{

// World texts are laid out at this size in pixels (a font's size: a line), then mapped into the
// world.
constexpr int WORLD_TEXT_PIXELS = 64;
// A rainbow point_worldtext's letters each take a hue (saturation 0.7, full value), this many
// degrees on from the letter before, all moving on this many degrees a second.
constexpr double WORLD_TEXT_RAINBOW_LETTER_DEGREES = 15.0;
constexpr double WORLD_TEXT_RAINBOW_DEGREES_A_SECOND = 100.0;
// The game draws its letters from distance-field sheets whose edges ramp over about this many of
// the sheet's pixels (measured from CS:S's editor/worldtextsheet_* textures); a material's outline
// and shadow reach out over parts of that ramp.
constexpr double WORLD_TEXT_SHEET_RAMP = 6.5;
// editor_text's letters are outlined in black, this wide (of the font's size) all round.
constexpr double EDITOR_TEXT_OUTLINE = 0.08;

// The typefaces point_worldtext's font key chooses among: the faces the game's sheets were made
// from, by name where they are installed, else the nearest kind; and the size each sheet was
// made at, which its spacing keys count in.
struct WorldTextFace_t
{
	QStringList m_Families;
	QFont::StyleHint m_nHint;
	bool m_bBold;
	bool m_bSmallCaps; // the sheet has capitals only: its small letters are smaller capitals
	double m_flSheetSize;
};

//-----------------------------------------------------------------------------
// Purpose: TF2 Build, TF2, Liberation Sans, TF2 Professor and Roboto Mono, in that order.
//-----------------------------------------------------------------------------
const std::array<WorldTextFace_t, 5> &WorldTextFaces()
{
	static const std::array<WorldTextFace_t, 5> faces{ {
		{ { QStringLiteral( "TF2 Build" ), QStringLiteral( "Impact" ), QStringLiteral( "DejaVu Sans" ) }, QFont::SansSerif, true, true, 64.0 },
		{ { QStringLiteral( "TF2 Secondary" ), QStringLiteral( "TF2" ), QStringLiteral( "DejaVu Sans" ) }, QFont::SansSerif, true, true, 64.0 },
		{ { QStringLiteral( "Liberation Sans" ), QStringLiteral( "Arial" ), QStringLiteral( "Helvetica" ) }, QFont::SansSerif, false, false, 64.0 },
		{ { QStringLiteral( "TF2 Professor" ), QStringLiteral( "Comic Neue" ), QStringLiteral( "Comic Sans MS" ) }, QFont::Cursive, false, true, 128.0 },
		{ { QStringLiteral( "Roboto Mono" ), QStringLiteral( "DejaVu Sans Mono" ), QStringLiteral( "Courier New" ) }, QFont::Monospace, false, false, 128.0 },
	} };
	return faces;
}

// What a font key draws, from the game's editor/worldtext_<key + 1> materials: the face, and the
// outline, drop shadow and glow those give its letters (none where invalid), and the colour one
// keeps whatever the entity's ($vertexcolor 0).
struct WorldTextLook_t
{
	int m_nFace = 0;
	QColor m_Outline;
	bool m_bShadow = false;
	QColor m_Glow;
	QColor m_Fixed;
};

//-----------------------------------------------------------------------------
// Purpose: The font key's look: a number (0 to 12; more is 12), or the material's own name.
//-----------------------------------------------------------------------------
WorldTextLook_t WorldTextLookOf( std::string_view szFont )
{
	int nKey = 0;
	if ( const auto nNumber = xv::ParseNumber<int>( szFont ) )
		nKey = *nNumber;
	else if ( const std::size_t nAt = szFont.rfind( "worldtext_" ); nAt != std::string_view::npos )
		nKey = xv::ParseNumber<int>( szFont.substr( nAt + 10 ) ).value_or( 1 ) - 1;
	nKey = std::clamp( nKey, 0, 12 );
	const QColor black( 0, 0, 0 );
	if ( nKey == 12 )
		return { 0, black, false, {}, {} };
	if ( nKey == 11 )
		return { 4, {}, false, QColor( 0, 255, 0 ), QColor( 153, 255, 102 ) };
	if ( nKey == 10 )
		return { 4, QColor( 255, 255, 255 ), true, {}, {} };
	const bool bPlain = nKey % 2 == 1;
	return { nKey / 2, bPlain ? QColor{} : black, !bPlain, {}, {} };
}
// The orbit's point: this far ahead when nothing is under the pointer or selected; the wheel
// brings the camera no nearer than this.
constexpr double ORBIT_AHEAD = 256.0;
constexpr double ORBIT_NEAREST = 8.0;

constexpr int TICK_MS = 16;
constexpr int SETTLE_MS = 150;
constexpr double SLOW_RENDER_MS = 40.0; // slower than this, moving pictures drop to half size
// The orthographic view: a wheel notch's zoom, its limits (pixels a unit, as the 2D views'), and
// how far ahead it keeps the size of when nothing is under the middle.
constexpr double ORTHO_WHEEL_ZOOM = 1.2;
constexpr double ORTHO_SMALLEST = 1.0 / 64.0;
constexpr double ORTHO_LARGEST = 64.0;
constexpr double ORTHO_DEFAULT_DISTANCE = 512.0;
constexpr double BOX_ARROW_PIXELS = 26.0; // how far a block box's arrows stand out of its faces
constexpr double BOX_ARROW_HEAD = 7.0;
constexpr double BOX_ARROW_SLACK = 6.0;   // how near the pointer has to be

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QImage ToQImage( const xv::texture::CImage &image )
{
	return QImage( image.m_Pixels.data(), image.m_nWidth, image.m_nHeight, image.m_nWidth * 4, QImage::Format_RGBA8888 ).copy();
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Labels over the graphics card's picture, where QPainter can't draw on it.
//-----------------------------------------------------------------------------
CMapOverlay3D::CMapOverlay3D( CMapView3D &view )
	: QWidget( &view ),
	  m_View( view )
{
	setAttribute( Qt::WA_TransparentForMouseEvents );
	setAttribute( Qt::WA_NoSystemBackground );
	setFocusPolicy( Qt::NoFocus );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapOverlay3D::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	m_View.PaintLabels( painter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapView3D::CMapView3D( CMapEditor &editor, MapViewType_t nType, QWidget *pParent )
	: CMapView( editor, pParent ),
	  m_nType( nType )
{
	setAttribute( Qt::WA_OpaquePaintEvent );
	setMouseTracking( true );
	setCursor( Qt::ArrowCursor ); // not the splitter's bar's, on the way in
	m_Camera.m_flNear = mapstyle::NEAR_PLANE;
	// Not ApplyPreferences: that tells the editor, which is still making its panes.
	m_Camera.m_flFov = CPreferences::Instance().GetMapPreferences().m_flFov;
	m_Camera.m_flFar = CPreferences::Instance().GetMapPreferences().m_flRenderDistance;
	static_cast<void>( CMapEventClock::Get() );
	m_pPaintPreview = new QTimer( this );
	m_pPaintPreview->setSingleShot( true );
	connect( m_pPaintPreview, &QTimer::timeout, this, &CMapView3D::PreviewPaint );
	m_pTick = new QTimer( this );
	m_pTick->setInterval( TICK_MS );
	m_pTick->setTimerType( Qt::PreciseTimer );
	connect( m_pTick, &QTimer::timeout, this, &CMapView3D::Tick );
	m_pSettle = new QTimer( this );
	m_pSettle->setSingleShot( true );
	m_pSettle->setInterval( SETTLE_MS );
	connect( m_pSettle, &QTimer::timeout, this, [ this ] {
		m_bReduced = false;
		RequestRender();
	} );
	m_Worker = std::thread( [ this ] { WorkerLoop(); } );
	SetUseGpu( CGraphics::Instance().IsGpu() );
	ResetCamera();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapView3D::~CMapView3D()
{
	{
		const std::lock_guard lock( m_Mutex );
		m_bQuit = true;
	}
	m_Wake.notify_all();
	m_Worker.join();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::SetUseGpu( bool bOn )
{
	DebugLog() << "map 3D view drawn" << ( bOn ? "on the graphics card" : "in software" ) << ( m_pGpu && !bOn ? "(the card's drawing failed)" : "" );
	delete m_pGpu;
	m_pGpu = nullptr;
#ifdef VOMIT_TOOLS_GPU
	if ( bOn )
	{
		auto pRenderer = std::make_unique<CMapRenderer>( [ this ]( QSize size ) {
			StepFlight();
			return MakeFrame( size );
		} );
		pRenderer->SetExposureSink( [ this ]( float flExposure ) { ExposureMeasured( flExposure ); },
			[ this ] { QTimer::singleShot( 0, this, [ this ] {
						   if ( m_pGpu )
						   {
							   m_pGpu->update();
						   }
					   } ); } );
		m_pGpu = new CGpuView( std::move( pRenderer ), this );
		connect( m_pGpu, &QRhiWidget::frameSubmitted, this, &CMapView3D::PaintFrameSubmitted );
		m_pGpu->setAttribute( Qt::WA_TransparentForMouseEvents );
		m_pGpu->setFocusPolicy( Qt::NoFocus );
		m_pGpu->setGeometry( rect() );
		m_pGpu->show();
		// A card that fails draws in software from then on.
		connect( m_pGpu, &QRhiWidget::renderFailed, this, [ this ] { QTimer::singleShot( 0, this, [ this ] {
																		 SetUseGpu( false );
																		 m_Editor.RendererChanged();
																	 } ); } );
		m_pOverlay = new CMapOverlay3D( *this );
		m_pOverlay->setGeometry( rect() );
		m_pOverlay->show();
		m_pOverlay->raise();
		return;
	}
#else
	(void)bOn;
#endif
	delete m_pOverlay;
	m_pOverlay = nullptr;
	RequestRender();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::SetType( MapViewType_t nType )
{
	if ( !Is2DView( nType ) && nType != m_nType )
	{
		m_nType = nType;
		Changed();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::RasterMode_t CMapView3D::GetMode() const
{
	switch ( m_nType )
	{
		case MapViewType_t::MAP_VIEW_TYPE_3D_WIREFRAME: return xv::map::RasterMode_t::RASTER_MODE_WIREFRAME;
		case MapViewType_t::MAP_VIEW_TYPE_3D_FLAT: return xv::map::RasterMode_t::RASTER_MODE_FLAT;
		case MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED_SHADED: return xv::map::RasterMode_t::RASTER_MODE_TEXTURED_SHADED;
		case MapViewType_t::MAP_VIEW_TYPE_3D_LIGHTMAP_GRID: return xv::map::RasterMode_t::RASTER_MODE_LIGHTMAP_GRID;
		case MapViewType_t::MAP_VIEW_TYPE_3D_LIGHTING: return xv::map::RasterMode_t::RASTER_MODE_LIGHTING;
		default: return xv::map::RasterMode_t::RASTER_MODE_TEXTURED;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Where the camera was when the map was last closed, else the map's active camera, else
//          Hammer's start without one: the origin, at angles 0 0 0 (facing east).
//-----------------------------------------------------------------------------
void CMapView3D::PlaceCamera()
{
	const xv::map::CScene *pScene = GetDocument().GetScene();
	m_nCameraLoad = GetDocument().GetLoadCount();
	m_Camera.m_Eye = {};
	m_Camera.m_flYaw = 0.0;
	m_Camera.m_flPitch = 0.0;
	if ( const std::optional<xv::map::CViewCamera> &remembered = m_Editor.GetRememberedCamera(); pScene && remembered )
	{
		m_Camera.m_Eye = remembered->m_Eye;
		m_Camera.m_flYaw = remembered->m_flYaw;
		m_Camera.m_flPitch = remembered->m_flPitch;
	}
	else if ( pScene && !pScene->m_Map.m_Cameras.empty() )
	{
		const int nActive = std::clamp( pScene->m_Map.m_nActiveCamera, 0, static_cast<int>( pScene->m_Map.m_Cameras.size() ) - 1 );
		const xv::map::Camera_t &camera = pScene->m_Map.m_Cameras[ static_cast<std::size_t>( nActive ) ];
		m_Camera.LookAt( camera.m_Position, camera.m_Look );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::ResetCamera()
{
	PlaceCamera();
	Changed();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::SetCamera( const xv::map::CViewCamera &camera )
{
	m_Camera = camera;
	Changed();
}

//-----------------------------------------------------------------------------
// Purpose: Something the picture shows changed.
//-----------------------------------------------------------------------------
void CMapView3D::Changed()
{
	m_Editor.CameraMoved( this );
	RequestRender();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapFrame_t CMapView3D::MakeFrame( QSize size )
{
	// Another map, not an edit's new snapshot of this one: those keep the camera where it is.
	if ( GetDocument().GetScene() && GetDocument().GetLoadCount() != m_nCameraLoad )
	{
		PlaceCamera();
	}
	xv::map::CViewCamera camera = m_Camera;
	camera.m_nWidth = std::max( 1, size.width() );
	camera.m_nHeight = std::max( 1, size.height() );
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	const MapColors_t &colors = preferences.m_Colors;
	auto vector = []( const QColor &color ) { return xv::CVec3{ static_cast<float>( color.redF() ), static_cast<float>( color.greenF() ), static_cast<float>( color.blueF() ) }; };
	auto bytes = []( const QColor &color ) { return std::array<std::uint8_t, 3>{ static_cast<std::uint8_t>( color.red() ), static_cast<std::uint8_t>( color.green() ), static_cast<std::uint8_t>( color.blue() ) }; };
	xv::map::RasterOptions_t options;
	options.m_Shade.m_nMode = GetMode();
	options.m_Shade.m_vecSelection = vector( colors.m_Selection3D );
	options.m_Shade.m_flSelectionTint = static_cast<float>( preferences.m_flSelectionTint );
	options.m_Shade.m_vecFaceShade = { mapstyle::FACE_SHADE[ 0 ], mapstyle::FACE_SHADE[ 1 ], mapstyle::FACE_SHADE[ 2 ] };
	options.m_Shade.m_flLuxelShade = static_cast<float>( preferences.m_flLightmapGridShade );
	options.m_Shade.m_vecInstance = vector( colors.m_Instance );
	options.m_Shade.m_flInstanceTint = static_cast<float>( preferences.m_flInstanceTint );
	options.m_Background = bytes( colors.m_Background3D );
	options.m_SelectionEdge = bytes( colors.m_SelectionEdge3D );
	options.m_Shade.m_vecInvalid = vector( colors.m_Invalid );
	options.m_Shade.m_flInvalidTint = static_cast<float>( preferences.m_flInvalidTint );
	options.m_Shade.m_bMipBlend = preferences.m_nTextureFilter != MapTextureFilter_t::MAP_TEXTURE_FILTER_BILINEAR;
	options.m_Shade.m_nAnisotropy = MapAnisotropy( preferences.m_nTextureFilter );
	options.m_InvalidEdge = bytes( colors.m_Invalid );
	// The 3D grid (P), on the 2D views' grid: the world units a pixel covers grow with distance,
	// or are the same everywhere looking orthographic.
	if ( const MapViewSettings_t &settings = m_Editor.GetSettings(); settings.m_bShow3DGrid && options.m_Shade.m_nMode != xv::map::RasterMode_t::RASTER_MODE_WIREFRAME )
	{
		options.m_Shade.m_flGrid = static_cast<float>( settings.m_nGridSize );
		if ( camera.IsOrthographic() )
			options.m_Shade.m_flGridPixel = static_cast<float>( 1.0 / camera.m_flOrthoScale );
		else
			options.m_Shade.m_flGridPerDistance = static_cast<float>( 2.0 * std::tan( std::clamp( camera.m_flFov, 1.0, 179.0 ) * std::numbers::pi / 360.0 ) / camera.m_nWidth );
	}
	// The world grid (Shift+P): the same lines on a plane of its own, in any look.
	if ( const MapViewSettings_t &settings = m_Editor.GetSettings(); settings.m_bWorldGrid )
	{
		options.m_flWorldGrid = static_cast<float>( settings.m_nGridSize );
		options.m_WorldGridOrigin = settings.m_WorldGrid.m_Origin;
		options.m_WorldGridU = settings.m_WorldGrid.m_Axes[ 0 ];
		options.m_WorldGridV = settings.m_WorldGrid.m_Axes[ 1 ];
	}
	if ( options.m_Shade.m_flGrid > 0.0f || options.m_flWorldGrid > 0.0f )
	{
		for ( std::size_t i = 0; i < 3; i++ )
			for ( std::size_t j = 0; j < 4; j++ )
				options.m_Shade.m_GridColors[ i ][ j ] = mapstyle::GRID_3D[ i ][ j ];
	}
	// A compiled map is lit by its own light, never baked again.
	CMapCompiled *pCompiled = m_Editor.GetCompiled();
	if ( pCompiled && !pCompiled->IsBuilt() )
	{
		pCompiled = nullptr;
	}
	if ( options.m_Shade.m_nMode == xv::map::RasterMode_t::RASTER_MODE_LIGHTING && !pCompiled )
	{
		m_Editor.WantLighting();
	}
	MapFrame_t frame = m_Editor.GetRenderData().Snapshot( camera, options, m_Editor.GetSettings() );
	if ( !pCompiled )
		m_Editor.ApplyModelPreview( frame );
	if ( pCompiled )
	{
		pCompiled->Apply( frame, pCompiled->GetLumpPlacements() );
	}
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BRUSH_EDIT && frame.m_pFlags )
	{
		auto pFlags = std::make_shared<std::vector<std::uint8_t>>( *frame.m_pFlags );
		for ( auto &nFlags : *pFlags )
			nFlags &= ~xv::map::RASTER_OBJECT_SELECTED;
		frame.m_pFlags = std::move( pFlags );
	}
	frame.m_bModelEdges = preferences.m_bWireframeModels;
	// HDR's auto exposure is this view's own, measured from what it draws.
	if ( frame.m_pLighting && frame.m_Options.m_Shade.m_bLightHdr )
	{
		frame.m_flExposure = m_flExposure;
		frame.m_Options.m_Shade.m_flLightScale *= m_flExposure;
	}
	// The screen effect shown over the 3D views, as at now for its proxies.
	frame.m_pScreenEffect = m_Editor.GetShownScreenEffect();
	frame.m_flScreenTime = m_Editor.GetScreenEffectTime();
	return frame;
}

//-----------------------------------------------------------------------------
// Purpose: Hands the newest frame to the drawing thread (or the graphics card).
//-----------------------------------------------------------------------------
void CMapView3D::RequestRender()
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
	const qreal flRatio = devicePixelRatioF();
	QSize size = ( QSizeF( this->size() ) * flRatio ).toSize();
	if ( m_bReduced )
	{
		size /= 2;
	}
	if ( size.isEmpty() )
	{
		return;
	}
	// Made before taking the lock: making it can come back here.
	MapFrame_t frame = MakeFrame( size );
	{
		const std::lock_guard lock( m_Mutex );
		m_Pending = std::move( frame );
	}
	m_Wake.notify_all();
}

//-----------------------------------------------------------------------------
// Purpose: The drawing thread: one frame at a time, always the newest asked for.
//-----------------------------------------------------------------------------
void CMapView3D::WorkerLoop()
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
		QElapsedTimer timer;
		timer.start();
		xv::map::RenderView( frame.MakeScene(), frame.m_Camera, frame.m_Options, image );
		if ( frame.m_pScreenEffect )
			frame.m_pScreenEffect->Apply( image, frame.m_flScreenTime );
		const double flMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
		QImage picture = ToQImage( image );
		// The HDR lighting preview's next exposure, from this picture (every fourth pixel each way).
		const float flExposure = frame.m_flExposure > 0.0f ? xv::map::NextExposure( xv::map::ShownLuminance( image.m_Pixels.data(), image.m_nWidth, image.m_nHeight, static_cast<std::size_t>( image.m_nWidth ) * 4, 4, frame.m_Options.m_Shade.m_flMonitorGamma ), frame.m_flExposure, frame.m_flExposureMin, frame.m_flExposureMax ) : 0.0f;
		QMetaObject::invokeMethod(
			this, [ this, picture = std::move( picture ), flMs, flExposure ]() mutable {
				m_Image = std::move( picture );
				m_flRenderMs = flMs;
				update();
				if ( flExposure > 0.0f )
				{
					ExposureMeasured( flExposure );
				}
			},
			Qt::QueuedConnection );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A measured exposure: drawn again with it while it still moves, and measured again
//          a little later, so it settles as the game's does when the eye adapts.
//-----------------------------------------------------------------------------
void CMapView3D::ExposureMeasured( float flExposure )
{
	if ( std::abs( std::log2( flExposure / std::max( m_flExposure, 1.0e-6f ) ) ) < 0.02f )
	{
		return;
	}
	m_flExposure = flExposure;
	RequestRender();
	QTimer::singleShot( 200, this, [ this ] { RequestRender(); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapRenderStats_t CMapView3D::GetStats() const
{
	if ( m_pGpu )
	{
		if ( const auto *pRenderer = dynamic_cast<const CMapRenderer *>( &m_pGpu->GetRenderer() ) )
		{
			return pRenderer->GetStats();
		}
	}
	MapRenderStats_t stats;
	stats.m_flFrameMs = m_flRenderMs;
	return stats;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QImage CMapView3D::RenderNow( QSize size )
{
	const MapFrame_t frame = MakeFrame( size );
	xv::texture::CImage image;
	xv::map::RenderView( frame.MakeScene(), frame.m_Camera, frame.m_Options, image );
	if ( frame.m_pScreenEffect )
		frame.m_pScreenEffect->Apply( image, frame.m_flScreenTime );
	return ToQImage( image );
}

//-----------------------------------------------------------------------------
// Purpose: The view stands for the player's screen; a message on a channel replaces the one
//          before it there, so of several selected the last on each channel shows.
//-----------------------------------------------------------------------------
void CMapView3D::PaintGameTexts( QPainter &painter ) const
{
	const xv::map::CScene *pScene = GetDocument().GetScene();
	if ( !pScene )
		return;
	std::map<int, xv::demo::HudText_t> channels;
	for ( const std::uint32_t nObject : GetDocument().GetSelection() )
	{
		if ( nObject >= pScene->GetObjectCount() || pScene->IsSolidObject( nObject ) )
			continue;
		const int nEntity = pScene->GetObjectEntity( nObject );
		if ( nEntity < 0 || static_cast<std::size_t>( nEntity ) >= pScene->m_Map.m_Entities.size() )
			continue;
		const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ];
		if ( !xv::kv1::EqualsIgnoreCase( entity.GetClassname(), "game_text" ) )
			continue;
		xv::demo::HudText_t text = xv::demo::HudTextFromGameText( [ & ]( std::string_view szKey ) { return entity.GetValue( szKey ); } );
		if ( !text.m_szText.empty() )
			channels[ text.m_nChannel ] = std::move( text );
	}
	if ( channels.empty() )
		return;
	painter.save();
	painter.setRenderHint( QPainter::TextAntialiasing, true );
	const QFont font = DemoHudTextFont( height() );
	for ( const auto &[ nChannel, text ] : channels )
		DemoPaintHudText( painter, rect(), text, xv::demo::HudTextHeldAt( text ), font );
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer++'s editor_text and the games' point_worldtext (any class with the FGD's
//          worldtext() helper), as the game draws point_worldtext: the message from the
//          entity's origin, the first line's baseline there and the others below; stationary
//          text (orientation 0) starts there and reads along the entity's right (Source's
//          angles), turning text faces the camera (1) or turns about z only (2) and is centred
//          across; every line starts at the same edge. textsize is a line, in units (less than
//          0 turns it over); the spacing keys count in the font sheet's pixels; it shows from
//          both sides (its materials don't cull). Drawn over the picture through a perspective
//          transform of the text's plane; one whose origin something drawn stands in front of
//          is left out.
//-----------------------------------------------------------------------------
void CMapView3D::PaintWorldTexts( QPainter &painter )
{
	const xv::map::CScene *pScene = GetDocument().GetScene();
	if ( !pScene )
		return;
	std::vector<std::size_t> texts;
	for ( std::size_t e = 0; e < pScene->m_Map.m_Entities.size(); e++ )
	{
		const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ e ];
		if ( entity.m_bDeleted || !pScene->m_Entities[ e ].m_bPoint )
			continue;
		const std::string_view szClass = entity.GetClassname();
		bool bText = xv::kv1::EqualsIgnoreCase( szClass, "editor_text" ) || xv::kv1::EqualsIgnoreCase( szClass, "point_worldtext" );
		if ( !bText && pScene->m_pFgd )
			if ( const xv::map::FgdClass_t *pClass = pScene->m_pFgd->Find( szClass ) )
				bText = pClass->FindHelper( "worldtext" ) != nullptr;
		if ( bText && GetDocument().IsShown( pScene->EntityObject( e ) ) )
			texts.push_back( e );
	}
	if ( texts.empty() )
	{
		m_WorldTexts.clear();
		return;
	}
	const xv::map::CViewCamera camera = GetSizedCamera();
	const MapFrame_t frame = MakeFrame( size() );
	const xv::map::RasterScene_t scene = frame.MakeScene();
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	const auto number = [ & ]( const xv::map::Entity_t &entity, std::string_view szKey, double flDefault ) {
		return xv::ParseNumber<double>( entity.GetValue( szKey ) ).value_or( flDefault );
	};
	// Pictures of texts no longer shown go.
	std::erase_if( m_WorldTexts, [ & ]( const auto &entry ) { return !std::ranges::binary_search( texts, entry.first ); } );
	painter.save();
	painter.setRenderHint( QPainter::Antialiasing, true );
	for ( const std::size_t e : texts )
	{
		const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ e ];
		const QString szMessage = QString::fromUtf8( entity.HasKey( "message" ) ? entity.GetValue( "message" ) : std::string_view{ "Hello World" } );
		if ( szMessage.isEmpty() )
			continue;
		const xv::map::Point_t origin = pScene->m_Entities[ e ].m_Origin;
		const xv::map::Point_t toText = xv::map::Subtract( origin, camera.m_Eye );
		const double flDistance = xv::map::Length( toText );
		if ( flDistance < 1.0 || flDistance > camera.m_flFar )
			continue;
		// Which way the text reads and which way is up.
		xv::map::Point_t reading = camera.GetRight();
		xv::map::Point_t up = camera.GetUp();
		const int nOrientation = static_cast<int>( number( entity, "orientation", 0.0 ) );
		if ( nOrientation == 2 )
		{
			up = { 0.0, 0.0, 1.0 };
		}
		else if ( nOrientation != 1 )
		{
			std::vector<double> angles;
			if ( !xv::map::ParseNumbers( entity.GetValue( "angles" ), 3, angles ) )
				angles = { 0.0, 0.0, 0.0 };
			const xv::map::Transform_t turn = xv::map::Transform_t::FromAngles( {}, { angles[ 0 ], angles[ 1 ], angles[ 2 ] } );
			reading = turn.Rotate( { 0, -1, 0 } ); // Source's right: its left turned around
			up = turn.Rotate( { 0, 0, 1 } );
		}
		// Something drawn in front of its origin hides it.
		bool bHidden = false;
		const xv::map::Point_t dir = xv::map::Scale( toText, 1.0 / flDistance );
		for ( const xv::map::TraceHit_t &hit : xv::map::TraceScene( scene, settings.m_bToolTextures, settings.m_bNodraw, camera.m_Eye, dir, flDistance - 1.0 ) )
		{
			if ( hit.m_nObject != pScene->EntityObject( e ) )
			{
				bHidden = true;
				break;
			}
		}
		if ( bHidden )
			continue;
		const double flSize = number( entity, "textsize", 10.0 );
		if ( std::abs( flSize ) < 1e-3 )
			continue;
		// The face and its look: Hammer++'s editor_text is bold monospace letters outlined in
		// black; point_worldtext's font key picks one of the game's.
		const bool bEditorText = xv::kv1::EqualsIgnoreCase( entity.GetClassname(), "editor_text" );
		WorldTextLook_t look;
		QFont lettered;
		double flSheet = WORLD_TEXT_PIXELS;
		if ( bEditorText )
		{
			lettered = QFont( QStringLiteral( "Courier New" ) );
			lettered.setStyleHint( QFont::Monospace );
			lettered.setBold( true );
		}
		else
		{
			look = WorldTextLookOf( entity.HasKey( "font" ) ? entity.GetValue( "font" ) : std::string_view{ "0" } );
			const WorldTextFace_t &face = WorldTextFaces()[ static_cast<std::size_t>( look.m_nFace ) ];
			lettered.setFamilies( face.m_Families );
			lettered.setStyleHint( face.m_nHint );
			lettered.setBold( face.m_bBold );
			if ( face.m_bSmallCaps )
				lettered.setCapitalization( QFont::SmallCaps );
			flSheet = face.m_flSheetSize;
		}
		lettered.setPixelSize( WORLD_TEXT_PIXELS );
		const double flSheetPixel = WORLD_TEXT_PIXELS / flSheet; // one of the sheet's pixels, in the layout's
		const double flScale = flSize / WORLD_TEXT_PIXELS;           // units a layout pixel
		const double flSpacingX = number( entity, "textspacingx", 0.0 ) * flSheetPixel;
		const double flLineStep = WORLD_TEXT_PIXELS + number( entity, "textspacingy", 0.0 ) * flSheetPixel;
		const bool bCentred = nOrientation == 1 || nOrientation == 2;
		// color is r g b, and a when given; rainbow gives each letter a hue instead, moving.
		std::vector<double> rgba;
		QColor color = Qt::white;
		if ( const std::string_view szColor = entity.GetValue( "color" ); xv::map::ParseNumbers( szColor, 4, rgba ) || xv::map::ParseNumbers( szColor, 3, rgba ) )
			color = QColor( std::clamp( static_cast<int>( rgba[ 0 ] ), 0, 255 ), std::clamp( static_cast<int>( rgba[ 1 ] ), 0, 255 ), std::clamp( static_cast<int>( rgba[ 2 ] ), 0, 255 ), rgba.size() > 3 ? std::clamp( static_cast<int>( rgba[ 3 ] ), 0, 255 ) : 255 );
		const bool bRainbow = !look.m_Fixed.isValid() && number( entity, "rainbow", 0.0 ) != 0.0;
		if ( look.m_Fixed.isValid() )
			color = QColor( look.m_Fixed.red(), look.m_Fixed.green(), look.m_Fixed.blue(), color.alpha() );
		const double flRamp = WORLD_TEXT_SHEET_RAMP * flSheetPixel;
		// Laid out again only when what it is laid out from changes.
		WorldTextPicture_t &picture = m_WorldTexts[ e ];
		const QString szKey = QStringLiteral( "%1|%2|%3|%4|%5|%6|%7|%8|%9" ).arg( szMessage, lettered.toString() ).arg( flSpacingX ).arg( flLineStep ).arg( bCentred ).arg( color.rgba() ).arg( bRainbow ).arg( bEditorText ).arg( QStringLiteral( "%1 %2 %3 %4" ).arg( look.m_Outline.rgba() ).arg( look.m_bShadow ).arg( look.m_Glow.rgba() ).arg( flRamp ) );
		if ( picture.m_szKey != szKey )
		{
			picture = WorldTextPicture_t{};
			picture.m_szKey = szKey;
			// Each letter at its pen position (no kerning: the game places letter by letter), and
			// its place in the message, which a rainbow's hue counts.
			const QFontMetricsF letters( lettered );
			const QString szText = QString( szMessage ).replace( QStringLiteral( "\\n" ), QStringLiteral( "\n" ) );
			double flPen = 0.0, flBaseline = 0.0, flWidth = 0.0;
			for ( qsizetype i = 0; i < szText.size(); i++ )
			{
				const QChar ch = szText[ i ];
				if ( ch == QLatin1Char( '\n' ) )
				{
					flWidth = std::max( flWidth, flPen );
					flPen = 0.0;
					flBaseline += flLineStep;
					continue;
				}
				if ( !ch.isSpace() )
				{
					QPainterPath letter;
					letter.addText( QPointF{ flPen, flBaseline }, lettered, QString( ch ) );
					picture.m_Letters.emplace_back( std::move( letter ), i );
				}
				flPen += letters.horizontalAdvance( ch ) + flSpacingX;
			}
			flWidth = std::max( flWidth, flPen );
			const double flLeft = bCentred ? -flWidth / 2.0 : 0.0;
			for ( auto &[ letter, nIndex ] : picture.m_Letters )
			{
				letter.translate( flLeft, 0.0 );
				picture.m_Path.addPath( letter );
			}
			picture.m_Bounds = QRectF{ flLeft, -letters.ascent(), std::max( flWidth, 1.0 ), letters.ascent() + flBaseline + letters.descent() };
			const double flMargin = std::max( flRamp, EDITOR_TEXT_OUTLINE * WORLD_TEXT_PIXELS ) + 2.0;
			picture.m_Area = picture.m_Bounds.adjusted( -flMargin, -flMargin, flMargin, flMargin );
		}
		const QRectF &bounds = picture.m_Bounds;
		const auto corner = [ & ]( double x, double y ) { return xv::map::Add( origin, xv::map::Add( xv::map::Scale( reading, x * flScale ), xv::map::Scale( up, -y * flScale ) ) ); };
		const std::array<xv::map::Point_t, 4> world{ corner( bounds.left(), bounds.top() ), corner( bounds.right(), bounds.top() ), corner( bounds.right(), bounds.bottom() ), corner( bounds.left(), bounds.bottom() ) };
		QPolygonF screen;
		for ( const xv::map::Point_t &point : world )
		{
			const auto projected = camera.Project( point );
			if ( !projected )
				break;
			screen << QPointF{ ( *projected )[ 0 ], ( *projected )[ 1 ] };
		}
		if ( screen.size() != 4 || screen.boundingRect().height() < 2.0 )
			continue;
		QTransform transform;
		if ( !QTransform::quadToQuad( QPolygonF( bounds ), screen, transform ) )
			continue;
		// The pictures' resolution follows the size on screen, in steps of two, so they stay sharp
		// close up and small far away.
		const auto length = []( QPointF d ) { return std::hypot( d.x(), d.y() ); };
		const double flOnScreen = std::max( { length( screen[ 1 ] - screen[ 0 ] ) / bounds.width(), length( screen[ 2 ] - screen[ 3 ] ) / bounds.width(), length( screen[ 3 ] - screen[ 0 ] ) / bounds.height(), length( screen[ 2 ] - screen[ 1 ] ) / bounds.height() } );
		double flRaster = std::exp2( std::ceil( std::log2( std::max( flOnScreen, 1e-3 ) ) ) );
		flRaster = std::clamp( flRaster, 1.0 / 16.0, 2.0 );
		while ( flRaster > 1.0 / 16.0 && std::max( picture.m_Area.width(), picture.m_Area.height() ) * flRaster > 4096.0 )
			flRaster /= 2.0;
		const QSize pictureSize( std::max( 1, static_cast<int>( std::ceil( picture.m_Area.width() * flRaster ) ) ), std::max( 1, static_cast<int>( std::ceil( picture.m_Area.height() * flRaster ) ) ) );
		const auto begin = [ & ]( QImage &image, QPainter &into ) {
			image.fill( Qt::transparent );
			into.begin( &image );
			into.setRenderHint( QPainter::Antialiasing, true );
			into.scale( flRaster, flRaster );
			into.translate( -picture.m_Area.topLeft() );
		};
		if ( picture.m_flRaster != flRaster )
		{
			picture.m_flRaster = flRaster;
			picture.m_flFillAt = -1.0;
			picture.m_Effects = QImage( pictureSize, QImage::Format_ARGB32_Premultiplied );
			picture.m_Fill = QImage( pictureSize, QImage::Format_ARGB32_Premultiplied );
			QPainter into;
			begin( picture.m_Effects, into );
			// A soft halo out from the letters' edges: strokes narrowing as they darken.
			const auto halo = [ & ]( const QColor &tint, double flReach, double flAlpha ) {
				for ( int n = 4; n >= 1; n-- )
				{
					QColor step = tint;
					step.setAlphaF( static_cast<float>( flAlpha / 4.0 * color.alphaF() ) );
					into.strokePath( picture.m_Path, QPen( step, 2.0 * flReach * n / 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
				}
			};
			if ( look.m_bShadow )
				halo( QColor( 0, 0, 0 ), flRamp, 0.45 );
			if ( look.m_Glow.isValid() )
				halo( look.m_Glow, flRamp * 0.85, 1.0 );
			if ( bEditorText )
				into.strokePath( picture.m_Path, QPen( QColor( 0, 0, 0, color.alpha() ), 2.0 * EDITOR_TEXT_OUTLINE * WORLD_TEXT_PIXELS, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
			else if ( look.m_Outline.isValid() )
				into.strokePath( picture.m_Path, QPen( QColor( look.m_Outline.red(), look.m_Outline.green(), look.m_Outline.blue(), color.alpha() ), 2.0 * 0.43 * flRamp, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
			into.end();
		}
		const double flHueAt = bRainbow ? std::fmod( m_Editor.GetRenderData().GetFxTime() * WORLD_TEXT_RAINBOW_DEGREES_A_SECOND, 360.0 ) : 0.0;
		if ( picture.m_flFillAt != flHueAt )
		{
			picture.m_flFillAt = flHueAt;
			QPainter into;
			begin( picture.m_Fill, into );
			if ( !bRainbow )
			{
				into.fillPath( picture.m_Path, color );
			}
			else
			{
				for ( const auto &[ letter, nIndex ] : picture.m_Letters )
				{
					const double flHue = std::fmod( flHueAt + static_cast<double>( nIndex ) * WORLD_TEXT_RAINBOW_LETTER_DEGREES, 360.0 );
					into.fillPath( letter, QColor::fromHsvF( static_cast<float>( flHue / 360.0 ), 0.7f, 1.0f, color.alphaF() ) );
				}
			}
			into.end();
		}
		painter.setTransform( transform );
		painter.setRenderHint( QPainter::SmoothPixmapTransform, true );
		painter.drawImage( picture.m_Area, picture.m_Effects );
		painter.drawImage( picture.m_Area, picture.m_Fill );
		// Selected, its rectangle is outlined, as Hammer++ boxes it.
		if ( GetDocument().IsSelected( pScene->EntityObject( e ) ) )
		{
			painter.resetTransform();
			painter.setPen( QPen( CPreferences::Instance().GetMapPreferences().m_Colors.m_ToolSelection, 1.0 ) );
			painter.setBrush( Qt::NoBrush );
			painter.drawPolygon( screen );
		}
	}
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::paintEvent( QPaintEvent * )
{
	if ( !m_pGpu )
		PaintFrameSubmitted();
	if ( m_pGpu )
	{
		return; // the card draws, the overlay labels
	}
	QPainter painter( this );
	if ( m_Image.isNull() )
	{
		painter.fillRect( rect(), CPreferences::Instance().GetMapPreferences().m_Colors.m_Background3D );
	}
	else
	{
		painter.setRenderHint( QPainter::SmoothPixmapTransform, false );
		painter.drawImage( rect(), m_Image );
	}
	PaintLabels( painter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::RedrawLabels()
{
	if ( m_pOverlay )
	{
		m_pOverlay->update();
	}
	else
	{
		update();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The world's axes at 0 0 0, each a short line in its colour, over the picture so they
//          show through walls: where the map's origin is, and which way each axis runs there.
//-----------------------------------------------------------------------------
void CMapView3D::PaintOriginAxes( QPainter &painter ) const
{
	if ( !m_Editor.GetSettings().m_bOriginAxes )
	{
		return;
	}
	const xv::map::CViewCamera camera = GetSizedCamera();
	const std::array<std::pair<xv::map::Point_t, QColor>, 3> axes{ { { { mapstyle::ORIGIN_AXIS_LENGTH, 0, 0 }, mapstyle::AXIS_X }, { { 0, mapstyle::ORIGIN_AXIS_LENGTH, 0 }, mapstyle::AXIS_Y }, { { 0, 0, mapstyle::ORIGIN_AXIS_LENGTH }, mapstyle::AXIS_Z } } };
	painter.save();
	painter.setRenderHint( QPainter::Antialiasing, true );
	for ( const auto &[ tip, color ] : axes )
	{
		if ( const auto projected = camera.ProjectLine( { 0, 0, 0 }, tip ) )
		{
			painter.setPen( QPen{ color, mapstyle::ORIGIN_AXIS_WIDTH, Qt::SolidLine, Qt::RoundCap } );
			painter.drawLine( QPointF{ projected->first[ 0 ], projected->first[ 1 ] }, QPointF{ projected->second[ 0 ], projected->second[ 1 ] } );
		}
	}
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: Which way each shown point entity near the camera faces, from its angles (or angle):
//          its own forward (long), left and up (short) in the axes' colours, so a turn that
//          went wrong shows. A light shows where the compile aims it instead, which reads the
//          yaw and the pitch key and leaves roll out (map/Lighting's LightDirection), with its
//          angles' forward dashed beside it when the two part: what turning a light in Hammer
//          leaves behind.
//-----------------------------------------------------------------------------
void CMapView3D::PaintEntityAngles( QPainter &painter ) const
{
	const xv::map::CScene *pScene = GetDocument().GetScene();
	if ( !m_Editor.GetSettings().m_bEntityAngles || !pScene )
		return;
	const xv::map::CViewCamera camera = GetSizedCamera();
	const auto line = [ & ]( const xv::map::Point_t &from, const xv::map::Point_t &to, const QPen &pen ) {
		if ( const auto projected = camera.ProjectLine( from, to ) )
		{
			painter.setPen( pen );
			painter.drawLine( QPointF{ projected->first[ 0 ], projected->first[ 1 ] }, QPointF{ projected->second[ 0 ], projected->second[ 1 ] } );
		}
	};
	const auto arrow = [ & ]( const xv::map::Point_t &from, const xv::map::Point_t &dir, double flLength, const QPen &pen ) {
		const xv::map::Point_t tip = xv::map::Add( from, xv::map::Scale( dir, flLength ) );
		line( from, tip, pen );
		const xv::map::Point_t u = xv::map::Normalized( xv::map::Cross( dir, std::abs( dir[ 2 ] ) < 0.9 ? xv::map::Point_t{ 0, 0, 1 } : xv::map::Point_t{ 1, 0, 0 } ) );
		const xv::map::Point_t v = xv::map::Cross( dir, u );
		const xv::map::Point_t back = xv::map::Subtract( tip, xv::map::Scale( dir, flLength / 4.0 ) );
		for ( const xv::map::Point_t &side : { u, xv::map::Scale( u, -1.0 ), v, xv::map::Scale( v, -1.0 ) } )
			line( tip, xv::map::Add( back, xv::map::Scale( side, flLength / 8.0 ) ), pen );
	};
	painter.save();
	painter.setRenderHint( QPainter::Antialiasing, true );
	const double flLength = mapstyle::ENTITY_AXIS_LENGTH;
	for ( std::size_t e = 0; e < pScene->m_Map.m_Entities.size(); e++ )
	{
		const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ e ];
		if ( entity.m_bDeleted || !pScene->m_Entities[ e ].m_bPoint || !GetDocument().IsShown( pScene->EntityObject( e ) ) )
			continue;
		const std::string_view szClass = entity.GetClassname();
		const bool bLight = szClass.size() >= 5 && xv::kv1::EqualsIgnoreCase( szClass.substr( 0, 5 ), "light" ) && ( entity.HasKey( "pitch" ) || entity.HasKey( "_cone" ) );
		if ( !bLight && !entity.HasKey( "angles" ) && !entity.HasKey( "angle" ) )
			continue;
		const xv::map::Point_t origin = pScene->m_Entities[ e ].m_Origin;
		if ( xv::map::Length( xv::map::Subtract( origin, camera.m_Eye ) ) > mapstyle::ENTITY_AXIS_RANGE )
			continue;
		std::array<double, 3> angles{};
		std::vector<double> parsed;
		if ( xv::map::ParseNumbers( entity.GetValue( "angles" ), 3, parsed ) )
			angles = { parsed[ 0 ], parsed[ 1 ], parsed[ 2 ] };
		else if ( const auto flAngle = xv::ParseNumber<double>( entity.GetValue( "angle" ) ) )
			angles = *flAngle == -1.0 ? std::array<double, 3>{ -90.0, 0.0, 0.0 } : *flAngle == -2.0 ? std::array<double, 3>{ 90.0, 0.0, 0.0 }
																									: std::array<double, 3>{ 0.0, *flAngle, 0.0 };
		const xv::map::Transform_t turn = xv::map::Transform_t::FromAngles( {}, angles );
		const xv::map::Point_t forward = turn.Rotate( { 1, 0, 0 } );
		if ( bLight )
		{
			const xv::map::Point_t aim = xv::map::Normalized( xv::map::LightDirection( entity ) );
			arrow( origin, aim, flLength * 1.5, QPen{ mapstyle::LIGHT_AIM, 2.0, Qt::SolidLine, Qt::RoundCap } );
			if ( xv::map::Dot( aim, forward ) < std::cos( std::numbers::pi / 180.0 ) )
				line( origin, xv::map::Add( origin, xv::map::Scale( forward, flLength * 1.5 ) ), QPen{ mapstyle::AXIS_X, 1.5, Qt::DashLine, Qt::RoundCap } );
			continue;
		}
		arrow( origin, forward, flLength, QPen{ mapstyle::AXIS_X, 2.0, Qt::SolidLine, Qt::RoundCap } );
		line( origin, xv::map::Add( origin, xv::map::Scale( turn.Rotate( { 0, 1, 0 } ), flLength / 2.0 ) ), QPen{ mapstyle::AXIS_Y, 2.0, Qt::SolidLine, Qt::RoundCap } );
		line( origin, xv::map::Add( origin, xv::map::Scale( turn.Rotate( { 0, 0, 1 } ), flLength / 2.0 ) ), QPen{ mapstyle::AXIS_Z, 2.0, Qt::SolidLine, Qt::RoundCap } );
	}
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: What goes over the picture: the statistics (O) and the focus frame.
//-----------------------------------------------------------------------------
void CMapView3D::PaintLabels( QPainter &painter )
{
	m_Editor.GetBrushEdit().Paint( *this, painter );
	if ( m_bStatistics )
	{
		// Hammer++'s statistics, with where each frame's time goes: the overlay and the debug
		// log (once a second) say the same.
		const MapRenderStats_t stats = GetStats();
		QStringList lines;
		if ( m_pGpu )
		{
			lines << tr( "%1 (%2)" ).arg( GraphicsApiName( CGraphics::Instance().GetActive() ), m_pGpu->GetBackendName() );
			lines << tr( "%1 ms between frames (%2 a second)" ).arg( stats.m_flIntervalMs, 0, 'f', 1 ).arg( stats.m_flIntervalMs > 0.0 ? 1000.0 / stats.m_flIntervalMs : 0.0, 0, 'f', 0 );
			lines << tr( "%1 ms on the main thread: %2 frame, %3 upload, %4 record" ).arg( stats.m_flFrameMs, 0, 'f', 1 ).arg( stats.m_flSnapshotMs, 0, 'f', 1 ).arg( stats.m_flUploadMs, 0, 'f', 1 ).arg( stats.m_flRecordMs, 0, 'f', 1 );
			if ( stats.m_flGpuMs >= 0.0 )
			{
				lines << tr( "%1 ms on the card" ).arg( stats.m_flGpuMs, 0, 'f', 1 );
			}
			lines << tr( "%1 draw calls, %2 triangles%3" ).arg( stats.m_nDrawCalls ).arg( stats.m_nTriangles ).arg( stats.m_nUploads ? tr( ", %n mesh(es) uploaded", nullptr, stats.m_nUploads ) : QString{} );
		}
		else
		{
			lines << tr( "software: %1 ms a frame%2" ).arg( m_flRenderMs, 0, 'f', 1 ).arg( m_bReduced ? tr( " (half size while moving)" ) : QString{} );
		}
		if ( m_PaintClock.isValid() )
		{
			lines << GetPaintCosts();
		}
		const bool bLog = !m_LogClock.isValid() || m_LogClock.elapsed() > 1000;
		lines << CMapEventClock::Get().Describe();
		for ( CMapView *pView : m_Editor.GetViews() )
		{
			if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
			{
				lines << tr( "%1: %2 ms a repaint (%3 patch, %4 layer), %5 repaints" ).arg( CMapPane::TypeName( p2D->GetType() ) ).arg( p2D->GetLastPaintTime(), 0, 'f', 1 ).arg( p2D->GetLastPatchTime(), 0, 'f', 2 ).arg( p2D->GetLastLayerTime(), 0, 'f', 2 ).arg( p2D->GetPaintCount() ) + QStringLiteral( "; " ) + p2D->DescribeLayerChanges( bLog );
			}
		}
		lines << GetDocument().GetEditListenerCosts();
		const QString szText = lines.join( QLatin1Char( '\n' ) );
		const QRect text = painter.fontMetrics().boundingRect( rect().adjusted( 0, 4, -8, 0 ), Qt::AlignRight | Qt::AlignTop, szText );
		painter.fillRect( text.adjusted( -6, -3, 6, 3 ), mapstyle::OVERLAY_BACKING );
		painter.setPen( Qt::white );
		painter.drawText( rect().adjusted( 0, 4, -8, 0 ), Qt::AlignRight | Qt::AlignTop, szText );
		if ( bLog )
		{
			m_LogClock.start();
			DebugLog() << "map 3D view:" << lines.join( QStringLiteral( "; " ) );
		}
	}
	PaintWorldTexts( painter );
	PaintGameTexts( painter );
	PaintOriginAxes( painter );
	PaintEntityAngles( painter );
	PaintTools( painter );
	PaintPortals( painter );
	// The axes as the camera sees them, in the bottom left corner: which way is which.
	{
		const xv::map::Point_t right = m_Camera.GetRight();
		const xv::map::Point_t up = m_Camera.GetUp();
		const xv::map::Point_t forward = m_Camera.GetForward();
		const QPointF centre{ static_cast<double>( mapstyle::GIZMO_MARGIN + mapstyle::GIZMO_SIZE ), static_cast<double>( height() - mapstyle::GIZMO_MARGIN - mapstyle::GIZMO_SIZE ) };
		struct Axis_t
		{
			xv::map::Point_t m_Direction;
			QColor m_Color;
			QChar m_chName;
		};
		std::array<Axis_t, 3> axes{ { { { 1, 0, 0 }, mapstyle::AXIS_X, QLatin1Char( 'x' ) }, { { 0, 1, 0 }, mapstyle::AXIS_Y, QLatin1Char( 'y' ) }, { { 0, 0, 1 }, mapstyle::AXIS_Z, QLatin1Char( 'z' ) } } };
		// The one pointing furthest away first, so the nearer ones lie over it.
		std::ranges::sort( axes, [ & ]( const Axis_t &a, const Axis_t &b ) { return xv::map::Dot( a.m_Direction, forward ) > xv::map::Dot( b.m_Direction, forward ); } );
		painter.setRenderHint( QPainter::Antialiasing, true );
		for ( const Axis_t &axis : axes )
		{
			const QPointF tip = centre + QPointF{ xv::map::Dot( axis.m_Direction, right ), -xv::map::Dot( axis.m_Direction, up ) } * mapstyle::GIZMO_SIZE;
			painter.setPen( QPen{ axis.m_Color, 2.0 } );
			painter.drawLine( centre, tip );
			const QPointF label = centre + ( tip - centre ) * 1.35;
			painter.drawText( QRectF{ label.x() - 8.0, label.y() - 8.0, 16.0, 16.0 }, Qt::AlignCenter, QString( axis.m_chName ) );
		}
		painter.setRenderHint( QPainter::Antialiasing, false );
	}
	if ( hasFocus() )
	{
		painter.setPen( QPen{ palette().highlight().color(), 0 } );
		painter.drawRect( rect().adjusted( 0, 0, -1, -1 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::resizeEvent( QResizeEvent *pEvent )
{
	CMapView::resizeEvent( pEvent );
	if ( m_pGpu )
	{
		m_pGpu->setGeometry( rect() );
	}
	if ( m_pOverlay )
	{
		m_pOverlay->setGeometry( rect() );
	}
	RequestRender();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's mouselook (Z, or Space held): the mouse turns the camera without a button.
//-----------------------------------------------------------------------------
void CMapView3D::SetMouselook( bool bOn )
{
	if ( bOn == m_bMouselook )
	{
		return;
	}
	m_bMouselook = bOn;
	if ( bOn )
	{
		// A drag begun with Space (or both buttons) while it is on moves the camera instead of
		// turning it: a pinned pointer's movement only comes through here.
		CapturePointer( mapFromGlobal( QCursor::pos() ), [ this ]( QPointF delta ) {
			if ( m_bSpaceDrag )
				DragCamera( m_Dragging, delta );
			else
				Look( delta );
		} );
	}
	else
	{
		ReleasePointer();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Hides the cursor and keeps it from leaving the view while the camera turns or
//          moves with the mouse: the pointer is pinned (Wayland), or warped back to the middle
//          at each move (WarpToMiddle), so it never reaches an edge.
//-----------------------------------------------------------------------------
void CMapView3D::CapturePointer( QPoint at, std::function<void( QPointF )> pfnMoved )
{
	setCursor( Qt::BlankCursor );
	// The cursor goes back here afterwards.
	m_LookFrom = at;
	m_LastMouse = m_LookFrom;
	grabMouse();
	m_PointerLock.Lock( this, std::move( pfnMoved ) );
}

//-----------------------------------------------------------------------------
// Purpose: Lets the pointer go, with the cursor back where the capture began.
//-----------------------------------------------------------------------------
void CMapView3D::ReleasePointer()
{
	releaseMouse();
	if ( m_PointerLock.IsActive() )
	{
		m_PointerLock.Unlock( m_LookFrom );
	}
	else
	{
		QCursor::setPos( mapToGlobal( m_LookFrom ) );
	}
	UpdateCursor();
}

//-----------------------------------------------------------------------------
// Purpose: Puts a captured pointer back in the view's middle, when it isn't pinned.
//-----------------------------------------------------------------------------
void CMapView3D::WarpToMiddle()
{
	const QPoint middle{ width() / 2, height() / 2 };
	QCursor::setPos( mapToGlobal( middle ) );
	m_LastMouse = middle;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::Look( QPointF delta )
{
	if ( delta.isNull() )
	{
		return; // the move the cursor's own warp sends
	}
	const double flSensitivity = CPreferences::Instance().GetMapPreferences().m_flSensitivity;
	m_Camera.m_flYaw -= delta.x() * flSensitivity;
	m_Camera.m_flPitch = std::clamp( m_Camera.m_flPitch + delta.y() * flSensitivity, -89.0, 89.0 );
	Changed();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::mousePressEvent( QMouseEvent *pEvent )
{
	// Alt and a right drag orbit the point under the pointer (the texture tool and the
	// displacement brush keep Alt and the right button for themselves).
	if ( pEvent->button() == Qt::RightButton && pEvent->modifiers() == Qt::AltModifier && !m_bOrbit && !m_bSpaceDrag && !m_bMouselook && !m_ModalTransform && !IsPaintingDisplacements() && m_Editor.GetTool() != MapTool_t::MAP_TOOL_TEXTURE )
	{
		setFocus( Qt::MouseFocusReason );
		BeginOrbit( pEvent->position() );
		pEvent->accept();
		return;
	}
	// With the preference, left and right held together move the camera whatever the tool: what
	// the first button began is let go, and neither button's release reaches the tool.
	const Qt::MouseButtons both = Qt::LeftButton | Qt::RightButton;
	if ( CPreferences::Instance().GetMapPreferences().m_bBothButtonsStrafe && !m_bSpaceDrag && !m_ModalTransform && ( pEvent->buttons() & both ) == both )
	{
		CancelDrags();
		m_bMenuPending = false;
		m_bBothDrag = true;
		m_bSpaceDrag = true;
		m_Dragging = pEvent->buttons();
		m_PressAt = pEvent->position().toPoint();
		m_bMoved = false;
		if ( !m_bMouselook )
			CapturePointer( m_PressAt, [ this ]( QPointF delta ) { DragCamera( m_Dragging, delta ); } );
		pEvent->accept();
		return;
	}
	// With Space held the buttons move the camera, as the camera tool's do: left looks, right
	// pans, both go forward and back. No tool sees the click.
	if ( ( m_bSpaceHeld || m_bSpaceDrag ) && !m_ModalTransform && ( pEvent->button() == Qt::LeftButton || pEvent->button() == Qt::RightButton ) )
	{
		setFocus( Qt::MouseFocusReason );
		m_Dragging = pEvent->buttons();
		if ( !m_bSpaceDrag )
		{
			m_bSpaceDrag = true;
			m_PressAt = pEvent->position().toPoint();
			m_bMoved = false;
			// Mouselook (Z) already holds the pointer.
			if ( !m_bMouselook )
				CapturePointer( m_PressAt, [ this ]( QPointF delta ) { DragCamera( m_Dragging, delta ); } );
		}
		pEvent->accept();
		return;
	}
	if ( m_Editor.GetBrushEdit().HandleEvent( *this, pEvent ) )
	{
		pEvent->accept();
		return;
	}
	setFocus( Qt::MouseFocusReason );
	if ( m_ModalTransform && ( pEvent->button() == Qt::LeftButton || pEvent->button() == Qt::RightButton ) )
	{
		m_ModalTransform->MovePointer( pEvent->position(), pEvent->modifiers().testFlag( Qt::ShiftModifier ) );
		PreviewModalTransform( pEvent->modifiers() );
		EndModalTransform( pEvent->button() == Qt::LeftButton );
		m_bConsumeModalRelease = true;
		return;
	}
	m_Dragging = pEvent->buttons();
	m_LastMouse = pEvent->position().toPoint();
	m_PressAt = m_LastMouse;
	m_bMoved = false;
	if ( pEvent->button() == Qt::LeftButton && !m_bSpaceHeld && !m_bMouselook && m_Editor.GetTool() == MapTool_t::MAP_TOOL_OVERLAY && m_Editor.GetFacePickKey().isEmpty() && !pEvent->modifiers().testFlag( Qt::ShiftModifier ) && BeginOverlayDrag( pEvent->position() ) )
		return;
	// The block tool's box: a face's arrow sizes it, and a drag on the box itself slides it
	// across the face pressed.
	if ( pEvent->button() == Qt::LeftButton && !m_bSpaceHeld && !m_bMouselook && m_Editor.GetTool() == MapTool_t::MAP_TOOL_BLOCK && m_Editor.GetBlockBox() && !m_Editor.GetBlockBox()->IsEmpty() )
	{
		if ( const int nFace = BoxHandleAt( pEvent->position() ); nFace >= 0 )
		{
			m_nBoxFace = nFace;
			m_BoxFrom = m_Editor.GetBlockBox();
			RedrawLabels();
			return;
		}
		if ( const auto surface = BoxSurfaceAt( pEvent->position() ) )
		{
			m_nBoxPlane = surface->first / 2;
			m_BoxGrab = surface->second;
			m_BoxFrom = m_Editor.GetBlockBox();
			UpdateCursor();
			return;
		}
	}
	if ( pEvent->button() == Qt::LeftButton && !m_bSpaceHeld && !m_bMouselook && m_Editor.GetTool() == MapTool_t::MAP_TOOL_BLOCK && !m_Editor.IsWorldGridEdited() && BeginBlockDrag( pEvent->position() ) )
	{
		return;
	}
	// On one of the gizmo's axes, the selection tool drags the selection along it; the world
	// grid's gizmo drags its plane, whatever the tool.
	if ( pEvent->button() == Qt::LeftButton && !m_bSpaceHeld && !m_bMouselook && ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT || m_Editor.IsWorldGridEdited() ) )
	{
		if ( const std::optional<CMapGizmo> gizmo = GetGizmo() )
		{
			const MapPreferences_t &look = CPreferences::Instance().GetMapPreferences();
			const int nAxis = gizmo->HitAt( GetSizedCamera(), look.m_flGizmoPixels, pEvent->position(), mapstyle::GIZMO_SLACK * std::max( 1.0, look.m_flGizmoThickness ) );
			if ( nAxis >= 0 )
			{
				m_nGizmoDrag = nAxis;
				m_GizmoFrom = pEvent->position();
				m_DragGizmo = gizmo;
				if ( m_Editor.IsWorldGridEdited() )
				{
					m_GridFrom = m_Editor.GetSettings().m_WorldGrid;
					UpdateCursor();
					return;
				}
				m_pGizmoScene = GetDocument().GetSharedScene();
				m_GizmoSelection = GetDocument().GetSelection();
				UpdateCursor();
			}
		}
	}
	// The sprinkle tool paints with the left button; the right one drags the radius.
	if ( IsSprinkling() && !m_bSpaceHeld && !m_bMouselook )
	{
		if ( pEvent->button() == Qt::RightButton )
		{
			m_RadiusFrom = pEvent->position().toPoint();
			m_flRadiusFrom = GetBrushRadius();
			m_flDensityFrom = m_Editor.GetSprinkle().GetDensity();
			return;
		}
		if ( pEvent->button() == Qt::LeftButton )
		{
			BeginSprinkle( pEvent->position(), pEvent->modifiers() );
			return;
		}
	}
	// The Displacement tab's brush: a press starts a stroke on the displacements under it (the
	// selected ones, else the one hit); the right button lowers.
	// Alt with the brush: Alt+right-click takes the face's normal as the axis, Alt+drag sizes it.
	if ( IsPaintingDisplacements() && pEvent->modifiers().testFlag( Qt::AltModifier ) && !m_bSpaceHeld && !m_bMouselook )
	{
		CMapFaceEdit &sheet = m_Editor.GetFaceEdit();
		if ( pEvent->button() == Qt::RightButton )
		{
			if ( const auto face = FaceAt( pEvent->position() ) )
			{
				if ( const xv::map::FaceGeometry_t *pFace = GetDocument().GetScene()->m_Solids[ face->m_nSolid ].FindFace( face->m_nSide ) )
				{
					sheet.SetPaintNormal( pFace->m_Plane.m_Normal );
				}
			}
			return;
		}
		if ( pEvent->button() == Qt::LeftButton )
		{
			m_RadiusFrom = pEvent->position().toPoint();
			m_flRadiusFrom = sheet.GetBrushRadius();
			return;
		}
	}
	if ( IsPaintingDisplacements() && m_Editor.GetFaceEdit().GetDispPaintMode() == DispPaintMode_t::DISP_PAINT_SCULPT && pEvent->button() == Qt::RightButton && !m_bSpaceHeld && !m_bMouselook )
	{
		m_RadiusFrom = pEvent->position().toPoint();
		m_flRadiusFrom = m_Editor.GetFaceEdit().GetBrushRadius();
		return;
	}
	if ( IsPaintingDisplacements() && ( pEvent->button() == Qt::LeftButton || pEvent->button() == Qt::RightButton ) && !m_bSpaceHeld && !m_bMouselook )
	{
		const std::vector<xv::map::FaceRef_t> faces = m_Editor.GetPaintFaces( FaceAt( pEvent->position() ) );
		if ( !faces.empty() )
		{
			m_PaintClock.start();
			QElapsedTimer start;
			start.start();
			m_nPaintSamples = m_nPaintPreviews = 0;
			m_bPaintPending = m_bPaintPreviewUsed = false;
			m_pPaintScene = GetDocument().GetSharedScene();
			m_Painted.clear();
			m_PaintChanged.clear();
			for ( const xv::map::FaceRef_t &face : faces )
			{
				m_Painted.emplace_back( face, *m_pPaintScene->m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ].m_pDisp );
			}
			std::vector<std::uint32_t> objects;
			for ( const auto &face : faces )
				objects.push_back( m_pPaintScene->SolidObject( face.m_nSolid ) );
			GetDocument().BeginDisplacementPreview( std::move( objects ) );
			m_flPaintStartMs = start.nsecsElapsed() / 1e6;
			m_SculptApplied.assign( m_Painted.size(), {} );
			m_PaintModifiers = pEvent->modifiers();
			m_bPaintAlpha = m_Editor.GetFaceEdit().GetDispPaintMode() == DispPaintMode_t::DISP_PAINT_ALPHA;
			m_bPaintSculpt = m_Editor.GetFaceEdit().GetDispPaintMode() == DispPaintMode_t::DISP_PAINT_SCULPT;
			m_bPaintLower = pEvent->button() == Qt::RightButton;
			m_PaintLast = pEvent->position().toPoint();
			PaintStroke( pEvent->position() );
		}
		else
		{
			emit m_Editor.StatusMessage( tr( "Select a displacement face before painting, or turn off Selected only" ) );
		}
		return;
	}
	// The texture tool's right click puts the current texture on the face (Alt: its values too).
	if ( pEvent->button() == Qt::RightButton && m_Editor.GetTool() == MapTool_t::MAP_TOOL_TEXTURE )
	{
		if ( const auto face = FaceAt( pEvent->position() ) )
		{
			m_Editor.ApplyToFace( *face, pEvent->modifiers().testFlag( Qt::AltModifier ) );
		}
	}
	// Hammer++'s right click on the gizmo goes round its modes; anywhere else, the menu of what
	// is under the cursor.
	// It opens when the button comes up, as Hammer's does, so the left one can still join it
	// for the both-buttons drag.
	if ( pEvent->button() == Qt::RightButton && m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT && !m_bMouselook && !( GetGizmo() && m_nGizmoHover >= 0 ) )
	{
		m_MenuAt = pEvent->position();
		m_bMenuPending = true;
		return;
	}
	if ( pEvent->button() == Qt::RightButton && m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT && GetGizmo() )
	{
		const MapGizmoMode_t nMode = m_Editor.GetGizmoMode();
		m_Editor.SetGizmoMode( nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE ? MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE : nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ? MapGizmoMode_t::MAP_GIZMO_MODE_SCALE
																																										  : MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Selected overlays expose four corners and a centre in the Overlay tool only.
//-----------------------------------------------------------------------------
std::optional<std::pair<std::uint32_t, int>> CMapView3D::OverlayHandleAt( QPointF at ) const
{
	const auto *pScene = GetDocument().GetScene();
	if ( !pScene || m_Editor.GetTool() != MapTool_t::MAP_TOOL_OVERLAY )
		return std::nullopt;
	const auto camera = GetSizedCamera();
	for ( const auto nObject : GetDocument().GetSelection() )
	{
		if ( pScene->IsSolidObject( nObject ) )
			continue;
		const auto &entity = pScene->m_Map.m_Entities[ pScene->GetIndex( nObject ) ];
		const auto corners = xv::map::OverlayCorners( entity );
		const auto origin = xv::map::ParsePoint( entity.GetValue( "BasisOrigin" ) );
		if ( !corners || !origin )
			continue;
		for ( int n = 0; n < 5; ++n )
		{
			const auto handle = n < 4 ? ( *corners )[ n ] : *origin;
			if ( const auto projected = camera.Project( handle ) )
			{
				if ( QLineF( at, QPointF{ ( *projected )[ 0 ], ( *projected )[ 1 ] } ).length() <= 8 )
					return std::pair{ pScene->GetIndex( nObject ), n };
			}
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The drag of the corner or centre under the mouse begins.
//-----------------------------------------------------------------------------
bool CMapView3D::BeginOverlayDrag( QPointF at )
{
	const auto handle = OverlayHandleAt( at );
	if ( !handle )
		return false;
	const auto &entity = GetDocument().GetScene()->m_Map.m_Entities[ handle->first ];
	const auto corners = xv::map::OverlayCorners( entity );
	const auto origin = xv::map::ParsePoint( entity.GetValue( "BasisOrigin" ) );
	if ( !corners || !origin )
		return false;
	m_pOverlayScene = GetDocument().GetSharedScene();
	m_nOverlayEntity = handle->first;
	m_nOverlayCorner = handle->second;
	m_OverlayFrom = handle->second < 4 ? ( *corners )[ static_cast<std::size_t>( handle->second ) ] : *origin;
	m_OverlayEdit = {};
	UpdateCursor();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Drag in the projection plane, constrained to the assigned brush/displacement faces.
//-----------------------------------------------------------------------------
void CMapView3D::PreviewOverlayDrag( QPointF at )
{
	if ( !m_pOverlayScene )
		return;
	const auto &entity = m_pOverlayScene->m_Map.m_Entities[ m_nOverlayEntity ];
	const auto normal = xv::map::ParsePoint( entity.GetValue( "BasisNormal" ) );
	if ( !normal )
		return;
	const auto [ from, direction ] = GetSizedCamera().Ray( at.x(), at.y() );
	const double flDenominator = xv::map::Dot( direction, *normal );
	if ( std::abs( flDenominator ) < 1e-9 )
		return;
	const double flDistance = xv::map::Dot( xv::map::Subtract( m_OverlayFrom, from ), *normal ) / flDenominator;
	if ( flDistance < 0 )
		return;
	const auto p = xv::map::Add( from, xv::map::Scale( direction, flDistance ) );
	xv::map::Entity_t changed = entity;
	if ( m_nOverlayCorner < 4 )
		changed = xv::map::MoveOverlayCorner( *m_pOverlayScene, entity, m_nOverlayCorner, p );
	else if ( const auto surface = xv::map::OverlaySurfacePoint( *m_pOverlayScene, entity, p ) )
		changed = xv::map::TransformEntity( m_pOverlayScene->m_Map, entity, xv::map::Transform_t::Translation( xv::map::Subtract( *surface, m_OverlayFrom ) ) );
	m_OverlayEdit.m_Entities = { { m_nOverlayEntity, std::move( changed ) } };
	GetDocument().Preview( m_OverlayEdit );
}

//-----------------------------------------------------------------------------
// Purpose: A handle drag is one undoable edit; Escape restores the untouched snapshot.
//-----------------------------------------------------------------------------
void CMapView3D::EndOverlayDrag( bool bCommit )
{
	if ( !m_pOverlayScene )
		return;
	m_pOverlayScene.reset();
	GetDocument().EndPreview();
	UpdateCursor();
	if ( bCommit && !m_OverlayEdit.IsEmpty() )
		GetDocument().Edit( std::move( m_OverlayEdit ), tr( "Edit Overlay" ) );
	m_OverlayEdit = {};
	RedrawLabels();
}

//-----------------------------------------------------------------------------
// Purpose: At the selection's middle (or the pivot, which turns and scales are about), on the
//          world's axes or (J) a lone entity's own.
//-----------------------------------------------------------------------------
std::optional<CMapGizmo> CMapView3D::GetGizmo() const
{
	const CMapDocument &document = GetDocument();
	const xv::map::CScene *pScene = document.GetScene();
	if ( m_Editor.IsWorldGridEdited() )
	{
		// The world grid's: at its origin on its own axes, arrows to move it or rings to turn it.
		const MapGridPlane_t &plane = m_Editor.GetSettings().m_WorldGrid;
		CMapGizmo gizmo;
		gizmo.Place( plane.m_Origin, plane.m_Axes );
		gizmo.SetBounds( xv::map::Box_t{ plane.m_Origin, plane.m_Origin } );
		gizmo.SetMode( m_Editor.GetGizmoMode() == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ? MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE : MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE );
		return gizmo;
	}
	if ( !m_Editor.IsGizmoShown() || m_Editor.GetTool() != MapTool_t::MAP_TOOL_SELECT || !pScene || document.GetSelection().empty() )
	{
		return std::nullopt;
	}
	const xv::map::Box_t bounds = document.GetSelectionBounds();
	if ( bounds.IsEmpty() )
	{
		return std::nullopt;
	}
	std::array<xv::map::Point_t, 3> axes{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	if ( m_Editor.IsGizmoLocal() && document.GetSelection().size() == 1 && !pScene->IsSolidObject( document.GetSelection().front() ) )
	{
		const xv::map::EntityVisual_t &visual = pScene->m_Entities[ pScene->GetIndex( document.GetSelection().front() ) ];
		const xv::map::Transform_t turn = xv::map::Transform_t::FromAngles( {}, visual.m_Angles );
		for ( xv::map::Point_t &axis : axes )
		{
			axis = turn.Rotate( axis );
		}
	}
	CMapGizmo gizmo;
	gizmo.Place( m_Editor.GetPivot().value_or( bounds.GetCenter() ), axes );
	gizmo.SetBounds( bounds );
	gizmo.SetMode( m_Editor.GetGizmoMode() );
	return gizmo;
}

//-----------------------------------------------------------------------------
// Purpose: The plane the block tool's box is measured on: the turned one it stands on, else
//          the world's own axes from the world's origin.
//-----------------------------------------------------------------------------
MapGridPlane_t CMapView3D::BoxFrame() const
{
	return m_Editor.GetNewBoxPlane().value_or( MapGridPlane_t{} );
}

//-----------------------------------------------------------------------------
// Purpose: The middle of each of the box's faces, in the box's own measures.
//-----------------------------------------------------------------------------
std::array<xv::map::Point_t, 6> CMapView3D::BoxHandles( const xv::map::Box_t &box ) const
{
	std::array<xv::map::Point_t, 6> handles{};
	const xv::map::Point_t centre = box.GetCenter();
	for ( int f = 0; f < 6; f++ )
	{
		handles[ f ] = centre;
		handles[ f ][ f / 2 ] = f % 2 ? box.m_Maxs[ f / 2 ] : box.m_Mins[ f / 2 ];
	}
	return handles;
}

//-----------------------------------------------------------------------------
// Purpose: A face's arrow on screen: from the middle of the face, a fixed length the way the
//          face looks. An axis that points at the camera has no direction on screen: its arrow
//          is a point (drawn as a dot), and its depth says which of two such is nearer.
//-----------------------------------------------------------------------------
std::optional<CMapView3D::BoxArrow_t> CMapView3D::BoxArrow( const xv::map::Box_t &box, int nFace ) const
{
	const xv::map::CViewCamera camera = GetSizedCamera();
	const MapGridPlane_t frame = BoxFrame();
	const xv::map::Point_t from = BoxHandles( box )[ static_cast<std::size_t>( nFace ) ];
	const auto foot = camera.Project( frame.ToWorld( from ) );
	if ( !foot )
		return std::nullopt;
	BoxArrow_t arrow;
	arrow.m_From = QPointF{ ( *foot )[ 0 ], ( *foot )[ 1 ] };
	arrow.m_To = arrow.m_From;
	arrow.m_flDepth = ( *foot )[ 2 ];
	// A step out of the face, as long as the box is wide there at least, so the direction on
	// screen is the axis's however near the camera is.
	xv::map::Point_t out = from;
	out[ static_cast<std::size_t>( nFace / 2 ) ] += ( nFace % 2 ? 1.0 : -1.0 ) * std::max( 1.0, box.GetSize()[ static_cast<std::size_t>( nFace / 2 ) ] * 0.25 );
	if ( const auto ahead = camera.Project( frame.ToWorld( out ) ) )
	{
		const QPointF along{ ( *ahead )[ 0 ] - ( *foot )[ 0 ], ( *ahead )[ 1 ] - ( *foot )[ 1 ] };
		if ( const double flLength = std::hypot( along.x(), along.y() ); flLength > 0.5 )
			arrow.m_To = arrow.m_From + along * ( BOX_ARROW_PIXELS / flLength );
	}
	return arrow;
}

//-----------------------------------------------------------------------------
// Purpose: The arrow the pointer is on (within a few pixels of its line); of several, the one
//          it is nearest, then the one nearer the camera.
//-----------------------------------------------------------------------------
int CMapView3D::BoxHandleAt( QPointF at ) const
{
	const std::optional<xv::map::Box_t> &box = m_Editor.GetBlockBox();
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BLOCK || !box || box->IsEmpty() )
		return -1;
	int nBest = -1;
	double flBest = std::numeric_limits<double>::infinity();
	for ( int f = 0; f < 6; f++ )
	{
		const std::optional<BoxArrow_t> arrow = BoxArrow( *box, f );
		if ( !arrow )
			continue;
		const QPointF along = arrow->m_To - arrow->m_From;
		const double flSquared = along.x() * along.x() + along.y() * along.y();
		const double t = flSquared > 0.0 ? std::clamp( QPointF::dotProduct( at - arrow->m_From, along ) / flSquared, 0.0, 1.0 ) : 0.0;
		const QPointF nearest = arrow->m_From + along * t;
		const double flPixels = std::hypot( nearest.x() - at.x(), nearest.y() - at.y() );
		if ( flPixels > BOX_ARROW_SLACK )
			continue;
		const double flScore = flPixels + arrow->m_flDepth * 1e-3;
		if ( flScore < flBest )
		{
			flBest = flScore;
			nBest = f;
		}
	}
	return nBest;
}

//-----------------------------------------------------------------------------
// Purpose: The pointer's ray in the box's own measures, where a turned box is one along the
//          axes like any other.
//-----------------------------------------------------------------------------
std::pair<xv::map::Point_t, xv::map::Point_t> CMapView3D::BoxRay( QPointF at ) const
{
	const MapGridPlane_t frame = BoxFrame();
	const auto [ origin, direction ] = GetSizedCamera().Ray( at.x(), at.y() );
	return { frame.ToLocal( origin ), MapGridPlane_t{ {}, frame.m_Axes }.ToLocal( direction ) };
}

//-----------------------------------------------------------------------------
// Purpose: Where the pointer's ray enters the block tool's box: the face (as BoxHandles numbers
//          them) and the point, in the box's own measures. None when it misses, or from inside
//          the box.
//-----------------------------------------------------------------------------
std::optional<std::pair<int, xv::map::Point_t>> CMapView3D::BoxSurfaceAt( QPointF at ) const
{
	const std::optional<xv::map::Box_t> &box = m_Editor.GetBlockBox();
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_BLOCK || !box || box->IsEmpty() )
		return std::nullopt;
	const auto [ origin, direction ] = BoxRay( at );
	double flEnter = 0.0;
	double flLeave = std::numeric_limits<double>::infinity();
	int nFace = -1;
	for ( std::size_t a = 0; a < 3; a++ )
	{
		if ( std::abs( direction[ a ] ) < 1e-12 )
		{
			if ( origin[ a ] < box->m_Mins[ a ] || origin[ a ] > box->m_Maxs[ a ] )
				return std::nullopt;
			continue;
		}
		const bool bFromLow = direction[ a ] > 0.0;
		const double flNear = ( ( bFromLow ? box->m_Mins[ a ] : box->m_Maxs[ a ] ) - origin[ a ] ) / direction[ a ];
		const double flFar = ( ( bFromLow ? box->m_Maxs[ a ] : box->m_Mins[ a ] ) - origin[ a ] ) / direction[ a ];
		if ( flNear > flEnter )
		{
			flEnter = flNear;
			nFace = static_cast<int>( a ) * 2 + ( bFromLow ? 0 : 1 );
		}
		flLeave = std::min( flLeave, flFar );
	}
	if ( nFace < 0 || flEnter > flLeave )
		return std::nullopt;
	return std::pair{ nFace, xv::map::Add( origin, xv::map::Scale( direction, flEnter ) ) };
}

//-----------------------------------------------------------------------------
// Purpose: A drag on the box. By an arrow: its face goes where the pointer's ray passes nearest
//          the line through the arrow's foot along its axis, never across the face opposite (a
//          grid step apart at least). By the box itself: it slides across the face pressed,
//          following the pointer on that face's plane. On the grid unless free (Alt).
//-----------------------------------------------------------------------------
void CMapView3D::DragBox( QPointF at, Qt::KeyboardModifiers modifiers )
{
	if ( !m_BoxFrom || ( m_nBoxFace < 0 && m_nBoxPlane < 0 ) )
		return;
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	const double flGrid = modifiers.testFlag( Qt::AltModifier ) || !settings.m_bSnapToGrid ? 0.0 : static_cast<double>( settings.m_nGridSize );
	const auto [ origin, direction ] = BoxRay( at );
	xv::map::Box_t box = *m_BoxFrom;
	if ( m_nBoxFace < 0 )
	{
		const auto nNormal = static_cast<std::size_t>( m_nBoxPlane );
		if ( std::abs( direction[ nNormal ] ) < 1e-6 )
			return; // looking along the plane: the pointer is nowhere on it
		const double t = ( m_BoxGrab[ nNormal ] - origin[ nNormal ] ) / direction[ nNormal ];
		if ( t <= 0.0 )
			return;
		xv::map::Point_t offset = xv::map::Subtract( xv::map::Add( origin, xv::map::Scale( direction, t ) ), m_BoxGrab );
		offset[ nNormal ] = 0.0;
		if ( flGrid > 0.0 )
			for ( double &flAlong : offset )
				flAlong = std::round( flAlong / flGrid ) * flGrid;
		box.m_Mins = xv::map::Add( box.m_Mins, offset );
		box.m_Maxs = xv::map::Add( box.m_Maxs, offset );
	}
	else
	{
		const auto nAxis = static_cast<std::size_t>( m_nBoxFace / 2 );
		const xv::map::Point_t handle = BoxHandles( *m_BoxFrom )[ static_cast<std::size_t>( m_nBoxFace ) ];
		xv::map::Point_t along{};
		along[ nAxis ] = 1.0;
		// The closest points of two lines: the ray's and the axis's through the handle.
		const xv::map::Point_t w0 = xv::map::Subtract( origin, handle );
		const double b = xv::map::Dot( direction, along );
		const double d = xv::map::Dot( direction, w0 );
		const double e = xv::map::Dot( along, w0 );
		const double flDenominator = 1.0 - b * b;
		if ( std::abs( flDenominator ) < 1e-6 )
			return; // looking straight along the axis: no way to tell how far
		double flTo = handle[ nAxis ] + ( e - b * d ) / flDenominator;
		if ( flGrid > 0.0 )
			flTo = std::round( flTo / flGrid ) * flGrid;
		const double flLeast = flGrid > 0.0 ? flGrid : 1.0;
		if ( m_nBoxFace % 2 )
			box.m_Maxs[ nAxis ] = std::max( flTo, box.m_Mins[ nAxis ] + flLeast );
		else
			box.m_Mins[ nAxis ] = std::min( flTo, box.m_Maxs[ nAxis ] - flLeast );
	}
	if ( box != m_Editor.GetBlockBox() )
		m_Editor.SetBlockBox( box );
}

//-----------------------------------------------------------------------------
// Purpose: Ends a drag on the block tool's box: where it is, or back as it was.
//-----------------------------------------------------------------------------
void CMapView3D::EndBoxDrag( bool bCommit )
{
	if ( !bCommit && m_BoxFrom )
		m_Editor.SetBlockBox( m_BoxFrom );
	m_nBoxFace = -1;
	m_nBoxPlane = -1;
	m_BoxFrom.reset();
	UpdateCursor();
	RedrawLabels();
}

//-----------------------------------------------------------------------------
// Purpose: A new box begun where the pointer is: on the world grid's plane while it shows, else
//          on the surface there, taken as the plane across the world axis it faces most. It
//          stands on the side of the plane the camera is on, as tall as the last box was that
//          way (a grid step when that is nothing). False when there is no plane under the
//          pointer.
//-----------------------------------------------------------------------------
bool CMapView3D::BeginBlockDrag( QPointF at )
{
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	const xv::map::CViewCamera camera = GetSizedCamera();
	const auto [ origin, direction ] = camera.Ray( at.x(), at.y() );
	MapGridPlane_t plane;
	if ( settings.m_bWorldGrid )
	{
		plane = settings.m_WorldGrid;
	}
	else
	{
		const auto surface = SurfaceAt( at );
		if ( !surface )
			return false;
		std::size_t nAxis = 0;
		for ( std::size_t a = 1; a < 3; a++ )
			if ( std::abs( surface->second[ a ] ) > std::abs( surface->second[ nAxis ] ) )
				nAxis = a;
		plane.m_Axes = {};
		plane.m_Axes[ 0 ][ ( nAxis + 1 ) % 3 ] = 1.0;
		plane.m_Axes[ 1 ][ ( nAxis + 2 ) % 3 ] = 1.0;
		plane.m_Axes[ 2 ][ nAxis ] = 1.0;
		// The mesh's corners are floats: a face at 128 is a hair off it.
		plane.m_Origin[ nAxis ] = std::round( surface->first[ nAxis ] * 64.0 ) / 64.0;
	}
	const double flAlong = xv::map::Dot( direction, plane.m_Axes[ 2 ] );
	const double flGap = xv::map::Dot( xv::map::Subtract( plane.m_Origin, origin ), plane.m_Axes[ 2 ] );
	if ( std::abs( flAlong ) < 1e-6 || flGap / flAlong <= 0.0 )
		return false; // the plane is edge on, or behind the pointer
	const xv::map::Point_t local = plane.ToLocal( xv::map::Add( origin, xv::map::Scale( direction, flGap / flAlong ) ) );
	const double flGrid = settings.m_bSnapToGrid ? static_cast<double>( settings.m_nGridSize ) : 0.0;
	const auto snapped = [ & ]( double flValue ) { return flGrid > 0.0 ? std::round( flValue / flGrid ) * flGrid : flValue; };
	m_BlockFrom = { snapped( local[ 0 ] ), snapped( local[ 1 ] ) };
	// How tall: the last box's size along the world axis nearest the plane's normal.
	std::size_t nUp = 0;
	for ( std::size_t a = 1; a < 3; a++ )
		if ( std::abs( plane.m_Axes[ 2 ][ a ] ) > std::abs( plane.m_Axes[ 2 ][ nUp ] ) )
			nUp = a;
	std::pair<double, double> depth = m_Editor.GetNewBoxDepth( static_cast<int>( nUp ) );
	if ( m_Editor.GetNewBoxPlane() && m_Editor.GetBlockBox() )
		depth = { m_Editor.GetBlockBox()->m_Mins[ 2 ], m_Editor.GetBlockBox()->m_Maxs[ 2 ] }; // a turned box's height is across its plane
	const double flTall = depth.second - depth.first > 0.0 ? depth.second - depth.first : static_cast<double>( std::max( 1, settings.m_nGridSize ) );
	m_flBlockDepth = flGap < 0.0 ? flTall : -flTall; // the camera's side of the plane
	m_BlockBefore = m_Editor.GetBlockBox();
	m_BlockPlaneBefore = m_Editor.GetNewBoxPlane();
	m_BlockPlane = plane;
	m_BlockLocal.reset();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The box from where the drag began to where the pointer is on the plane, on the grid
//          unless free (Alt). Along the world's axes it is a box in the world's measures, which
//          the 2D views show and size too; turned, one in the plane's.
//-----------------------------------------------------------------------------
void CMapView3D::DragBlock( QPointF at, Qt::KeyboardModifiers modifiers )
{
	if ( !m_BlockPlane )
		return;
	const MapGridPlane_t &plane = *m_BlockPlane;
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	const double flGrid = modifiers.testFlag( Qt::AltModifier ) || !settings.m_bSnapToGrid ? 0.0 : static_cast<double>( settings.m_nGridSize );
	const auto [ origin, direction ] = GetSizedCamera().Ray( at.x(), at.y() );
	const double flAlong = xv::map::Dot( direction, plane.m_Axes[ 2 ] );
	const double flGap = xv::map::Dot( xv::map::Subtract( plane.m_Origin, origin ), plane.m_Axes[ 2 ] );
	if ( std::abs( flAlong ) < 1e-6 || flGap / flAlong <= 0.0 )
		return;
	xv::map::Point_t to = plane.ToLocal( xv::map::Add( origin, xv::map::Scale( direction, flGap / flAlong ) ) );
	if ( flGrid > 0.0 )
		to = { std::round( to[ 0 ] / flGrid ) * flGrid, std::round( to[ 1 ] / flGrid ) * flGrid, 0.0 };
	xv::map::Box_t local;
	local.Add( xv::map::Point_t{ m_BlockFrom[ 0 ], m_BlockFrom[ 1 ], 0.0 } );
	local.Add( xv::map::Point_t{ to[ 0 ], to[ 1 ], m_flBlockDepth } );
	m_BlockLocal = local;
	if ( plane.IsAlongWorld() )
	{
		xv::map::Box_t box;
		for ( const xv::map::Point_t &corner : { local.m_Mins, local.m_Maxs } )
		{
			xv::map::Point_t world = plane.ToWorld( corner );
			for ( double &flPart : world )
				flPart = std::round( flPart * 1024.0 ) / 1024.0;
			box.Add( world );
		}
		m_Editor.SetNewBox( box );
	}
	else
	{
		m_Editor.SetNewBox( local, plane );
	}
	RedrawLabels();
}

//-----------------------------------------------------------------------------
// Purpose: The drag over. A box with no size across the plane (a click) or a drag given up
//          leaves the editor's box as it was; any other stays to be sized, and Enter makes it.
//-----------------------------------------------------------------------------
void CMapView3D::EndBlockDrag( bool bCommit )
{
	if ( !m_BlockPlane )
		return;
	const std::optional<xv::map::Box_t> local = m_BlockLocal;
	m_BlockPlane.reset();
	m_BlockLocal.reset();
	const bool bSized = local && local->m_Maxs[ 0 ] > local->m_Mins[ 0 ] && local->m_Maxs[ 1 ] > local->m_Mins[ 1 ];
	if ( !bCommit || !bSized )
		m_Editor.SetNewBox( m_BlockBefore, m_BlockPlaneBefore );
	m_BlockBefore.reset();
	m_BlockPlaneBefore.reset();
	RedrawLabels();
}

//-----------------------------------------------------------------------------
// Purpose: The plane's origin carried by the transform and its axes turned by it, then made
//          square again (many small turns add up their rounding).
//-----------------------------------------------------------------------------
MapGridPlane_t CMapView3D::MovedPlane( const MapGridPlane_t &plane, const xv::map::Transform_t &transform )
{
	MapGridPlane_t moved;
	moved.m_Origin = transform.Apply( plane.m_Origin );
	const xv::map::Point_t u = xv::map::Normalized( transform.Rotate( plane.m_Axes[ 0 ] ) );
	const xv::map::Point_t normal = xv::map::Normalized( xv::map::Cross( u, transform.Rotate( plane.m_Axes[ 1 ] ) ) );
	moved.m_Axes = { u, xv::map::Cross( normal, u ), normal };
	// A turn back onto the world's axes lands on them, not a rounding off.
	for ( xv::map::Point_t &axis : moved.m_Axes )
		for ( double &flPart : axis )
			if ( std::abs( flPart ) < 1e-12 || std::abs( std::abs( flPart ) - 1.0 ) < 1e-12 )
				flPart = std::round( flPart );
	return moved;
}

//-----------------------------------------------------------------------------
// Purpose: While the world grid is edited, a double-click lays it on the surface there: through
//          the point, level lines along the surface (across its slope), counted from the grid
//          crossing nearest the point.
//-----------------------------------------------------------------------------
void CMapView3D::mouseDoubleClickEvent( QMouseEvent *pEvent )
{
	if ( pEvent->button() != Qt::LeftButton || !m_Editor.IsWorldGridEdited() )
	{
		CMapView::mouseDoubleClickEvent( pEvent );
		return;
	}
	const auto surface = SurfaceAt( pEvent->position() );
	if ( !surface )
		return;
	MapGridPlane_t plane;
	const xv::map::Point_t normal = xv::map::Normalized( surface->second );
	if ( std::abs( normal[ 2 ] ) < 1.0 - 1e-9 )
	{
		const xv::map::Point_t level = xv::map::Normalized( xv::map::Cross( xv::map::Point_t{ 0, 0, 1 }, normal ) );
		plane.m_Axes = { level, xv::map::Cross( normal, level ), normal };
	}
	else if ( normal[ 2 ] < 0.0 )
		plane.m_Axes = { { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 } } };
	// On the world's grid where the plane lets it: a level floor's lines are the 2D views'.
	plane.m_Origin = surface->first;
	const double flGrid = std::max( 1, m_Editor.GetSettings().m_nGridSize );
	const MapGridPlane_t fromWorld{ {}, plane.m_Axes };
	xv::map::Point_t local = fromWorld.ToLocal( surface->first );
	local = { std::round( local[ 0 ] / flGrid ) * flGrid, std::round( local[ 1 ] / flGrid ) * flGrid, std::round( local[ 2 ] * 64.0 ) / 64.0 };
	plane.m_Origin = fromWorld.ToWorld( local );
	m_Editor.SetWorldGridPlane( MovedPlane( plane, {} ) );
}

//-----------------------------------------------------------------------------
// Purpose: The scale that keeps what the middle of the view looks at (or 512 units ahead) the
//          size the field of view showed it; back, the field of view as before.
//-----------------------------------------------------------------------------
void CMapView3D::SetOrthographic( bool bOrthographic )
{
	if ( bOrthographic == IsOrthographic() )
	{
		return;
	}
	if ( !bOrthographic )
	{
		m_Camera.m_flOrthoScale = 0.0;
		Changed();
		return;
	}
	double flDistance = ORTHO_DEFAULT_DISTANCE;
	if ( const auto surface = SurfaceAt( QPointF{ width() / 2.0, height() / 2.0 } ) )
	{
		flDistance = std::max( 16.0, xv::map::Dot( xv::map::Subtract( surface->first, m_Camera.m_Eye ), m_Camera.GetForward() ) );
	}
	const double flFocal = std::max( 1, width() ) / 2.0 / std::tan( std::clamp( m_Camera.m_flFov, 1.0, 179.0 ) * std::numbers::pi / 360.0 );
	m_Camera.m_flOrthoScale = std::clamp( flFocal / flDistance, ORTHO_SMALLEST, ORTHO_LARGEST );
	Changed();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::CViewCamera CMapView3D::GetSizedCamera() const
{
	xv::map::CViewCamera camera = m_Camera;
	camera.m_nWidth = std::max( 1, width() );
	camera.m_nHeight = std::max( 1, height() );
	return camera;
}

//-----------------------------------------------------------------------------
// Purpose: Start a keyboard transform from the current selection and pointer.
//-----------------------------------------------------------------------------
void CMapView3D::BeginModalTransform( MapGizmoMode_t nMode )
{
	if ( m_ModalTransform )
		EndModalTransform( false );
	m_Editor.SetGizmoMode( nMode );
	if ( m_nGizmoDrag >= 0 || m_pOverlayScene || m_pPaintScene || m_nBoxFace >= 0 || m_nBoxPlane >= 0 )
		return;
	// The world grid's plane: moved or turned about its origin, on its own axes.
	if ( m_Editor.IsWorldGridEdited() )
	{
		if ( nMode != MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE && nMode != MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE )
			return;
		if ( m_bMouselook )
			SetMouselook( false );
		m_GridFrom = m_Editor.GetSettings().m_WorldGrid;
		m_ModalTransform.emplace( nMode, m_GridFrom->m_Origin, m_GridFrom->m_Axes, GetSizedCamera(), m_LastMouse );
		m_ModalResult = {};
		PreviewModalTransform( QApplication::keyboardModifiers() );
		return;
	}
	// The block tool's box: moved, turned by quarter turns (it stays a box along the axes) or
	// sized about its middle.
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_BLOCK )
	{
		const std::optional<xv::map::Box_t> &box = m_Editor.GetBlockBox();
		if ( !box || box->IsEmpty() )
			return;
		if ( m_bMouselook )
			SetMouselook( false );
		m_BoxFrom = box;
		const MapGridPlane_t frame = BoxFrame();
		m_ModalTransform.emplace( nMode, frame.ToWorld( box->GetCenter() ), frame.m_Axes, GetSizedCamera(), m_LastMouse );
		m_ModalResult = {};
		PreviewModalTransform( QApplication::keyboardModifiers() );
		return;
	}
	if ( m_Editor.GetTool() != MapTool_t::MAP_TOOL_SELECT )
		return;
	const auto &document = GetDocument();
	if ( !document.GetScene() || document.GetSelection().empty() || document.GetSelectionBounds().IsEmpty() )
		return;
	std::array<xv::map::Point_t, 3> axes{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	if ( document.GetSelection().size() == 1 && !document.GetScene()->IsSolidObject( document.GetSelection().front() ) )
	{
		const auto &visual = document.GetScene()->m_Entities[ document.GetScene()->GetIndex( document.GetSelection().front() ) ];
		const auto rotation = xv::map::Transform_t::FromAngles( {}, visual.m_Angles );
		for ( auto &axis : axes )
			axis = rotation.Rotate( axis );
	}
	if ( m_bMouselook )
		SetMouselook( false );
	m_pGizmoScene = document.GetSharedScene();
	m_GizmoSelection = document.GetSelection();
	m_ModalTransform.emplace( nMode, m_Editor.GetPivot().value_or( document.GetSelectionBounds().GetCenter() ), axes, GetSizedCamera(), m_LastMouse );
	m_ModalResult = {};
	PreviewModalTransform( QApplication::keyboardModifiers() );
}

//-----------------------------------------------------------------------------
// Purpose: Preview against the original scene without adding undo commands.
//-----------------------------------------------------------------------------
void CMapView3D::PreviewModalTransform( Qt::KeyboardModifiers modifiers )
{
	if ( !m_ModalTransform || ( !m_pGizmoScene && !m_BoxFrom && !m_GridFrom ) )
		return;
	const auto &settings = m_Editor.GetSettings();
	const bool bFree = modifiers.testFlag( Qt::ControlModifier ) || modifiers.testFlag( Qt::AltModifier );
	if ( m_GridFrom )
	{
		m_ModalResult = m_ModalTransform->Evaluate( GetSizedCamera(), bFree || !settings.m_bSnapToGrid ? 0.0 : settings.m_nGridSize, bFree ? 0.0 : CPreferences::Instance().GetMapPreferences().m_flRotationSnap );
		m_Editor.SetWorldGridPlane( MovedPlane( *m_GridFrom, m_ModalResult ), false );
		emit m_Editor.StatusMessage( m_ModalTransform->GetDescription() );
		RedrawLabels();
		return;
	}
	if ( m_BoxFrom )
	{
		m_ModalResult = m_ModalTransform->Evaluate( GetSizedCamera(), bFree || !settings.m_bSnapToGrid ? 0.0 : settings.m_nGridSize, 90.0 );
		// The box around its corners moved; a size squeezed to nothing (or through itself) is
		// left as it was. Rounded, as a quarter turn's sines aren't quite 0 and 1.
		// A turned box is moved in the world and measured on its plane again.
		const MapGridPlane_t frame = BoxFrame();
		xv::map::Box_t box;
		for ( int n = 0; n < 8; n++ )
			box.Add( frame.ToLocal( m_ModalResult.Apply( frame.ToWorld( xv::map::Point_t{ ( n & 1 ) ? m_BoxFrom->m_Maxs[ 0 ] : m_BoxFrom->m_Mins[ 0 ], ( n & 2 ) ? m_BoxFrom->m_Maxs[ 1 ] : m_BoxFrom->m_Mins[ 1 ], ( n & 4 ) ? m_BoxFrom->m_Maxs[ 2 ] : m_BoxFrom->m_Mins[ 2 ] } ) ) ) );
		bool bSized = true;
		for ( std::size_t a = 0; a < 3; a++ )
		{
			box.m_Mins[ a ] = std::round( box.m_Mins[ a ] * 1024.0 ) / 1024.0;
			box.m_Maxs[ a ] = std::round( box.m_Maxs[ a ] * 1024.0 ) / 1024.0;
			bSized = bSized && box.m_Maxs[ a ] - box.m_Mins[ a ] >= 1.0;
		}
		m_Editor.SetBlockBox( bSized ? box : *m_BoxFrom );
		emit m_Editor.StatusMessage( m_ModalTransform->GetDescription() );
		RedrawLabels();
		return;
	}
	m_ModalResult = m_ModalTransform->Evaluate( GetSizedCamera(), bFree || !settings.m_bSnapToGrid ? 0.0 : settings.m_nGridSize, bFree ? 0.0 : CPreferences::Instance().GetMapPreferences().m_flRotationSnap );
	if ( m_ModalResult.IsIdentity() )
		GetDocument().EndPreview();
	else
		GetDocument().Preview( xv::map::TransformObjects( *m_pGizmoScene, m_GizmoSelection, m_ModalResult, settings.m_bTextureLock, settings.m_bScaleLock ) );
	emit m_Editor.StatusMessage( m_ModalTransform->GetDescription() );
	RedrawLabels();
}

//-----------------------------------------------------------------------------
// Purpose: Restore the preview base, then commit exactly one transform if confirmed.
//-----------------------------------------------------------------------------
void CMapView3D::EndModalTransform( bool bCommit )
{
	if ( !m_ModalTransform )
		return;
	const auto nMode = m_ModalTransform->GetMode();
	const auto transform = m_ModalResult;
	m_ModalTransform.reset();
	if ( m_GridFrom )
	{
		// Kept where the preview put it, or back as it was.
		m_Editor.SetWorldGridPlane( bCommit ? m_Editor.GetSettings().m_WorldGrid : *m_GridFrom );
		m_GridFrom.reset();
		emit m_Editor.StatusMessage( QString{} );
		RedrawLabels();
		return;
	}
	if ( m_BoxFrom )
	{
		// The block tool's box stays where the preview put it, or goes back.
		if ( !bCommit )
			m_Editor.SetBlockBox( m_BoxFrom );
		m_BoxFrom.reset();
		emit m_Editor.StatusMessage( QString{} );
		RedrawLabels();
		return;
	}
	GetDocument().EndPreview();
	m_pGizmoScene.reset();
	m_GizmoSelection.clear();
	if ( bCommit )
		m_Editor.TransformSelection( transform, nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE ? tr( "Move" ) : nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ? tr( "Rotate" )
																																								  : tr( "Scale" ) );
	emit m_Editor.StatusMessage( QString{} );
	RedrawLabels();
}

//-----------------------------------------------------------------------------
// Purpose: The drag's transform shown as it goes: along the grid (Alt: free), in rotation
//          steps (Preferences; Alt: free).
//-----------------------------------------------------------------------------
void CMapView3D::PreviewGizmo( QPointF at, Qt::KeyboardModifiers modifiers )
{
	if ( !m_DragGizmo || ( !m_pGizmoScene && !m_GridFrom ) )
	{
		return;
	}
	const bool bFree = modifiers.testFlag( Qt::AltModifier );
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	const double flGrid = bFree || !settings.m_bSnapToGrid ? 0.0 : static_cast<double>( settings.m_nGridSize );
	const double flStep = bFree ? 0.0 : CPreferences::Instance().GetMapPreferences().m_flRotationSnap;
	const xv::map::Transform_t transform = m_DragGizmo->Drag( GetSizedCamera(), m_nGizmoDrag, m_GizmoFrom, at, flGrid, flStep );
	if ( m_GridFrom )
	{
		m_Editor.SetWorldGridPlane( MovedPlane( *m_GridFrom, transform ), false );
		return;
	}
	GetDocument().Preview( xv::map::TransformObjects( *m_pGizmoScene, m_GizmoSelection, transform, settings.m_bTextureLock, settings.m_bScaleLock ) );
}

//-----------------------------------------------------------------------------
// Purpose: The drag over: the scene as it was, then (unless given up) the transform as one
//          step to undo.
//-----------------------------------------------------------------------------
void CMapView3D::EndGizmo( bool bCommit, QPointF at, Qt::KeyboardModifiers modifiers )
{
	if ( m_nGizmoDrag < 0 )
	{
		return;
	}
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	const bool bFree = modifiers.testFlag( Qt::AltModifier );
	const double flGrid = bFree || !settings.m_bSnapToGrid ? 0.0 : static_cast<double>( settings.m_nGridSize );
	const double flStep = bFree ? 0.0 : CPreferences::Instance().GetMapPreferences().m_flRotationSnap;
	const xv::map::Transform_t transform = m_DragGizmo ? m_DragGizmo->Drag( GetSizedCamera(), m_nGizmoDrag, m_GizmoFrom, at, flGrid, flStep ) : xv::map::Transform_t{};
	const MapGizmoMode_t nMode = m_DragGizmo ? m_DragGizmo->GetPartMode( m_nGizmoDrag ) : MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE;
	if ( m_GridFrom )
	{
		// The world grid's plane: where the drag took it (kept for the next run), or back.
		m_Editor.SetWorldGridPlane( bCommit ? MovedPlane( *m_GridFrom, transform ) : *m_GridFrom );
		m_GridFrom.reset();
		m_nGizmoDrag = -1;
		m_DragGizmo.reset();
		UpdateCursor();
		RedrawLabels();
		return;
	}
	if ( m_pGizmoScene )
	{
		GetDocument().EndPreview();
	}
	m_nGizmoDrag = -1;
	m_DragGizmo.reset();
	m_pGizmoScene.reset();
	m_GizmoSelection.clear();
	UpdateCursor();
	if ( bCommit )
	{
		m_Editor.TransformSelection( transform, nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE ? tr( "Move" ) : nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ? tr( "Rotate" )
				: nMode == MapGizmoMode_t::MAP_GIZMO_MODE_SHEAR                                                                                                   ? tr( "Shear" )
																																								  : tr( "Scale" ) );
	}
	RedrawLabels();
}

//-----------------------------------------------------------------------------
// Purpose: The camera tool's drags: left looks, right pans, both move forward and back.
//          Space or mouselook turn the camera without the tool.
//-----------------------------------------------------------------------------
void CMapView3D::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( m_bOrbit )
	{
		if ( m_PointerLock.IsActive() )
			return;
		const QPoint at = pEvent->position().toPoint();
		const QPoint delta = at - m_LastMouse;
		m_LastMouse = at;
		Orbit( delta );
		WarpToMiddle();
		return;
	}
	if ( m_bSpaceDrag )
	{
		// A pinned pointer's movement comes through the lock's callback.
		if ( m_PointerLock.IsActive() )
			return;
		const QPoint at = pEvent->position().toPoint();
		const QPoint delta = at - m_LastMouse;
		m_LastMouse = at;
		if ( !delta.isNull() )
			m_bMoved = true;
		DragCamera( pEvent->buttons(), delta );
		WarpToMiddle();
		return;
	}
	if ( m_Editor.GetBrushEdit().HandleEvent( *this, pEvent ) )
	{
		pEvent->accept();
		return;
	}
	if ( m_ModalTransform )
	{
		m_LastMouse = pEvent->position().toPoint();
		m_ModalTransform->MovePointer( pEvent->position(), pEvent->modifiers().testFlag( Qt::ShiftModifier ) );
		PreviewModalTransform( pEvent->modifiers() );
		return;
	}
	if ( m_pOverlayScene )
	{
		PreviewOverlayDrag( pEvent->position() );
		return;
	}
	const QPoint at = pEvent->position().toPoint();
	const QPoint delta = at - m_LastMouse;
	m_LastMouse = at;
	if ( ( at - m_PressAt ).manhattanLength() > 2 )
	{
		m_bMoved = true;
	}
	const bool bCameraTool = m_Editor.GetTool() == MapTool_t::MAP_TOOL_CAMERA;
	if ( m_bMouselook && m_PointerLock.IsActive() )
	{
		return; // the pinned pointer's movement comes through Look() directly
	}
	if ( m_nGizmoDrag >= 0 && ( pEvent->buttons() & Qt::LeftButton ) )
	{
		PreviewGizmo( pEvent->position(), pEvent->modifiers() );
		return;
	}
	if ( ( m_nBoxFace >= 0 || m_nBoxPlane >= 0 ) && ( pEvent->buttons() & Qt::LeftButton ) )
	{
		DragBox( pEvent->position(), pEvent->modifiers() );
		return;
	}
	if ( m_BlockPlane && ( pEvent->buttons() & Qt::LeftButton ) )
	{
		DragBlock( pEvent->position(), pEvent->modifiers() );
		return;
	}
	if ( m_RadiusFrom && ( pEvent->buttons() & ( Qt::LeftButton | Qt::RightButton ) ) )
	{
		// A hundred pixels right makes the brush about 2.7 times as big.
		SetBrushRadius( std::clamp( m_flRadiusFrom * std::exp( ( at.x() - m_RadiusFrom->x() ) / 100.0 ), 1.0, 8192.0 ) );
		// The sprinkle tool's drag works as Photoshop's brush: up makes it denser, down sparser,
		// two hundred pixels the whole range.
		if ( IsSprinkling() )
			m_Editor.GetSprinkle().SetDensity( m_flDensityFrom - ( at.y() - m_RadiusFrom->y() ) / 200.0 );
		RedrawLabels();
		return;
	}
	if ( m_pSprinkleStroke && ( pEvent->buttons() & Qt::LeftButton ) )
	{
		if ( ( at - m_SprinkleLast ).manhattanLength() >= 8 )
			SprinkleAt( pEvent->position(), pEvent->modifiers() );
		return;
	}
	if ( m_pPaintScene && ( pEvent->buttons() & ( Qt::LeftButton | Qt::RightButton ) ) )
	{
		// A stroke paints again every few pixels along the way.
		if ( ( at - m_PaintLast ).manhattanLength() >= 8 )
		{
			m_PaintLast = at;
			m_PaintModifiers = pEvent->modifiers();
			PaintStroke( pEvent->position() );
		}
		return;
	}
	if ( pEvent->buttons() == Qt::NoButton && !m_bMouselook && IsBrushShown() )
	{
		m_BrushAt = SurfaceAt( pEvent->position() );
		RedrawLabels();
	}
	else if ( m_BrushAt && !m_pPaintScene )
	{
		m_BrushAt.reset();
		RedrawLabels();
	}
	if ( pEvent->buttons() == Qt::NoButton && !m_bMouselook )
	{
		UpdateCursor();
	}
	if ( pEvent->buttons() == Qt::NoButton && !m_bMouselook && !m_ModalTransform && m_Editor.GetTool() == MapTool_t::MAP_TOOL_BLOCK )
	{
		// The box's arrow under the mouse lights up; over the box itself the cursor says it moves.
		const int nFace = BoxHandleAt( pEvent->position() );
		const bool bOver = nFace < 0 && BoxSurfaceAt( pEvent->position() ).has_value();
		if ( nFace != m_nBoxFaceHover || bOver != m_bBoxHover )
		{
			m_nBoxFaceHover = nFace;
			m_bBoxHover = bOver;
			RedrawLabels();
			UpdateCursor();
		}
	}
	if ( pEvent->buttons() == Qt::NoButton && !m_bMouselook && ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT || m_Editor.IsWorldGridEdited() ) )
	{
		// The axis under the mouse lights up.
		const std::optional<CMapGizmo> gizmo = GetGizmo();
		const MapPreferences_t &look = CPreferences::Instance().GetMapPreferences();
		const int nHover = gizmo ? gizmo->HitAt( GetSizedCamera(), look.m_flGizmoPixels, pEvent->position(), mapstyle::GIZMO_SLACK * std::max( 1.0, look.m_flGizmoThickness ) ) : -1;
		if ( nHover != m_nGizmoHover )
		{
			m_nGizmoHover = nHover;
			RedrawLabels();
			UpdateCursor();
		}
	}
	if ( m_bMouselook || ( m_bSpaceHeld && ( pEvent->buttons() & Qt::LeftButton ) ) )
	{
		Look( delta );
		// Back to the middle, so the mouse never runs into the screen's edge (a compositor
		// that won't move the pointer leaves it where it is, and the movement is all there is).
		if ( m_bMouselook )
		{
			WarpToMiddle();
		}
		return;
	}
	if ( !bCameraTool || pEvent->buttons() == Qt::NoButton )
	{
		return;
	}
	DragCamera( pEvent->buttons(), delta );
}

//-----------------------------------------------------------------------------
// Purpose: The camera tool's drags, also Space's: left looks, right pans across the view,
//          both move forward and back.
//-----------------------------------------------------------------------------
void CMapView3D::DragCamera( Qt::MouseButtons buttons, QPointF delta )
{
	const bool bLeft = ( buttons & Qt::LeftButton ) != 0;
	const bool bRight = ( buttons & Qt::RightButton ) != 0;
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	const double flPan = preferences.m_flPanScale;
	if ( bLeft && bRight )
	{
		xv::map::Point_t move = xv::map::Scale( m_Camera.GetForward(), -delta.y() * flPan );
		if ( preferences.m_bBothButtonsStrafe )
		{
			// Sideways stays level, whatever the camera's pitch.
			xv::map::Point_t right = m_Camera.GetRight();
			right[ 2 ] = 0.0;
			const double flLength = std::hypot( right[ 0 ], right[ 1 ] );
			if ( flLength > 1e-6 )
				move = xv::map::Add( move, xv::map::Scale( right, delta.x() * flPan / flLength ) );
		}
		m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, move );
		Changed();
	}
	else if ( m_bBothDrag )
	{
		// One of the two let go: nothing until the other is too.
	}
	else if ( bLeft )
	{
		Look( delta );
	}
	else if ( bRight )
	{
		m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Add( xv::map::Scale( m_Camera.GetRight(), delta.x() * flPan ), xv::map::Scale( m_Camera.GetUp(), -delta.y() * flPan ) ) );
		Changed();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The point orbited is what the pointer is on, else the selection's centre, else a
//          point ahead; the camera turns to face it, staying as far away.
//-----------------------------------------------------------------------------
void CMapView3D::BeginOrbit( QPointF at )
{
	const auto [ from, dir ] = GetSizedCamera().Ray( at.x(), at.y() );
	const std::vector<xv::map::TraceHit_t> hits = HitsAt( at );
	const xv::map::Box_t selection = GetDocument().GetSelectionBounds();
	if ( !hits.empty() )
		m_OrbitCenter = xv::map::Add( from, xv::map::Scale( dir, hits.front().m_flDistance ) );
	else if ( !selection.IsEmpty() )
		m_OrbitCenter = selection.GetCenter();
	else
		m_OrbitCenter = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( m_Camera.GetForward(), ORBIT_AHEAD ) );
	const double flRadius = std::max( ORBIT_NEAREST, xv::map::Length( xv::map::Subtract( m_Camera.m_Eye, m_OrbitCenter ) ) );
	m_Camera.LookAt( m_Camera.m_Eye, m_OrbitCenter );
	m_Camera.m_Eye = xv::map::Subtract( m_OrbitCenter, xv::map::Scale( m_Camera.GetForward(), flRadius ) );
	Changed();
	m_bOrbit = true;
	CapturePointer( at.toPoint(), [ this ]( QPointF delta ) { Orbit( delta ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Across the mouse goes round the point, up and down over it; up stays up, so the
//          camera stops short of the poles.
//-----------------------------------------------------------------------------
void CMapView3D::Orbit( QPointF delta )
{
	if ( delta.isNull() )
		return;
	const double flRadius = xv::map::Length( xv::map::Subtract( m_Camera.m_Eye, m_OrbitCenter ) );
	const double flSensitivity = CPreferences::Instance().GetMapPreferences().m_flSensitivity;
	m_Camera.m_flYaw -= delta.x() * flSensitivity;
	m_Camera.m_flPitch = std::clamp( m_Camera.m_flPitch + delta.y() * flSensitivity, -89.0, 89.0 );
	m_Camera.m_Eye = xv::map::Subtract( m_OrbitCenter, xv::map::Scale( m_Camera.GetForward(), flRadius ) );
	Changed();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::EndOrbit()
{
	if ( !m_bOrbit )
		return;
	m_bOrbit = false;
	ReleasePointer();
}

//-----------------------------------------------------------------------------
// Purpose: Whatever a button began with a tool, given up (as Escape gives it up).
//-----------------------------------------------------------------------------
void CMapView3D::CancelDrags()
{
	if ( m_nGizmoDrag >= 0 )
		EndGizmo( false, m_GizmoFrom, Qt::NoModifier );
	if ( m_nBoxFace >= 0 || m_nBoxPlane >= 0 )
		EndBoxDrag( false );
	if ( m_BlockPlane )
		EndBlockDrag( false );
	if ( m_pOverlayScene )
		EndOverlayDrag( false );
	if ( m_pPaintScene )
		EndPaint( false );
}

//-----------------------------------------------------------------------------
// Purpose: Ends a drag begun with Space held, letting the pointer go.
//-----------------------------------------------------------------------------
void CMapView3D::EndSpaceDrag()
{
	if ( !m_bSpaceDrag )
		return;
	m_bSpaceDrag = false;
	m_bBothDrag = false;
	m_Dragging = Qt::NoButton;
	if ( !m_bMouselook )
		ReleasePointer();
}

//-----------------------------------------------------------------------------
// Purpose: A click (not a drag) with the selection tool picks what is under it.
//-----------------------------------------------------------------------------
void CMapView3D::mouseReleaseEvent( QMouseEvent *pEvent )
{
	if ( m_bOrbit )
	{
		if ( pEvent->button() == Qt::RightButton )
			EndOrbit();
		return;
	}
	if ( m_bMenuPending && pEvent->button() == Qt::RightButton )
	{
		m_bMenuPending = false;
		if ( ( pEvent->position() - m_MenuAt ).manhattanLength() <= QApplication::startDragDistance() )
		{
			const std::vector<xv::map::TraceHit_t> hits = HitsAt( m_MenuAt );
			m_Editor.ShowObjectMenu( this, mapToGlobal( m_MenuAt.toPoint() ), hits.empty() ? std::nullopt : std::optional<std::uint32_t>{ hits.front().m_nObject }, FaceAt( m_MenuAt ) );
		}
		return;
	}
	if ( m_bSpaceDrag )
	{
		m_Dragging = pEvent->buttons();
		if ( m_Dragging == Qt::NoButton )
			EndSpaceDrag();
		return;
	}
	if ( m_Editor.GetBrushEdit().HandleEvent( *this, pEvent ) )
	{
		pEvent->accept();
		return;
	}
	if ( m_bConsumeModalRelease )
	{
		m_bConsumeModalRelease = false;
		m_Dragging = pEvent->buttons();
		return;
	}
	const bool bWasLeft = ( m_Dragging & Qt::LeftButton ) != 0;
	m_Dragging = pEvent->buttons();
	if ( m_pOverlayScene && pEvent->button() == Qt::LeftButton )
	{
		PreviewOverlayDrag( pEvent->position() );
		EndOverlayDrag( true );
		return;
	}
	if ( m_nGizmoDrag >= 0 && pEvent->button() == Qt::LeftButton )
	{
		EndGizmo( true, pEvent->position(), pEvent->modifiers() );
		return;
	}
	if ( ( m_nBoxFace >= 0 || m_nBoxPlane >= 0 ) && pEvent->button() == Qt::LeftButton )
	{
		DragBox( pEvent->position(), pEvent->modifiers() );
		EndBoxDrag( true );
		return;
	}
	if ( m_BlockPlane && pEvent->button() == Qt::LeftButton )
	{
		DragBlock( pEvent->position(), pEvent->modifiers() );
		EndBlockDrag( true );
		return;
	}
	if ( m_RadiusFrom && ( pEvent->button() == Qt::LeftButton || pEvent->button() == Qt::RightButton ) )
	{
		m_RadiusFrom.reset();
		return;
	}
	if ( m_pPaintScene && ( pEvent->button() == Qt::LeftButton || pEvent->button() == Qt::RightButton ) )
	{
		EndPaint( true );
		return;
	}
	if ( m_pSprinkleStroke && pEvent->button() == Qt::LeftButton )
	{
		EndSprinkle( true );
		return;
	}
	if ( !bWasLeft || m_bMoved || m_bSpaceHeld || m_bMouselook || pEvent->button() != Qt::LeftButton )
	{
		return;
	}
	if ( !m_Editor.GetFacePickKey().isEmpty() )
	{
		if ( const auto face = FaceAt( pEvent->position(), true ) )
			m_Editor.AssignOverlayFace( *face, m_Editor.GetFacePickKey(), pEvent->modifiers().testFlag( Qt::ControlModifier ), !pEvent->modifiers().testFlag( Qt::ControlModifier ) );
		return;
	}
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_OVERLAY )
	{
		if ( pEvent->modifiers().testFlag( Qt::ShiftModifier ) )
		{
			if ( const auto face = FaceAt( pEvent->position(), true ) )
				m_Editor.AssignOverlayFace( *face, QStringLiteral( "sides" ), false, false );
			return;
		}
		for ( const auto &hit : HitsAt( pEvent->position() ) )
		{
			const auto *pScene = GetDocument().GetScene();
			if ( pScene && !pScene->IsSolidObject( hit.m_nObject ) )
			{
				const auto szClass = pScene->m_Map.m_Entities[ pScene->GetIndex( hit.m_nObject ) ].GetClassname();
				if ( szClass == "info_overlay" || szClass == "info_overlay_transition" )
				{
					GetDocument().SetSelection( { hit.m_nObject } );
					return;
				}
			}
		}
		if ( const auto face = FaceAt( pEvent->position() ) )
			if ( const auto surface = SurfaceAt( pEvent->position() ) )
				m_Editor.PlaceOverlay( surface->first, surface->second, *face );
		return;
	}
	// The physics tool picks as the selection tool does while it is stopped, so what falls next
	// can be chosen without switching tools.
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT || ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_PHYSICS && !m_Editor.GetPhysicsTool().IsRunning() ) )
	{
		Select( pEvent->position(), pEvent->modifiers().testFlag( Qt::ControlModifier ) );
	}
	else if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_DECAL )
	{
		if ( pEvent->modifiers().testFlag( Qt::ShiftModifier ) )
		{
			const auto *pScene = GetDocument().GetScene();
			for ( const auto &hit : HitsAt( pEvent->position() ) )
			{
				if ( pScene && !pScene->IsSolidObject( hit.m_nObject ) && pScene->m_Map.m_Entities[ pScene->GetIndex( hit.m_nObject ) ].GetClassname() == "infodecal" )
				{
					GetDocument().SetSelection( { hit.m_nObject } );
					break;
				}
			}
		}
		else if ( FaceAt( pEvent->position() ) )
		{
			if ( const auto surface = SurfaceAt( pEvent->position() ) )
				m_Editor.PlaceDecal( surface->first );
		}
	}
	else if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_ENTITY )
	{
		// Hammer's entity tool puts it straight on what was clicked.
		if ( const auto surface = SurfaceAt( pEvent->position() ) )
		{
			m_Editor.PlaceEntityOn( surface->first, surface->second );
		}
	}
	else if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_TEXTURE )
	{
		if ( const auto face = FaceAt( pEvent->position() ) )
		{
			m_Editor.ClickFace( *face, pEvent->modifiers() );
		}
		else if ( !pEvent->modifiers().testFlag( Qt::ControlModifier ) )
		{
			m_Editor.SetSelectedFaces( {} );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<xv::map::TraceHit_t> CMapView3D::HitsAt( QPointF at )
{
	const MapFrame_t frame = MakeFrame( size() );
	if ( !frame.m_pScene )
	{
		return {};
	}
	const auto [ from, dir ] = frame.m_Camera.Ray( at.x(), at.y() );
	return xv::map::TraceScene( frame.MakeScene(), m_Editor.GetSettings().m_bToolTextures, m_Editor.GetSettings().m_bNodraw, from, dir, m_Camera.m_flFar );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::map::FaceRef_t> CMapView3D::FaceAt( QPointF at, bool bThroughOverlays )
{
	return FacesAt( std::span<const QPointF>( &at, 1 ), bThroughOverlays ).front();
}

//-----------------------------------------------------------------------------
// Purpose: Along each point's ray, as the view casts it, up to just short of the point.
//-----------------------------------------------------------------------------
std::vector<bool> CMapView3D::BrushesHide( std::span<const xv::map::Point_t> points, std::span<const std::uint32_t> objects )
{
	std::vector<bool> hidden( points.size(), false );
	const MapFrame_t frame = MakeFrame( size() );
	if ( !frame.m_pScene )
		return hidden;
	const xv::map::RasterScene_t scene = frame.MakeScene();
	std::vector<const xv::map::CMapMesh *> brushes{ &frame.m_pScene->GetMesh(), &frame.m_pScene->GetEditMesh() };
	for ( const auto &entry : frame.m_pScene->m_SolidMeshes )
		brushes.push_back( entry.second.get() );
	for ( std::size_t i = 0; i < points.size(); i++ )
	{
		const auto screen = frame.m_Camera.Project( points[ i ] );
		if ( !screen )
			continue;
		const auto [ from, dir ] = frame.m_Camera.Ray( ( *screen )[ 0 ], ( *screen )[ 1 ] );
		const double flDistance = xv::map::Dot( xv::map::Subtract( points[ i ], from ), dir ) - 0.1;
		hidden[ i ] = flDistance > 0 && xv::map::TraceBlocked( scene, m_Editor.GetSettings().m_bToolTextures, m_Editor.GetSettings().m_bNodraw, from, dir, flDistance, brushes, objects[ i ] );
	}
	return hidden;
}

//-----------------------------------------------------------------------------
// Purpose: Only the map's own meshes (as read, and the edit mesh) map triangles back to sides.
//-----------------------------------------------------------------------------
std::vector<std::optional<xv::map::FaceRef_t>> CMapView3D::FacesAt( std::span<const QPointF> points, bool bThroughOverlays )
{
	std::vector<std::optional<xv::map::FaceRef_t>> faces( points.size() );
	const MapFrame_t frame = MakeFrame( size() );
	if ( !frame.m_pScene )
	{
		return faces;
	}
	const xv::map::RasterScene_t scene = frame.MakeScene();
	const auto isMapMesh = [ & ]( const xv::map::CMapMesh &mesh ) {
		return &mesh == &frame.m_pScene->GetMesh() || &mesh == &frame.m_pScene->GetEditMesh() || std::ranges::any_of( frame.m_pScene->m_SolidMeshes, [ &mesh ]( const auto &entry ) { return entry.second.get() == &mesh; } );
	};
	for ( std::size_t i = 0; i < points.size(); i++ )
	{
		const auto [ from, dir ] = frame.m_Camera.Ray( points[ i ].x(), points[ i ].y() );
		const std::vector<xv::map::TraceHit_t> hits = xv::map::TraceScene( scene, m_Editor.GetSettings().m_bToolTextures, m_Editor.GetSettings().m_bNodraw, from, dir, m_Camera.m_flFar );
		for ( const auto &hit : hits )
		{
			const xv::map::CMapMesh &mesh = *scene.m_Meshes[ hit.m_nMesh ];
			if ( isMapMesh( mesh ) && hit.m_nTriangle < mesh.m_TriangleFaces.size() )
			{
				const auto &face = mesh.m_Faces[ mesh.m_TriangleFaces[ hit.m_nTriangle ] ];
				faces[ i ] = xv::map::FaceRef_t{ face.m_nSolid, face.m_nSide };
				break;
			}
			if ( bThroughOverlays && !frame.m_pScene->IsSolidObject( hit.m_nObject ) )
			{
				const auto szClass = frame.m_pScene->m_Map.m_Entities[ frame.m_pScene->GetIndex( hit.m_nObject ) ].GetClassname();
				if ( szClass == "info_overlay" || szClass == "infodecal" || szClass == "info_overlay_transition" )
					continue;
			}
			break;
		}
	}
	return faces;
}

//-----------------------------------------------------------------------------
// Purpose: The nearest hit's triangle gives the surface's direction.
//-----------------------------------------------------------------------------
std::optional<std::pair<xv::map::Point_t, xv::map::Point_t>> CMapView3D::SurfaceAt( QPointF at )
{
	const MapFrame_t frame = MakeFrame( size() );
	if ( !frame.m_pScene )
	{
		return std::nullopt;
	}
	const xv::map::RasterScene_t scene = frame.MakeScene();
	const auto [ from, dir ] = frame.m_Camera.Ray( at.x(), at.y() );
	const std::vector<xv::map::TraceHit_t> hits = xv::map::TraceScene( scene, m_Editor.GetSettings().m_bToolTextures, m_Editor.GetSettings().m_bNodraw, from, dir, m_Camera.m_flFar );
	if ( hits.empty() )
	{
		return std::nullopt;
	}
	const xv::map::TraceHit_t &hit = hits.front();
	const xv::map::CMapMesh &mesh = *scene.m_Meshes[ hit.m_nMesh ];
	std::array<xv::map::Point_t, 3> corners{};
	for ( std::size_t k = 0; k < 3; k++ )
	{
		const auto &p = mesh.m_Vertices[ mesh.m_Indices[ hit.m_nTriangle * 3 + k ] ].m_vecPosition;
		corners[ k ] = { p.m_flX, p.m_flY, p.m_flZ };
	}
	xv::map::Point_t normal = xv::map::Normalized( xv::map::Cross( xv::map::Subtract( corners[ 1 ], corners[ 0 ] ), xv::map::Subtract( corners[ 2 ], corners[ 0 ] ) ) );
	if ( xv::map::Dot( normal, dir ) > 0.0 )
	{
		normal = xv::map::Scale( normal, -1.0 );
	}
	return std::pair{ xv::map::Add( from, xv::map::Scale( dir, hit.m_flDistance ) ), normal };
}

//-----------------------------------------------------------------------------
// Purpose: The stroke's main-thread stages, for the overlay and the corpus benchmark.
//-----------------------------------------------------------------------------
QString CMapView3D::GetPaintCosts() const
{
	return tr( "paint: %1 grid, %2 edit, %3 scene, %4 notify (ms)" ).arg( m_flGridPaintMs, 0, 'f', 2 ).arg( m_flDispEditMs, 0, 'f', 2 ).arg( GetDocument().GetPreviewTime(), 0, 'f', 2 ).arg( GetDocument().GetPreviewNotifyTime(), 0, 'f', 2 ) + tr( "; %1 samples, %2 previews in %3 ms; start %4 ms, commit %5 ms" ).arg( m_nPaintSamples ).arg( m_nPaintPreviews ).arg( m_PaintClock.isValid() ? m_PaintClock.elapsed() : 0 ).arg( m_flPaintStartMs, 0, 'f', 2 ).arg( m_flPaintCommitMs, 0, 'f', 2 ) + QStringLiteral( "; " ) + CMapEventClock::Get().Describe();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::UpdateCursor()
{
	if ( m_pOverlayScene && m_Editor.GetTool() != MapTool_t::MAP_TOOL_OVERLAY )
		EndOverlayDrag( false );
	if ( m_pPaintScene && !IsPaintingDisplacements() )
		EndPaint( false );
	if ( m_pSprinkleStroke && !IsSprinkling() )
		EndSprinkle( false );
	if ( m_bMouselook )
	{
		return;
	}
	// A handle being dragged, one that can be, or a brush (or a face pick) to aim.
	Qt::CursorShape nShape = ( IsPaintingDisplacements() || IsSprinkling() || !m_Editor.GetFacePickKey().isEmpty() ) ? Qt::CrossCursor : Qt::ArrowCursor;
	const bool bBlock = m_Editor.GetTool() == MapTool_t::MAP_TOOL_BLOCK;
	if ( m_nGizmoDrag >= 0 || m_pOverlayScene || m_nBoxFace >= 0 )
		nShape = Qt::ClosedHandCursor;
	else if ( m_nBoxPlane >= 0 || ( bBlock && m_bBoxHover && m_nBoxFaceHover < 0 ) )
		nShape = Qt::SizeAllCursor;
	else if ( m_nGizmoHover >= 0 || ( bBlock && m_nBoxFaceHover >= 0 ) || OverlayHandleAt( mapFromGlobal( QCursor::pos() ) ) )
		nShape = Qt::OpenHandCursor;
	if ( cursor().shape() != nShape )
	{
		setCursor( nShape );
	}
	if ( IsBrushShown() && !m_pPaintScene )
	{
		const QPoint at = mapFromGlobal( QCursor::pos() );
		if ( rect().contains( at ) )
			m_BrushAt = SurfaceAt( at );
	}
	if ( !IsBrushShown() && m_BrushAt && !m_pPaintScene )
	{
		m_BrushAt.reset();
		RedrawLabels();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapView3D::IsPaintingDisplacements() const
{
	return m_Editor.GetTool() == MapTool_t::MAP_TOOL_TEXTURE && m_Editor.GetFaceEdit().IsDisplacementTabVisible() && m_Editor.GetFaceEdit().GetDispPaintMode() != DispPaintMode_t::DISP_PAINT_NONE;
}

//-----------------------------------------------------------------------------
// Purpose: Select can display an aiming ring without granting permission to paint.
//-----------------------------------------------------------------------------
bool CMapView3D::IsDisplacementBrushShown() const
{
	return IsPaintingDisplacements() || ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_TEXTURE && m_Editor.GetFaceEdit().IsDisplacementTabVisible() && CPreferences::Instance().GetMapPreferences().m_bShowDisplacementBrush );
}

//-----------------------------------------------------------------------------
// Purpose: Expose whether the view currently has a surface to draw the permitted ring on.
//-----------------------------------------------------------------------------
bool CMapView3D::HasPaintBrushRing() const
{
	return m_BrushAt.has_value() && IsBrushShown();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapView3D::IsBrushShown() const
{
	return IsDisplacementBrushShown() || IsSprinkling();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CMapView3D::GetBrushRadius() const
{
	return IsSprinkling() ? m_Editor.GetSprinkle().GetRadius() : m_Editor.GetFaceEdit().GetBrushRadius();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::SetBrushRadius( double flRadius )
{
	if ( IsSprinkling() )
		m_Editor.GetSprinkle().SetRadius( flRadius );
	else
		m_Editor.GetFaceEdit().SetBrushRadius( flRadius );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapView3D::IsSprinkling() const
{
	return m_Editor.GetTool() == MapTool_t::MAP_TOOL_SPRINKLE;
}

//-----------------------------------------------------------------------------
// Purpose: The stroke starts on the scene as it is, and its rays see only brushes and
//          displacements, never what it (or anything else) placed.
//-----------------------------------------------------------------------------
void CMapView3D::BeginSprinkle( QPointF at, Qt::KeyboardModifiers modifiers )
{
	const xv::map::SprinkleType_t *pType = m_Editor.GetSprinkle().GetType();
	if ( !pType || !GetDocument().GetScene() )
	{
		emit m_Editor.StatusMessage( tr( "No sprinkle type to paint with: add definitions to the game's scripts/hammer/sprinkle" ) );
		return;
	}
	auto trace = [ this ]( xv::map::Point_t from, xv::map::Point_t dir, double flMax ) -> std::optional<std::pair<xv::map::Point_t, xv::map::Point_t>> {
		if ( !m_SprinkleFrame || !m_SprinkleFrame->m_pScene )
			return std::nullopt;
		const xv::map::RasterScene_t &scene = m_SprinkleRaster;
		for ( const xv::map::TraceHit_t &hit : xv::map::TraceScene( scene, m_Editor.GetSettings().m_bToolTextures, m_Editor.GetSettings().m_bNodraw, from, dir, flMax ) )
		{
			if ( !m_SprinkleFrame->m_pScene->IsSolidObject( hit.m_nObject ) )
				continue;
			const xv::map::CMapMesh &mesh = *scene.m_Meshes[ hit.m_nMesh ];
			std::array<xv::map::Point_t, 3> corners{};
			for ( std::size_t k = 0; k < 3; k++ )
				corners[ k ] = xv::map::ToPoint( mesh.m_Vertices[ mesh.m_Indices[ hit.m_nTriangle * 3 + k ] ].m_vecPosition );
			xv::map::Point_t normal = xv::map::Normalized( xv::map::Cross( xv::map::Subtract( corners[ 1 ], corners[ 0 ] ), xv::map::Subtract( corners[ 2 ], corners[ 0 ] ) ) );
			if ( xv::map::Dot( normal, dir ) > 0.0 )
				normal = xv::map::Scale( normal, -1.0 );
			return std::pair{ xv::map::Add( from, xv::map::Scale( dir, hit.m_flDistance ) ), normal };
		}
		return std::nullopt;
	};
	const auto nSeed = static_cast<std::uint32_t>( QDateTime::currentMSecsSinceEpoch() ) ^ ++m_nSprinkleStrokes;
	m_pSprinkleStroke = std::make_unique<xv::map::CSprinkleStroke>( GetDocument().GetSharedScene(), *pType, m_Editor.GetSprinkle().GetOptions( nSeed ), std::move( trace ) );
	SprinkleAt( at, modifiers );
}

//-----------------------------------------------------------------------------
// Purpose: A dab where the surface under the mouse is (Ctrl: taking away).
//-----------------------------------------------------------------------------
void CMapView3D::SprinkleAt( QPointF at, Qt::KeyboardModifiers modifiers )
{
	m_SprinkleLast = at.toPoint();
	if ( !m_pSprinkleStroke )
		return;
	const auto surface = SurfaceAt( at );
	m_BrushAt = surface;
	RedrawLabels();
	if ( !surface )
		return;
	const auto nMode = modifiers.testFlag( Qt::ControlModifier ) ? std::optional{ xv::map::SprinkleMode_t::SPRINKLE_MODE_SUBTRACTIVE } : std::nullopt;
	m_SprinkleFrame = MakeFrame( size() );
	m_SprinkleRaster = m_SprinkleFrame->MakeScene();
	const bool bChanged = m_pSprinkleStroke->Dab( surface->first, surface->second, GetBrushRadius(), nMode );
	m_SprinkleRaster = {};
	m_SprinkleFrame.reset();
	if ( bChanged )
	{
		GetDocument().Preview( m_pSprinkleStroke->GetEdit() );
		emit m_Editor.StatusMessage( tr( "Sprinkle: %n added", "", static_cast<int>( m_pSprinkleStroke->GetAddedCount() ) ) + tr( ", %n taken away", "", static_cast<int>( m_pSprinkleStroke->GetRemovedCount() ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::EndSprinkle( bool bCommit )
{
	if ( !m_pSprinkleStroke )
		return;
	const std::unique_ptr<xv::map::CSprinkleStroke> pStroke = std::move( m_pSprinkleStroke );
	if ( GetDocument().IsPreviewing() )
		GetDocument().EndPreview();
	if ( !bCommit )
		return;
	xv::map::MapEdit_t edit = pStroke->GetEdit();
	if ( !edit.IsEmpty() )
		GetDocument().Edit( std::move( edit ), tr( "Sprinkle" ) );
}

//-----------------------------------------------------------------------------
// Purpose: The brush put where the surface is under the mouse (the surface as the stroke has
//          left it, so a stroke climbs what it raised), each painted displacement given the
//          sample, and the result shown.
//-----------------------------------------------------------------------------
void CMapView3D::PaintStroke( QPointF at )
{
	const std::optional<std::pair<xv::map::Point_t, xv::map::Point_t>> surface = SurfaceAt( at );
	if ( !surface || !m_pPaintScene )
	{
		return;
	}
	++m_nPaintSamples;
	m_BrushAt = surface;
	const CMapFaceEdit &sheet = m_Editor.GetFaceEdit();
	const bool bAlpha = m_bPaintAlpha;
	xv::map::DispBrush_t brush = sheet.GetBrush();
	brush.m_Centre = surface->first;
	if ( ( m_bPaintLower && brush.m_nEffect == xv::map::PaintEffect_t::PAINT_EFFECT_RAISE_LOWER ) || ( m_bPaintSculpt && m_PaintModifiers.testFlag( Qt::ControlModifier ) ) )
	{
		brush.m_flAmount = -brush.m_flAmount;
	}
	QElapsedTimer timer;
	timer.start();
	bool bChanged = false;
	std::size_t nStrokeFace = 0;
	for ( auto &[ face, disp ] : m_Painted )
	{
		const std::size_t nFace = nStrokeFace++;
		const xv::map::FaceGeometry_t *pFace = m_pPaintScene->m_Solids[ face.m_nSolid ].FindFace( face.m_nSide );
		if ( !pFace )
		{
			continue;
		}
		xv::map::DispBrush_t own = brush;
		if ( sheet.IsBrushAlongFaceNormal() )
		{
			own.m_Axis = pFace->m_Plane.m_Normal;
		}
		bool bPainted = false;
		if ( m_bPaintSculpt && m_PaintModifiers.testFlag( Qt::ShiftModifier ) )
		{
			own.m_nEffect = xv::map::PaintEffect_t::PAINT_EFFECT_SMOOTH;
			own.m_flAmount = 0.5;
			bPainted = xv::map::PaintGeometry( disp, *pFace, own );
		}
		else if ( m_bPaintSculpt )
		{
			xv::map::SculptBrush_t sculpt = sheet.GetSculptBrush();
			sculpt.m_Right = m_Camera.GetRight();
			const int nDirection = sheet.GetSculptDirection();
			if ( nDirection >= 0 && nDirection <= 2 )
			{
				own.m_Axis = {};
				own.m_Axis[ nDirection ] = 1;
			}
			else if ( nDirection == 4 )
			{
				own.m_Axis = xv::map::Scale( m_Camera.GetForward(), -1 );
			}
			bPainted = xv::map::SculptGeometry( disp, *pFace, own, sculpt, &m_SculptApplied[ nFace ] );
		}
		else
		{
			bPainted = bAlpha ? xv::map::PaintAlpha( disp, *pFace, own ) : xv::map::PaintGeometry( disp, *pFace, own );
		}
		if ( bPainted )
		{
			bChanged = true;
			if ( std::ranges::find( m_PaintChanged, face ) == m_PaintChanged.end() )
			{
				m_PaintChanged.push_back( face );
			}
		}
	}
	m_flGridPaintMs = timer.nsecsElapsed() / 1e6;
	if ( !bChanged )
	{
		return;
	}
	m_bPaintPending = true;
	if ( !m_bPaintPreviewUsed && !m_pPaintPreview->isActive() )
		m_pPaintPreview->start( 0 );
}

//-----------------------------------------------------------------------------
// Purpose: A submitted frame permits one more preview; mouse samples keep accumulating in
//          the grids while the GPU or software view is busy with the previous frame.
//-----------------------------------------------------------------------------
void CMapView3D::PaintFrameSubmitted()
{
	m_bPaintPreviewUsed = false;
	if ( m_bPaintPending && !m_pPaintPreview->isActive() )
		m_pPaintPreview->start( 0 );
}

//-----------------------------------------------------------------------------
// Purpose: Coalesce samples in one event loop pass without dropping their grid changes.
//-----------------------------------------------------------------------------
void CMapView3D::PreviewPaint()
{
	if ( !m_pPaintScene || !m_bPaintPending )
		return;
	if ( m_bPaintPreviewUsed )
		return;
	m_bPaintPending = false;
	m_bPaintPreviewUsed = true;
	++m_nPaintPreviews;
	QElapsedTimer timer;
	timer.start();
	const xv::map::MapEdit_t edit = xv::map::EditDisplacements( *m_pPaintScene, m_PaintChanged, [ & ]( std::vector<xv::map::DispFace_t> &refs ) {
		for ( xv::map::DispFace_t &ref : refs )
		{
			for ( const auto &[ face, disp ] : m_Painted )
			{
				if ( face == ref.m_Ref && ref.m_pDisp )
				{
					*ref.m_pDisp = disp;
				}
			}
		}
		return true;
	} );
	m_flDispEditMs = timer.nsecsElapsed() / 1e6;
	GetDocument().Preview( edit, true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::EndPaint( bool bCommit )
{
	if ( !m_pPaintScene )
	{
		return;
	}
	QElapsedTimer timer;
	timer.start();
	m_pPaintPreview->stop();
	if ( bCommit && m_bPaintPending )
	{
		m_bPaintPreviewUsed = false;
		PreviewPaint();
	}
	std::erase_if( m_Painted, [ this ]( const auto &entry ) { return std::ranges::find( m_PaintChanged, entry.first ) == m_PaintChanged.end(); } );
	if ( bCommit && !m_Painted.empty() )
	{
		m_Editor.CommitDisplacements( *m_pPaintScene, m_Painted, m_bPaintSculpt ? tr( "Sculpt" ) : m_bPaintAlpha ? tr( "Paint Alpha" )
																												 : tr( "Paint Geometry" ) );
	}
	else
		GetDocument().EndPreview();
	m_bPaintPending = false;
	m_pPaintScene.reset();
	m_flPaintCommitMs = timer.nsecsElapsed() / 1e6;
	m_Painted.clear();
}

//-----------------------------------------------------------------------------
// Purpose: Seam collapse depends on base geometry and powers, so cache its indices while a
//          stroke only changes positions. The marker positions still come from the live grid.
//-----------------------------------------------------------------------------
void CMapView3D::UpdateCollapsibleVertices()
{
	if ( !m_Editor.GetFaceEdit().IsCollapsibleShown() )
	{
		m_pCollapsibleScene.reset();
		m_CollapsibleFaces.clear();
		m_CollapsibleVertices.clear();
		return;
	}
	const auto pScene = GetDocument().GetDisplacementPreviewBase() ? m_pPaintScene : GetDocument().GetSharedScene();
	const auto &faces = m_Editor.GetSelectedFaces();
	if ( !pScene || ( pScene == m_pCollapsibleScene && faces == m_CollapsibleFaces ) )
		return;
	m_pCollapsibleScene = pScene;
	m_CollapsibleFaces = faces;
	m_CollapsibleVertices.assign( faces.size(), {} );
	std::vector<xv::map::DispMesh_t> neighbours;
	for ( std::uint32_t s = 0; s < pScene->m_Map.m_Solids.size(); ++s )
	{
		if ( pScene->m_Map.m_Solids[ s ].m_bDeleted )
			continue;
		for ( const xv::map::FaceGeometry_t &face : pScene->m_Solids[ s ].m_Faces )
		{
			const auto &disp = pScene->m_Map.m_Solids[ s ].m_Sides[ face.m_nSide ].m_pDisp;
			if ( const auto mesh = disp ? xv::map::BuildDisplacement( *disp, face ) : std::nullopt )
				neighbours.push_back( *mesh );
		}
	}
	for ( std::size_t f = 0; f < faces.size(); ++f )
	{
		const auto &ref = faces[ f ];
		if ( ref.m_nSolid >= pScene->m_Map.m_Solids.size() || ref.m_nSide >= pScene->m_Map.m_Solids[ ref.m_nSolid ].m_Sides.size() )
			continue;
		const auto &disp = pScene->m_Map.m_Solids[ ref.m_nSolid ].m_Sides[ ref.m_nSide ].m_pDisp;
		const auto *pFace = pScene->m_Solids[ ref.m_nSolid ].FindFace( ref.m_nSide );
		if ( const auto mesh = disp && pFace ? xv::map::BuildDisplacement( *disp, *pFace ) : std::nullopt )
		{
			m_CollapsibleVertices[ f ] = xv::map::CollapsibleVertices( *disp, *mesh, neighbours );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Project portal edges the same way for drawing and picking.
//-----------------------------------------------------------------------------
CMapView::PortalProjection_t CMapView3D::PortalProjection() const
{
	return [ camera = GetSizedCamera() ]( const xv::map::Point_t &a, const xv::map::Point_t &b ) -> std::optional<QLineF> {
		if ( const auto segment = camera.ProjectLine( a, b ) )
			return QLineF( QPointF( segment->first[ 0 ], segment->first[ 1 ] ), QPointF( segment->second[ 0 ], segment->second[ 1 ] ) );
		return std::nullopt;
	};
}

//-----------------------------------------------------------------------------
// Purpose: Edges in front of the near plane are cut where they cross it, so a box reaching
//          behind the camera is still drawn where it is in view. A dark edge round each line
//          keeps it seen against faces of its own colour.
//-----------------------------------------------------------------------------
void CMapView3D::PaintTools( QPainter &painter )
{
	UpdateCollapsibleVertices();
	const std::optional<xv::map::Box_t> &box = m_Editor.GetBlockBox();
	const std::optional<xv::map::Point_t> &marker = m_Editor.GetEntityMarker();
	const std::optional<CMapGizmo> gizmo = m_DragGizmo ? m_DragGizmo : GetGizmo();
	std::vector<xv::map::FaceRef_t> faces = m_Editor.GetSelectedFaces();
	const auto &highlighted = m_Editor.GetHighlightedSides();
	faces.insert( faces.end(), highlighted.begin(), highlighted.end() );
	const std::optional<std::vector<xv::map::Point_t>> &pointfile = GetDocument().GetPointfile();
	const bool bBrush = HasPaintBrushRing();
	const bool bOverlay = m_Editor.GetTool() == MapTool_t::MAP_TOOL_OVERLAY && !GetDocument().GetSelection().empty();
	const bool bWalkable = m_Editor.GetSettings().m_bWalkable && GetDocument().GetScene();
	// The selected entities' FGD helpers (Ctrl+Shift+H): lines to their targets, cones, radii.
	std::vector<const xv::map::EntityHelpers_t *> helpers;
	if ( const xv::map::CScene *pScene = GetDocument().GetScene(); pScene && m_Editor.GetSettings().m_bHelpers )
		for ( const std::uint32_t nObject : GetDocument().GetSelection() )
			if ( !pScene->IsSolidObject( nObject ) && !pScene->IsDeleted( nObject ) )
				if ( const xv::map::EntityHelpers_t &entity = GetDocument().GetHelpers( pScene->GetIndex( nObject ) ); !entity.m_Lines.empty() || !entity.m_Rings.empty() || !entity.m_Sides.empty() )
					helpers.push_back( &entity );
	// Every path's line to its next corner, selected or not, while both ends are shown.
	std::vector<const xv::map::PathLink_t *> paths;
	if ( const xv::map::CScene *pScene = GetDocument().GetScene(); pScene && m_Editor.GetSettings().m_bHelpers )
		for ( const xv::map::PathLink_t &link : GetDocument().GetPathLinks() )
			if ( GetDocument().IsShown( pScene->EntityObject( link.m_nFrom ) ) && GetDocument().IsShown( pScene->EntityObject( link.m_nTo ) ) )
				paths.push_back( &link );
	if ( !box && !marker && !gizmo && faces.empty() && !pointfile && !bBrush && !bWalkable && !bOverlay && !m_ModalTransform && helpers.empty() && paths.empty() )
	{
		return;
	}
	const xv::map::CViewCamera camera = GetSizedCamera();
	auto project = [ & ]( const std::vector<xv::map::Point_t> &points ) {
		QPolygonF polygon;
		for ( const auto &at : camera.ProjectPolygon( points ) )
			polygon << QPointF{ at[ 0 ], at[ 1 ] };
		return polygon;
	};
	if ( bOverlay )
	{
		const auto *pScene = GetDocument().GetScene();
		for ( const auto nObject : GetDocument().GetSelection() )
		{
			if ( !pScene || pScene->IsSolidObject( nObject ) )
				continue;
			const auto &entity = pScene->m_Map.m_Entities[ pScene->GetIndex( nObject ) ];
			const auto corners = xv::map::OverlayCorners( entity );
			if ( !corners )
				continue;
			painter.setPen( QPen{ Qt::white, 1.5 } );
			painter.setBrush( Qt::NoBrush );
			for ( int n = 0; n < 4; ++n )
			{
				if ( const auto edge = camera.ProjectLine( ( *corners )[ n ], ( *corners )[ ( n + 1 ) % 4 ] ) )
					painter.drawLine( QPointF{ edge->first[ 0 ], edge->first[ 1 ] }, QPointF{ edge->second[ 0 ], edge->second[ 1 ] } );
				if ( const auto at = camera.Project( ( *corners )[ n ] ) )
				{
					painter.setBrush( Qt::white );
					painter.drawRect( QRectF{ ( *at )[ 0 ] - 3, ( *at )[ 1 ] - 3, 6, 6 } );
					painter.setBrush( Qt::NoBrush );
				}
			}
			if ( const auto origin = xv::map::ParsePoint( entity.GetValue( "BasisOrigin" ) ) )
				if ( const auto at = camera.Project( *origin ) )
					painter.drawEllipse( QPointF{ ( *at )[ 0 ], ( *at )[ 1 ] }, 5, 5 );
		}
	}
	if ( bWalkable )
	{
		// Hammer's Display walkable area, on the selected displacements: steep triangles red,
		// walkable but too steep to build on yellow.
		const xv::map::CScene &scene = *GetDocument().GetScene();
		std::vector<xv::map::FaceRef_t> shown = xv::map::DisplacementFaces( scene, faces );
		for ( const xv::map::FaceRef_t &face : xv::map::DisplacementFaces( scene, xv::map::FacesOf( scene, GetDocument().GetSelection() ) ) )
		{
			shown.push_back( face );
		}
		painter.setRenderHint( QPainter::Antialiasing, true );
		painter.setPen( Qt::NoPen );
		for ( const xv::map::FaceRef_t &face : shown )
		{
			const xv::map::FaceGeometry_t *pFace = scene.m_Solids[ face.m_nSolid ].FindFace( face.m_nSide );
			const xv::map::DispInfo_t &disp = *scene.m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ].m_pDisp;
			const std::optional<xv::map::DispMesh_t> mesh = pFace ? xv::map::BuildDisplacement( disp, *pFace ) : std::nullopt;
			if ( !mesh )
			{
				continue;
			}
			for ( std::size_t t = 0; t * 3 + 2 < mesh->m_Indices.size() && t < disp.m_TriangleTags.size(); t++ )
			{
				if ( disp.m_TriangleTags[ t ] == xv::map::DISP_TAG_LEVEL )
				{
					continue;
				}
				const QPolygonF polygon = project( { mesh->m_Points[ mesh->m_Indices[ t * 3 ] ], mesh->m_Points[ mesh->m_Indices[ t * 3 + 1 ] ], mesh->m_Points[ mesh->m_Indices[ t * 3 + 2 ] ] } );
				if ( polygon.size() >= 3 )
				{
					painter.setBrush( disp.m_TriangleTags[ t ] == xv::map::DISP_TAG_WALKABLE ? QColor{ 255, 220, 0, 90 } : QColor{ 255, 0, 0, 110 } );
					painter.drawPolygon( polygon );
				}
			}
		}
		painter.setBrush( Qt::NoBrush );
		painter.setRenderHint( QPainter::Antialiasing, false );
	}
	// The texture tool's faces, tinted (over everything: a face behind another still shows). A
	// displacement shows as its triangles, tinted, with its grid, as Hammer shows a selected one
	// (the Displacement tab's masks leave out either).
	if ( const xv::map::CScene *pScene = GetDocument().GetScene(); !faces.empty() && pScene )
	{
		const MapColors_t &colors = CPreferences::Instance().GetMapPreferences().m_Colors;
		QColor fill = colors.m_Selection3D;
		fill.setAlpha( 110 );
		const QPen edge{ colors.m_SelectionEdge3D, 1.5 };
		QColor gridColor = colors.m_SelectionEdge3D;
		gridColor.setAlpha( 200 );
		const QPen grid{ gridColor, 1.0 };

		const bool bGrid = !m_Editor.GetFaceEdit().IsGridMasked();
		painter.setRenderHint( QPainter::Antialiasing, true );

		for ( const xv::map::FaceRef_t &face : faces )
		{
			const bool bShade = std::ranges::find( highlighted, face ) != highlighted.end() || ( !m_Editor.GetFaceEdit().IsSelectMasked() && !m_Editor.IsFaceMaskHidden() );
			fill = m_Editor.IsSecondarySides() && std::ranges::find( highlighted, face ) != highlighted.end() ? QColor{ 0, 180, 255, 110 } : colors.m_Selection3D;
			fill.setAlpha( 110 );
			const xv::map::FaceGeometry_t *pFace = face.m_nSolid < pScene->m_Solids.size() ? pScene->m_Solids[ face.m_nSolid ].FindFace( face.m_nSide ) : nullptr;
			if ( !pFace )
			{
				continue;
			}
			const xv::map::Side_t &side = pScene->m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ];
			if ( const std::optional<xv::map::DispMesh_t> mesh = side.m_pDisp ? xv::map::BuildDisplacement( *side.m_pDisp, *pFace ) : std::nullopt )
			{
				painter.setPen( bGrid ? grid : QPen{ Qt::NoPen } );
				painter.setBrush( bShade ? QBrush{ fill } : QBrush{ Qt::NoBrush } );
				if ( bShade || bGrid )
				{
					for ( std::size_t t = 0; t + 2 < mesh->m_Indices.size(); t += 3 )
					{
						painter.drawPolygon( project( { mesh->m_Points[ mesh->m_Indices[ t ] ], mesh->m_Points[ mesh->m_Indices[ t + 1 ] ], mesh->m_Points[ mesh->m_Indices[ t + 2 ] ] } ) );
					}
				}
				if ( m_Editor.GetFaceEdit().IsCollapsibleShown() )
				{
					const auto it = std::ranges::find( m_CollapsibleFaces, face );
					if ( it != m_CollapsibleFaces.end() )
					{
						painter.setPen( Qt::NoPen );
						painter.setBrush( QColor( 255, 80, 220 ) );
						for ( const std::size_t i : m_CollapsibleVertices[ static_cast<std::size_t>( it - m_CollapsibleFaces.begin() ) ] )
						{
							if ( const auto at = camera.Project( mesh->m_Points[ i ] ) )
								painter.drawRect( QRectF( ( *at )[ 0 ] - 3, ( *at )[ 1 ] - 3, 6, 6 ) );
						}
					}
				}

				continue;
			}
			painter.setPen( edge );
			painter.setBrush( bShade ? QBrush{ fill } : QBrush{ Qt::NoBrush } );
			painter.drawPolygon( project( pFace->m_Points ) );
		}
		painter.setBrush( Qt::NoBrush );
		painter.setRenderHint( QPainter::Antialiasing, false );
	}
	std::vector<QLineF> lines;
	auto line = [ & ]( xv::map::Point_t a, xv::map::Point_t b ) {
		if ( const auto projected = camera.ProjectLine( a, b ) )
		{
			const auto &[ pa, pb ] = *projected;
			lines.emplace_back( QPointF{ pa[ 0 ], pa[ 1 ] }, QPointF{ pb[ 0 ], pb[ 1 ] } );
		}
	};
	auto drawHelper = [ & ]( const xv::map::HelperLine_t &helper ) {
		if ( const auto projected = camera.ProjectLine( helper.m_From, helper.m_To ) )
		{
			const auto &[ pa, pb ] = *projected;
			painter.setPen( QPen{ QColor{ helper.m_Color.m_nR, helper.m_Color.m_nG, helper.m_Color.m_nB }, 1.5 } );
			painter.drawLine( QPointF{ pa[ 0 ], pa[ 1 ] }, QPointF{ pb[ 0 ], pb[ 1 ] } );
		}
	};
	if ( !paths.empty() )
	{
		painter.setRenderHint( QPainter::Antialiasing, true );
		for ( const xv::map::PathLink_t *pLink : paths )
			for ( const xv::map::HelperLine_t &helper : pLink->m_Lines )
				drawHelper( helper );
		painter.setRenderHint( QPainter::Antialiasing, false );
	}
	if ( !helpers.empty() )
	{
		const xv::map::CScene &scene = *GetDocument().GetScene();
		painter.setRenderHint( QPainter::Antialiasing, true );
		for ( const xv::map::EntityHelpers_t *pEntity : helpers )
		{
			const xv::map::EntityHelpers_t &entity = *pEntity;
			for ( const auto *pLines : { &entity.m_Lines, &entity.m_Rings } )
			{
				for ( const xv::map::HelperLine_t &helper : *pLines )
					drawHelper( helper );
			}
			// A side list's faces, tinted as Hammer marks a cubemap's.
			if ( !entity.m_Sides.empty() )
			{
				painter.setPen( Qt::NoPen );
				painter.setBrush( QColor{ 255, 160, 0, 90 } );
				for ( std::uint32_t nSolid = 0; nSolid < scene.m_Map.m_Solids.size(); nSolid++ )
				{
					const auto &solid = scene.m_Map.m_Solids[ nSolid ];
					for ( std::uint32_t nSide = 0; nSide < solid.m_Sides.size(); nSide++ )
					{
						if ( solid.m_bDeleted || std::ranges::find( entity.m_Sides, solid.m_Sides[ nSide ].m_nId ) == entity.m_Sides.end() )
							continue;
						if ( const xv::map::FaceGeometry_t *pFace = scene.m_Solids[ nSolid ].FindFace( nSide ) )
							painter.drawPolygon( project( pFace->m_Points ) );
					}
				}
				painter.setBrush( Qt::NoBrush );
			}
		}
		painter.setRenderHint( QPainter::Antialiasing, false );
	}
	if ( box )
	{
		const MapGridPlane_t frame = BoxFrame();
		auto corner = [ & ]( int n ) { return frame.ToWorld( xv::map::Point_t{ ( n & 1 ) ? box->m_Maxs[ 0 ] : box->m_Mins[ 0 ], ( n & 2 ) ? box->m_Maxs[ 1 ] : box->m_Mins[ 1 ], ( n & 4 ) ? box->m_Maxs[ 2 ] : box->m_Mins[ 2 ] } ); };
		for ( int n = 0; n < 8; n++ )
		{
			for ( const int nBit : { 1, 2, 4 } )
			{
				if ( ( n & nBit ) == 0 )
				{
					line( corner( n ), corner( n | nBit ) );
				}
			}
		}
	}
	painter.setRenderHint( QPainter::Antialiasing, true );
	auto drawEdged = [ & ]( const QColor &color ) {
		painter.setPen( QPen{ QColor{ 0, 0, 0, 160 }, 3.0 } );
		painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
		painter.setPen( QPen{ color, 1.0 } );
		painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
	};
	drawEdged( CPreferences::Instance().GetMapPreferences().m_Colors.m_ToolBlock );
	// The block tool's arrows: one out of the middle of each face, in its axis's colour, to drag
	// the face out or in. Not while the keys move the box (G, R, E).
	if ( box && !box->IsEmpty() && m_Editor.GetTool() == MapTool_t::MAP_TOOL_BLOCK && !m_ModalTransform )
	{
		const MapPreferences_t &look = CPreferences::Instance().GetMapPreferences();
		for ( int f = 0; f < 6; f++ )
		{
			const std::optional<BoxArrow_t> arrow = BoxArrow( *box, f );
			if ( !arrow )
				continue;
			const bool bHot = f == m_nBoxFaceHover || f == m_nBoxFace;
			const QColor color = bHot ? look.m_Colors.m_SelectionEdge3D : f / 2 == 0 ? mapstyle::AXIS_X
				: f / 2 == 1                                                         ? mapstyle::AXIS_Y
																					 : mapstyle::AXIS_Z;
			const QPointF along = arrow->m_To - arrow->m_From;
			const double flLength = std::hypot( along.x(), along.y() );
			if ( flLength < 1.0 )
			{
				// Pointing at the camera (or away): a dot.
				painter.setPen( QPen{ QColor{ 0, 0, 0, 160 }, 1.5 } );
				painter.setBrush( color );
				painter.drawEllipse( arrow->m_From, 4.0, 4.0 );
				continue;
			}
			const QPointF unit = along / flLength;
			const QPointF side{ -unit.y(), unit.x() };
			const QPointF neck = arrow->m_To - unit * BOX_ARROW_HEAD;
			const QPolygonF head{ { arrow->m_To, neck + side * ( BOX_ARROW_HEAD * 0.55 ), neck - side * ( BOX_ARROW_HEAD * 0.55 ) } };
			painter.setBrush( Qt::NoBrush );
			painter.setPen( QPen{ QColor{ 0, 0, 0, 160 }, bHot ? 5.5 : 4.5, Qt::SolidLine, Qt::RoundCap } );
			painter.drawLine( arrow->m_From, neck );
			painter.setPen( QPen{ color, bHot ? 3.0 : 2.0, Qt::SolidLine, Qt::RoundCap } );
			painter.drawLine( arrow->m_From, neck );
			painter.setPen( QPen{ QColor{ 0, 0, 0, 160 }, 1.0 } );
			painter.setBrush( color );
			painter.drawPolygon( head );
		}
		painter.setBrush( Qt::NoBrush );
	}
	if ( bBrush )
	{
		// The brush's ring on the surface, the radius the sheet has.
		lines.clear();
		const xv::map::Point_t &centre = m_BrushAt->first;
		const xv::map::Point_t normal = xv::map::Length( m_BrushAt->second ) > 1e-9 ? xv::map::Normalized( m_BrushAt->second ) : xv::map::Point_t{ 0, 0, 1 };
		const xv::map::Point_t side = std::abs( normal[ 2 ] ) < 0.9 ? xv::map::Point_t{ 0, 0, 1 } : xv::map::Point_t{ 1, 0, 0 };
		const xv::map::Point_t u = xv::map::Normalized( xv::map::Cross( normal, side ) );
		const xv::map::Point_t v = xv::map::Cross( normal, u );
		const double flRadius = GetBrushRadius();
		constexpr int SEGMENTS = 36;
		for ( int i = 0; i < SEGMENTS; i++ )
		{
			const double a0 = i * 2.0 * std::numbers::pi / SEGMENTS;
			const double a1 = ( i + 1 ) * 2.0 * std::numbers::pi / SEGMENTS;
			line( xv::map::Add( centre, xv::map::Add( xv::map::Scale( u, std::cos( a0 ) * flRadius ), xv::map::Scale( v, std::sin( a0 ) * flRadius ) ) ), xv::map::Add( centre, xv::map::Add( xv::map::Scale( u, std::cos( a1 ) * flRadius ), xv::map::Scale( v, std::sin( a1 ) * flRadius ) ) ) );
		}
		if ( !IsSprinkling() && m_Editor.GetFaceEdit().GetDispPaintMode() == DispPaintMode_t::DISP_PAINT_SCULPT )
		{
			const xv::map::SculptBrush_t sculpt = m_Editor.GetFaceEdit().GetSculptBrush();
			if ( sculpt.m_bCarve )
			{
				const double flHeight = sculpt.m_bAdaptive ? sculpt.m_flPercent * flRadius : sculpt.m_flDistance;
				const xv::map::Point_t across = camera.GetRight();
				for ( std::size_t i = 0; i + 1 < sculpt.m_Profile.size(); ++i )
				{
					auto at = [ & ]( std::size_t n ) { return xv::map::Add( centre, xv::map::Add( xv::map::Scale( across, ( static_cast<double>( n ) / 4.0 - 1 ) * flRadius ), xv::map::Scale( normal, sculpt.m_Profile[ n ] * flHeight ) ) ); };
					line( at( i ), at( i + 1 ) );
				}
			}
		}
		line( centre, xv::map::Add( centre, xv::map::Scale( normal, flRadius * 0.5 ) ) );
		drawEdged( CPreferences::Instance().GetMapPreferences().m_Colors.m_ToolSelection );
	}
	if ( marker )
	{
		lines.clear();
		for ( std::size_t a = 0; a < 3; a++ )
		{
			xv::map::Point_t from = *marker;
			xv::map::Point_t to = *marker;
			from[ a ] -= 16.0;
			to[ a ] += 16.0;
			line( from, to );
		}
		drawEdged( CPreferences::Instance().GetMapPreferences().m_Colors.m_EntityMarker );
	}
	if ( pointfile && pointfile->size() >= 2 )
	{
		// The leak's line, over everything, like Hammer's.
		lines.clear();
		for ( std::size_t i = 0; i + 1 < pointfile->size(); i++ )
		{
			line( ( *pointfile )[ i ], ( *pointfile )[ i + 1 ] );
		}
		painter.setPen( QPen{ CPreferences::Instance().GetMapPreferences().m_Colors.m_Pointfile, 2.0 } );
		painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
	}
	if ( GetDocument().GetScene() )
	{
		// The cordon's box while it is on, as Hammer draws it in 3D.
		lines.clear();
		for ( const xv::map::Box_t &box : xv::map::ActiveCordonBoxes( GetDocument().GetScene()->m_Map ) )
		{
			const xv::map::Point_t &n = box.m_Mins;
			const xv::map::Point_t &x = box.m_Maxs;
			const xv::map::Point_t corners[ 8 ] = { { n[ 0 ], n[ 1 ], n[ 2 ] }, { x[ 0 ], n[ 1 ], n[ 2 ] }, { x[ 0 ], x[ 1 ], n[ 2 ] }, { n[ 0 ], x[ 1 ], n[ 2 ] }, { n[ 0 ], n[ 1 ], x[ 2 ] }, { x[ 0 ], n[ 1 ], x[ 2 ] }, { x[ 0 ], x[ 1 ], x[ 2 ] }, { n[ 0 ], x[ 1 ], x[ 2 ] } };
			for ( int i = 0; i < 4; i++ )
			{
				line( corners[ i ], corners[ ( i + 1 ) % 4 ] );
				line( corners[ 4 + i ], corners[ 4 + ( i + 1 ) % 4 ] );
				line( corners[ i ], corners[ 4 + i ] );
			}
		}
		if ( !lines.empty() )
		{
			painter.setPen( QPen{ CPreferences::Instance().GetMapPreferences().m_Colors.m_Cordon, 2.0 } );
			painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
		}
	}
	if ( m_ModalTransform )
	{
		for ( const auto &lock : m_ModalTransform->GetLockLines( m_Camera.m_flFar ) )
		{
			lines.clear();
			line( lock.m_From, lock.m_To );
			painter.setPen( QPen{ lock.m_nPart == 0 ? mapstyle::AXIS_X : lock.m_nPart == 1 ? mapstyle::AXIS_Y
																						   : mapstyle::AXIS_Z,
				1.5 } );
			painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
		}
	}
	if ( gizmo )
	{
		// Each axis in its colour, the one under the mouse (or being dragged) in the selection's
		// yellow and thicker; while a drag is on, the pivot stays where the drag began.
		const int nLit = m_nGizmoDrag >= 0 ? m_nGizmoDrag : m_nGizmoHover;
		const MapPreferences_t &look = CPreferences::Instance().GetMapPreferences();
		const double flThick = look.m_flGizmoThickness;
		const int nAlpha = std::clamp( look.m_nGizmoAlpha, 0, 255 );
		const auto gizmoLines = gizmo->GetLines( camera, look.m_flGizmoPixels );
		for ( int nAxis = 0; nAxis < ( gizmo->GetMode() == MapGizmoMode_t::MAP_GIZMO_MODE_ALL ? 13 : 3 ); nAxis++ )
		{
			lines.clear();
			for ( const MapGizmoLine_t &axisLine : gizmoLines )
			{
				if ( axisLine.m_nPart == nAxis )
				{
					line( axisLine.m_From, axisLine.m_To );
				}
			}
			const int nColorAxis = gizmo->GetPartAxis( nAxis );
			QColor color = nAxis == nLit || nColorAxis < 0 ? look.m_Colors.m_SelectionEdge3D : nColorAxis == 0 ? mapstyle::AXIS_X
				: nColorAxis == 1                                                                              ? mapstyle::AXIS_Y
																											   : mapstyle::AXIS_Z;
			color.setAlpha( nAlpha );
			painter.setPen( QPen{ QColor{ 0, 0, 0, 160 * nAlpha / 255 }, ( nAxis == nLit ? 7.0 : 6.0 ) * flThick, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin } );
			painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
			painter.setPen( QPen{ color, ( nAxis == nLit ? 4.5 : 3.5 ) * flThick, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin } );
			painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
		}
		struct GizmoFace_t
		{
			QPolygonF m_Polygon;
			QColor m_Color;
			double m_flDepth;
		};
		std::vector<GizmoFace_t> faces;
		for ( const auto &triangle : gizmo->GetTriangles( camera, look.m_flGizmoPixels ) )
		{
			const auto projected = camera.ProjectPolygon( triangle.m_Points );
			if ( projected.size() < 3 )
				continue;
			GizmoFace_t face;
			face.m_flDepth = 0.0;
			for ( const auto &point : projected )
			{
				face.m_Polygon << QPointF{ point[ 0 ], point[ 1 ] };
				face.m_flDepth += point[ 2 ];
			}
			face.m_flDepth /= projected.size();
			const int nColorAxis = gizmo->GetPartAxis( triangle.m_nPart );
			face.m_Color = triangle.m_nPart == nLit || nColorAxis < 0 ? look.m_Colors.m_SelectionEdge3D : nColorAxis == 0 ? mapstyle::AXIS_X
				: nColorAxis == 1                                                                                         ? mapstyle::AXIS_Y
																														  : mapstyle::AXIS_Z;
			const double flLight = 0.5 + 0.5 * std::abs( xv::map::Dot( triangle.m_Normal, camera.GetForward() ) );
			face.m_Color.setRgbF( face.m_Color.redF() * flLight, face.m_Color.greenF() * flLight, face.m_Color.blueF() * flLight, nAlpha / 255.0 );
			faces.push_back( std::move( face ) );
		}
		std::ranges::sort( faces, []( const GizmoFace_t &a, const GizmoFace_t &b ) { return a.m_flDepth > b.m_flDepth; } );
		painter.setPen( Qt::NoPen );
		for ( const auto &face : faces )
		{
			painter.setBrush( face.m_Color );
			painter.drawPolygon( face.m_Polygon );
		}
		painter.setBrush( Qt::NoBrush );
	}
	painter.setRenderHint( QPainter::Antialiasing, false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::Select( QPointF at, bool bToggle )
{
	CMapDocument &document = GetDocument();
	m_Hits = HitsAt( at );
	m_nHit = 0;
	if ( m_Hits.empty() )
	{
		if ( !bToggle )
		{
			document.ClearSelection();
		}
		return;
	}
	const std::vector<std::uint32_t> objects = document.Expand( m_Hits.front().m_nObject );
	if ( bToggle )
	{
		document.ToggleSelection( objects );
	}
	else
	{
		document.SetSelection( objects );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A notch moves the camera a step along its view.
//-----------------------------------------------------------------------------
void CMapView3D::wheelEvent( QWheelEvent *pEvent )
{
	if ( m_Editor.GetBrushEdit().HandleEvent( *this, pEvent ) )
	{
		pEvent->accept();
		return;
	}
	// Alt turns the wheel to the brush's size; some systems turn an Alt wheel sideways.
	if ( IsBrushShown() && pEvent->modifiers().testFlag( Qt::AltModifier ) )
	{
		const int nDelta = pEvent->angleDelta().y() != 0 ? pEvent->angleDelta().y() : pEvent->angleDelta().x();
		SetBrushRadius( std::clamp( GetBrushRadius() * std::pow( 1.1, nDelta / 120.0 ), 1.0, 8192.0 ) );
		RedrawLabels();
		pEvent->accept();
		return;
	}
	const double flNotches = pEvent->angleDelta().y() / 120.0;
	if ( m_bOrbit )
	{
		// Nearer the point or further out, still looking at it.
		const double flRadius = xv::map::Length( xv::map::Subtract( m_Camera.m_Eye, m_OrbitCenter ) );
		const double flNew = std::max( ORBIT_NEAREST, flRadius - flNotches * CPreferences::Instance().GetMapPreferences().m_flWheelStep );
		m_Camera.m_Eye = xv::map::Subtract( m_OrbitCenter, xv::map::Scale( m_Camera.GetForward(), flNew ) );
		Changed();
		pEvent->accept();
		return;
	}
	if ( IsOrthographic() )
	{
		// Zoomed about the pointer: the point under it stays there.
		const xv::map::CViewCamera before = GetSizedCamera();
		m_Camera.m_flOrthoScale = std::clamp( m_Camera.m_flOrthoScale * std::pow( ORTHO_WHEEL_ZOOM, flNotches ), ORTHO_SMALLEST, ORTHO_LARGEST );
		const auto from = before.Ray( pEvent->position().x(), pEvent->position().y() ).first;
		const auto to = GetSizedCamera().Ray( pEvent->position().x(), pEvent->position().y() ).first;
		m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Subtract( from, to ) );
		Changed();
		pEvent->accept();
		return;
	}
	m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( m_Camera.GetForward(), flNotches * CPreferences::Instance().GetMapPreferences().m_flWheelStep ) );
	Changed();
	pEvent->accept();
}

//-----------------------------------------------------------------------------
// Purpose: Movement keys are held: they start the clock, and the tick moves the camera.
//-----------------------------------------------------------------------------
void CMapView3D::keyPressEvent( QKeyEvent *pEvent )
{
	if ( m_Editor.GetBrushEdit().HandleEvent( *this, pEvent ) )
	{
		pEvent->accept();
		return;
	}
	if ( m_ModalTransform )
	{
		const int nKey = pEvent->key();
		if ( nKey == Qt::Key_Escape || nKey == Qt::Key_Return || nKey == Qt::Key_Enter )
		{
			PreviewModalTransform( pEvent->modifiers() );
			EndModalTransform( nKey != Qt::Key_Escape );
			return;
		}
		if ( nKey == Qt::Key_X || nKey == Qt::Key_Y || nKey == Qt::Key_Z )
		{
			if ( !pEvent->isAutoRepeat() )
				m_ModalTransform->LockAxis( nKey == Qt::Key_X ? 0 : nKey == Qt::Key_Y ? 1
																					  : 2,
					pEvent->modifiers().testFlag( Qt::ShiftModifier ) );
			PreviewModalTransform( pEvent->modifiers() );
			return;
		}
		if ( nKey == Qt::Key_Backspace )
		{
			m_ModalTransform->Backspace();
			PreviewModalTransform( pEvent->modifiers() );
			return;
		}
		if ( !pEvent->text().isEmpty() && m_ModalTransform->Type( pEvent->text().front() ) )
		{
			PreviewModalTransform( pEvent->modifiers() );
			return;
		}
		if ( nKey == Qt::Key_Control || nKey == Qt::Key_Alt || nKey == Qt::Key_Shift )
		{
			PreviewModalTransform( pEvent->modifiers() );
			return;
		}
	}
	if ( pEvent->modifiers() == Qt::NoModifier && ( pEvent->key() == Qt::Key_G || pEvent->key() == Qt::Key_R || pEvent->key() == Qt::Key_E ) )
	{
		if ( !pEvent->isAutoRepeat() )
			BeginModalTransform( pEvent->key() == Qt::Key_G ? MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE : pEvent->key() == Qt::Key_R ? MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE
																																	: MapGizmoMode_t::MAP_GIZMO_MODE_SCALE );
		return;
	}
	if ( pEvent->key() == Qt::Key_Escape && m_pOverlayScene )
	{
		EndOverlayDrag( false );
		return;
	}
	if ( pEvent->key() == Qt::Key_Escape && m_pPaintScene )
	{
		EndPaint( false );
		return;
	}
	if ( pEvent->key() == Qt::Key_Escape && m_pSprinkleStroke )
	{
		EndSprinkle( false );
		return;
	}
	// Hammer++'s [ and ] shrink and enlarge the brush while painting (they size the grid else).
	if ( ( IsPaintingDisplacements() || IsSprinkling() ) && pEvent->modifiers() == Qt::NoModifier && ( pEvent->key() == Qt::Key_BracketLeft || pEvent->key() == Qt::Key_BracketRight ) )
	{
		SetBrushRadius( std::clamp( GetBrushRadius() * ( pEvent->key() == Qt::Key_BracketRight ? 1.25 : 0.8 ), 1.0, 8192.0 ) );
		RedrawLabels();
		return;
	}
	if ( pEvent->key() == Qt::Key_Space && pEvent->modifiers() == Qt::NoModifier )
	{
		if ( !pEvent->isAutoRepeat() )
		{
			m_bSpaceHeld = true;
		}
		return;
	}
	// While an axis is dragged, X, Y and Z pick the axis instead (Hammer++'s axis locks).
	if ( m_nGizmoDrag >= 0 && pEvent->modifiers() == Qt::NoModifier && ( pEvent->key() == Qt::Key_X || pEvent->key() == Qt::Key_Y || pEvent->key() == Qt::Key_Z ) )
	{
		const int nKind = m_DragGizmo && m_DragGizmo->GetMode() == MapGizmoMode_t::MAP_GIZMO_MODE_ALL && m_nGizmoDrag < 9 ? m_nGizmoDrag / 3 : 0;
		m_nGizmoDrag = pEvent->key() == Qt::Key_X ? 0 : pEvent->key() == Qt::Key_Y ? 1
																				   : 2;
		m_nGizmoDrag += nKind * 3;
		PreviewGizmo( mapFromGlobal( QCursor::pos() ), QApplication::keyboardModifiers() );
		return;
	}
	QKeyEvent flightEvent( pEvent->type(), pEvent->key(), m_ModalTransform ? Qt::NoModifier : pEvent->modifiers() );
	if ( const auto nAction = MapActionFor( flightEvent, MapKeyContext_t::MAP_KEY_CONTEXT_3D ); nAction && IsHeldMapAction( *nAction ) )
	{
		if ( !pEvent->isAutoRepeat() && std::ranges::find( m_Held, *nAction ) == m_Held.end() )
		{
			if ( m_Held.empty() )
			{
				m_flSpeed = 0.0;
				m_Clock.start();
				// On the graphics card the camera moves once a frame, by the time since the last
				// one, so it moves as smoothly as the display shows it; a timer of its own beats
				// against the display's refresh and judders.
				if ( m_pGpu )
				{
					m_pGpu->update();
				}
				else
				{
					m_pTick->start();
				}
			}
			m_Held.push_back( *nAction );
		}
		return;
	}
	if ( m_ModalTransform && MapActionFor( *pEvent, MapKeyContext_t::MAP_KEY_CONTEXT_3D ) )
		EndModalTransform( false );
	CMapView::keyPressEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::keyReleaseEvent( QKeyEvent *pEvent )
{
	if ( m_ModalTransform && ( pEvent->key() == Qt::Key_Control || pEvent->key() == Qt::Key_Alt || pEvent->key() == Qt::Key_Shift ) )
	{
		PreviewModalTransform( pEvent->modifiers() );
		return;
	}
	if ( pEvent->isAutoRepeat() )
	{
		return;
	}
	if ( pEvent->key() == Qt::Key_Space )
	{
		m_bSpaceHeld = false;
		return;
	}
	QKeyEvent flightEvent( pEvent->type(), pEvent->key(), Qt::NoModifier );
	if ( const auto nAction = MapActionFor( flightEvent, MapKeyContext_t::MAP_KEY_CONTEXT_3D ); nAction && IsHeldMapAction( *nAction ) )
	{
		std::erase( m_Held, *nAction );
		if ( m_Held.empty() )
		{
			m_pTick->stop();
			m_flSpeed = 0.0;
		}
		return;
	}
	CMapView::keyReleaseEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: Leaving the view lets go of the keys, so the camera doesn't fly off on its own.
//-----------------------------------------------------------------------------
void CMapView3D::focusOutEvent( QFocusEvent *pEvent )
{
	if ( m_ModalTransform )
		EndModalTransform( false );
	m_Held.clear();
	m_pTick->stop();
	m_flSpeed = 0.0;
	m_bSpaceHeld = false;
	EndSpaceDrag();
	EndOrbit();
	if ( m_bMouselook )
	{
		SetMouselook( false );
	}
	CMapView::focusOutEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: The field of view and render distance Preferences > Maps gives.
//-----------------------------------------------------------------------------
void CMapView3D::ApplyPreferences()
{
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	m_Camera.m_flFov = preferences.m_flFov;
	m_Camera.m_flFar = preferences.m_flRenderDistance;
	Changed();
}

//-----------------------------------------------------------------------------
// Purpose: Speed builds up to the top speed (Hammer++'s by default) over its ramp time, as long
//          as keys are held.
//-----------------------------------------------------------------------------
void CMapView3D::Fly( double flSeconds )
{
	const double flTopSpeed = CPreferences::Instance().GetMapPreferences().m_flForwardSpeed;
	m_flSpeed = std::min<double>( flTopSpeed, m_flSpeed + flTopSpeed / mapstyle::MAX_SPEED_TIME * flSeconds );
	xv::map::Point_t vecMove{};
	auto add = [ & ]( const xv::map::Point_t &vecDirection, double flSign ) { vecMove = xv::map::Add( vecMove, xv::map::Scale( vecDirection, flSign ) ); };
	for ( const MapAction_t nAction : m_Held )
	{
		switch ( nAction )
		{
			case MapAction_t::MAP_ACTION_MOVE_FORWARD: add( m_Camera.GetForward(), 1.0 ); break;
			case MapAction_t::MAP_ACTION_MOVE_BACK: add( m_Camera.GetForward(), -1.0 ); break;
			case MapAction_t::MAP_ACTION_MOVE_LEFT:
			case MapAction_t::MAP_ACTION_STRAFE_LEFT: add( m_Camera.GetRight(), -1.0 ); break;
			case MapAction_t::MAP_ACTION_MOVE_RIGHT:
			case MapAction_t::MAP_ACTION_STRAFE_RIGHT: add( m_Camera.GetRight(), 1.0 ); break;
			case MapAction_t::MAP_ACTION_STRAFE_UP: add( m_Camera.GetUp(), 1.0 ); break;
			case MapAction_t::MAP_ACTION_STRAFE_DOWN: add( m_Camera.GetUp(), -1.0 ); break;
			default: break;
		}
	}
	if ( xv::map::Length( vecMove ) > 0.0 )
	{
		m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( xv::map::Normalized( vecMove ), m_flSpeed * flSeconds ) );
		Changed();
		if ( m_ModalTransform )
			PreviewModalTransform( QApplication::keyboardModifiers() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The card's frame is about to be made: held keys move the camera by the time since the
//          last one, and ask for the next frame.
//-----------------------------------------------------------------------------
void CMapView3D::StepFlight()
{
	if ( m_Held.empty() )
	{
		return;
	}
	const double flSeconds = std::min( 0.1, static_cast<double>( m_Clock.nsecsElapsed() ) / 1e9 );
	m_Clock.restart();
	Fly( flSeconds );
	m_pGpu->update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::Tick()
{
	const double flSeconds = std::min( 0.1, static_cast<double>( m_Clock.restart() ) / 1000.0 );
	if ( m_flRenderMs > SLOW_RENDER_MS && !m_pGpu )
	{
		m_bReduced = true;
	}
	Fly( flSeconds );
	m_pSettle->start();
}

//-----------------------------------------------------------------------------
// Purpose: Puts the box in front of the camera, far enough back to see it whole.
//-----------------------------------------------------------------------------
void CMapView3D::CenterOn( const xv::map::Box_t &box )
{
	if ( box.IsEmpty() )
	{
		return;
	}
	const xv::map::Point_t center = box.GetCenter();
	const double flRadius = xv::map::Length( box.GetSize() ) * 0.5;
	const double flDistance = std::max( 64.0, flRadius / std::tan( m_Camera.m_flFov * 3.14159265358979 / 360.0 ) ) + flRadius * 0.25;
	m_Camera.m_Eye = xv::map::Subtract( center, xv::map::Scale( m_Camera.GetForward(), flDistance ) );
	Changed();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapView3D::Run( MapAction_t nAction )
{
	switch ( nAction )
	{
		case MapAction_t::MAP_ACTION_MOUSELOOK:
			SetMouselook( !m_bMouselook );
			return true;
		case MapAction_t::MAP_ACTION_RENDER_CLOSER:
			m_Camera.m_flFar = std::max( 256.0, m_Camera.m_flFar / CPreferences::Instance().GetMapPreferences().m_flFarStep );
			Changed();
			return true;
		case MapAction_t::MAP_ACTION_RENDER_FURTHER:
			m_Camera.m_flFar = std::min<double>( CPreferences::Instance().GetMapPreferences().m_flRenderDistance * 2.0, m_Camera.m_flFar * CPreferences::Instance().GetMapPreferences().m_flFarStep );
			Changed();
			return true;
		case MapAction_t::MAP_ACTION_STATISTICS:
			m_bStatistics = !m_bStatistics;
			update();
			return true;
		case MapAction_t::MAP_ACTION_EDIT_MODE:
			// Hammer++'s X in 3D: the gizmo shown or not.
			m_Editor.SetGizmoShown( !m_Editor.IsGizmoShown() );
			return true;
		case MapAction_t::MAP_ACTION_ESCAPE:
			if ( m_nGizmoDrag >= 0 )
			{
				EndGizmo( false, m_GizmoFrom, Qt::NoModifier );
				return true;
			}
			if ( m_nBoxFace >= 0 || m_nBoxPlane >= 0 )
			{
				EndBoxDrag( false );
				return true;
			}
			if ( m_BlockPlane )
			{
				EndBlockDrag( false );
				return true;
			}
			if ( m_Editor.IsWorldGridEdited() )
			{
				m_Editor.SetWorldGridEdited( false );
				return true;
			}
			return false;
		case MapAction_t::MAP_ACTION_NEXT_HIT:
		case MapAction_t::MAP_ACTION_PREVIOUS_HIT:
		{
			// The camera tool steps through the map's cameras; the others through the hits.
			const xv::map::CScene *pScene = GetDocument().GetScene();
			if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_CAMERA && pScene && !pScene->m_Map.m_Cameras.empty() )
			{
				m_Editor.StepCamera( nAction == MapAction_t::MAP_ACTION_PREVIOUS_HIT ? 1 : -1 );
				return true;
			}
			if ( m_Hits.size() < 2 )
			{
				return true;
			}
			m_nHit = ( m_nHit + ( nAction == MapAction_t::MAP_ACTION_NEXT_HIT ? 1 : m_Hits.size() - 1 ) ) % m_Hits.size();
			GetDocument().SetSelection( GetDocument().Expand( m_Hits[ m_nHit ].m_nObject ) );
			return true;
		}
		default:
			return false;
	}
}
