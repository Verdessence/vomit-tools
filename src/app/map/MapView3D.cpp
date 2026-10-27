//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapView3D.h. The camera controls are Hammer's: the camera tool's drags, Z to
//          toggle mouselook (Space to hold it), WASD and the arrows to fly, the wheel to step.
//
//===========================================================================//
#include "MapView3D.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QApplication>
#include <QGuiApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include "Preferences.h"
#include "DebugLog.h"
#include "map/MapPanes.h"
#include "map/MapView2D.h"
#include "map/MapConstants.h"
#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "Graphics.h"
#include "GpuView.h"
#ifdef VOMIT_TOOLS_GPU
#include <rhi/qrhi.h>
#endif
#include "map/MapRenderer.h"

namespace
{

constexpr int TICK_MS = 16;
constexpr int SETTLE_MS = 150;
constexpr double SLOW_RENDER_MS = 40.0; // slower than this, moving pictures drop to half size

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
	m_Camera.m_flNear = mapstyle::NEAR_PLANE;
	// Not ApplyPreferences: that tells the editor, which is still making its panes.
	m_Camera.m_flFov = CPreferences::Instance().GetMapPreferences().m_flFov;
	m_Camera.m_flFar = CPreferences::Instance().GetMapPreferences().m_flRenderDistance;
	m_pTick = new QTimer( this );
	m_pTick->setInterval( TICK_MS );
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
		m_pGpu = new CGpuView( std::make_unique<CMapRenderer>( [ this ]( QSize size ) { return MakeFrame( size ); } ), this );
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
		default: return xv::map::RasterMode_t::RASTER_MODE_TEXTURED;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The map's active camera, or Hammer's start without one: the origin, facing north.
//-----------------------------------------------------------------------------
void CMapView3D::PlaceCamera()
{
	const xv::map::CScene *pScene = GetDocument().GetScene();
	m_pCameraFor = pScene;
	m_Camera.m_Eye = {};
	m_Camera.m_flYaw = 90.0;
	m_Camera.m_flPitch = 0.0;
	if ( pScene && !pScene->m_Map.m_Cameras.empty() )
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
	if ( GetDocument().GetScene() != m_pCameraFor && GetDocument().GetScene() )
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
	options.m_Shade.m_vecInstance = vector( colors.m_Instance );
	options.m_Shade.m_flInstanceTint = static_cast<float>( preferences.m_flInstanceTint );
	options.m_Background = bytes( colors.m_Background3D );
	options.m_SelectionEdge = bytes( colors.m_SelectionEdge3D );
	options.m_Shade.m_vecInvalid = vector( colors.m_Invalid );
	options.m_Shade.m_flInvalidTint = static_cast<float>( preferences.m_flInvalidTint );
	options.m_InvalidEdge = bytes( colors.m_Invalid );
	MapFrame_t frame = m_Editor.GetRenderData().Snapshot( camera, options, m_Editor.GetSettings() );
	frame.m_bModelEdges = preferences.m_bWireframeModels;
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
		const double flMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
		QImage picture = ToQImage( image );
		QMetaObject::invokeMethod(
			this, [ this, picture = std::move( picture ), flMs ]() mutable {
				m_Image = std::move( picture );
				m_flRenderMs = flMs;
				update();
			},
			Qt::QueuedConnection );
	}
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
	return ToQImage( image );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::paintEvent( QPaintEvent * )
{
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
// Purpose: What goes over the picture: the statistics (O) and the focus frame.
//-----------------------------------------------------------------------------
void CMapView3D::PaintLabels( QPainter &painter )
{
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
		for ( CMapView *pView : m_Editor.GetViews() )
		{
			if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
			{
				lines << tr( "%1: %2 ms a repaint, %3 repaints" ).arg( CMapPane::TypeName( p2D->GetType() ) ).arg( p2D->GetLastPaintTime(), 0, 'f', 1 ).arg( p2D->GetPaintCount() );
			}
		}
		const QString szText = lines.join( QLatin1Char( '\n' ) );
		const QRect text = painter.fontMetrics().boundingRect( rect().adjusted( 0, 4, -8, 0 ), Qt::AlignRight | Qt::AlignTop, szText );
		painter.fillRect( text.adjusted( -6, -3, 6, 3 ), mapstyle::OVERLAY_BACKING );
		painter.setPen( Qt::white );
		painter.drawText( rect().adjusted( 0, 4, -8, 0 ), Qt::AlignRight | Qt::AlignTop, szText );
		if ( !m_LogClock.isValid() || m_LogClock.elapsed() > 1000 )
		{
			m_LogClock.start();
			DebugLog() << "map 3D view:" << lines.join( QStringLiteral( "; " ) );
		}
	}
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
	setCursor( bOn ? Qt::BlankCursor : Qt::ArrowCursor );
	if ( bOn )
	{
		// The cursor goes back here afterwards. Meanwhile the pointer is pinned (Wayland), or
		// warped back to the middle at each move (mouseMoveEvent), so it never reaches an edge.
		m_LookFrom = mapFromGlobal( QCursor::pos() );
		m_LastMouse = m_LookFrom;
		grabMouse();
		m_PointerLock.Lock( this, [ this ]( QPointF delta ) { Look( delta ); } );
	}
	else
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
	}
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
	setFocus( Qt::MouseFocusReason );
	m_Dragging = pEvent->buttons();
	m_LastMouse = pEvent->position().toPoint();
	m_PressAt = m_LastMouse;
	m_bMoved = false;
}

//-----------------------------------------------------------------------------
// Purpose: The camera tool's drags: left looks, right pans, both move forward and back.
//          Space or mouselook turn the camera without the tool.
//-----------------------------------------------------------------------------
void CMapView3D::mouseMoveEvent( QMouseEvent *pEvent )
{
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
	auto look = [ & ] { Look( delta ); };
	if ( m_bMouselook || ( m_bSpaceHeld && ( pEvent->buttons() & Qt::LeftButton ) ) )
	{
		look();
		// Back to the middle, so the mouse never runs into the screen's edge (a compositor
		// that won't move the pointer leaves it where it is, and the movement is all there is).
		if ( m_bMouselook )
		{
			const QPoint middle{ width() / 2, height() / 2 };
			QCursor::setPos( mapToGlobal( middle ) );
			m_LastMouse = middle;
		}
		return;
	}
	if ( !bCameraTool || pEvent->buttons() == Qt::NoButton )
	{
		return;
	}
	const bool bLeft = ( pEvent->buttons() & Qt::LeftButton ) != 0;
	const bool bRight = ( pEvent->buttons() & Qt::RightButton ) != 0;
	const double flPan = CPreferences::Instance().GetMapPreferences().m_flPanScale;
	if ( bLeft && bRight )
	{
		m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( m_Camera.GetForward(), -delta.y() * flPan ) );
		Changed();
	}
	else if ( bLeft )
	{
		look();
	}
	else if ( bRight )
	{
		m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Add( xv::map::Scale( m_Camera.GetRight(), delta.x() * flPan ), xv::map::Scale( m_Camera.GetUp(), -delta.y() * flPan ) ) );
		Changed();
	}
}

//-----------------------------------------------------------------------------
// Purpose: A click (not a drag) with the selection tool picks what is under it.
//-----------------------------------------------------------------------------
void CMapView3D::mouseReleaseEvent( QMouseEvent *pEvent )
{
	const bool bWasLeft = ( m_Dragging & Qt::LeftButton ) != 0;
	m_Dragging = pEvent->buttons();
	if ( bWasLeft && !m_bMoved && !m_bSpaceHeld && !m_bMouselook && m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT && pEvent->button() == Qt::LeftButton )
	{
		Select( pEvent->position(), pEvent->modifiers().testFlag( Qt::ControlModifier ) );
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
	return xv::map::TraceScene( frame.MakeScene(), m_Editor.GetSettings().m_bToolTextures, from, dir, m_Camera.m_flFar );
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
	const double flNotches = pEvent->angleDelta().y() / 120.0;
	m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( m_Camera.GetForward(), flNotches * CPreferences::Instance().GetMapPreferences().m_flWheelStep ) );
	Changed();
	pEvent->accept();
}

//-----------------------------------------------------------------------------
// Purpose: Movement keys are held: they start the clock, and the tick moves the camera.
//-----------------------------------------------------------------------------
void CMapView3D::keyPressEvent( QKeyEvent *pEvent )
{
	if ( pEvent->key() == Qt::Key_Space && pEvent->modifiers() == Qt::NoModifier )
	{
		if ( !pEvent->isAutoRepeat() )
		{
			m_bSpaceHeld = true;
		}
		return;
	}
	if ( const auto nAction = MapActionFor( *pEvent, MapKeyContext_t::MAP_KEY_CONTEXT_3D ); nAction && IsHeldMapAction( *nAction ) )
	{
		if ( !pEvent->isAutoRepeat() && std::ranges::find( m_Held, *nAction ) == m_Held.end() )
		{
			if ( m_Held.empty() )
			{
				m_flSpeed = 0.0;
				m_Clock.start();
				m_pTick->start();
			}
			m_Held.push_back( *nAction );
		}
		return;
	}
	CMapView::keyPressEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::keyReleaseEvent( QKeyEvent *pEvent )
{
	if ( pEvent->isAutoRepeat() )
	{
		return;
	}
	if ( pEvent->key() == Qt::Key_Space )
	{
		m_bSpaceHeld = false;
		return;
	}
	if ( const auto nAction = MapActionFor( *pEvent, MapKeyContext_t::MAP_KEY_CONTEXT_3D ); nAction && IsHeldMapAction( *nAction ) )
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
	m_Held.clear();
	m_pTick->stop();
	m_flSpeed = 0.0;
	m_bSpaceHeld = false;
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
	}
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
		case MapAction_t::MAP_ACTION_NEXT_HIT:
		case MapAction_t::MAP_ACTION_PREVIOUS_HIT:
		{
			// The camera tool steps through the map's cameras; the others through the hits.
			const xv::map::CScene *pScene = GetDocument().GetScene();
			if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_CAMERA && pScene && !pScene->m_Map.m_Cameras.empty() )
			{
				const std::size_t nCount = pScene->m_Map.m_Cameras.size();
				m_nCamera = ( m_nCamera + ( nAction == MapAction_t::MAP_ACTION_NEXT_HIT ? 1 : nCount - 1 ) ) % nCount;
				const xv::map::Camera_t &camera = pScene->m_Map.m_Cameras[ m_nCamera ];
				m_Camera.LookAt( camera.m_Position, camera.m_Look );
				Changed();
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
