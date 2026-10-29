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
#include <numbers>

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
#include "map/Cordon.h"
#include "map/DispEdit.h"
#include "map/MapFaceEdit.h"
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
	setCursor( Qt::ArrowCursor ); // not the splitter's bar's, on the way in
	m_Camera.m_flNear = mapstyle::NEAR_PLANE;
	// Not ApplyPreferences: that tells the editor, which is still making its panes.
	m_Camera.m_flFov = CPreferences::Instance().GetMapPreferences().m_flFov;
	m_Camera.m_flFar = CPreferences::Instance().GetMapPreferences().m_flRenderDistance;
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
		m_pGpu = new CGpuView( std::make_unique<CMapRenderer>( [ this ]( QSize size ) {
			StepFlight();
			return MakeFrame( size );
		} ),
			this );
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
		if ( m_pPaintScene )
		{
			lines << GetPaintCosts();
		}
		for ( CMapView *pView : m_Editor.GetViews() )
		{
			if ( auto *p2D = qobject_cast<CMapView2D *>( pView ) )
			{
				lines << tr( "%1: %2 ms a repaint (%3 patch, %4 layer), %5 repaints" ).arg( CMapPane::TypeName( p2D->GetType() ) ).arg( p2D->GetLastPaintTime(), 0, 'f', 1 ).arg( p2D->GetLastPatchTime(), 0, 'f', 2 ).arg( p2D->GetLastLayerTime(), 0, 'f', 2 ).arg( p2D->GetPaintCount() );
			}
		}
		lines << GetDocument().GetEditListenerCosts();
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
	PaintTools( painter );
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
		setCursor( Qt::BlankCursor );
	}
	else
	{
		UpdateCursor();
	}
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
	// On one of the gizmo's axes, the selection tool drags the selection along it.
	if ( pEvent->button() == Qt::LeftButton && !m_bSpaceHeld && !m_bMouselook && m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT )
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
				m_pGizmoScene = GetDocument().GetSharedScene();
				m_GizmoSelection = GetDocument().GetSelection();
			}
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
			m_pPaintScene = GetDocument().GetSharedScene();
			m_Painted.clear();
			m_PaintChanged.clear();
			for ( const xv::map::FaceRef_t &face : faces )
			{
				m_Painted.emplace_back( face, *m_pPaintScene->m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ].m_pDisp );
			}
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
	// Hammer++'s right click goes round the gizmo's modes.
	if ( pEvent->button() == Qt::RightButton && m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT && GetGizmo() )
	{
		const MapGizmoMode_t nMode = m_Editor.GetGizmoMode();
		m_Editor.SetGizmoMode( nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE ? MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE : nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ? MapGizmoMode_t::MAP_GIZMO_MODE_SCALE
																																										  : MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE );
	}
}

//-----------------------------------------------------------------------------
// Purpose: At the selection's middle (or the pivot, which turns and scales are about), on the
//          world's axes or (J) a lone entity's own.
//-----------------------------------------------------------------------------
std::optional<CMapGizmo> CMapView3D::GetGizmo() const
{
	const CMapDocument &document = GetDocument();
	const xv::map::CScene *pScene = document.GetScene();
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
	gizmo.SetMode( m_Editor.GetGizmoMode() );
	return gizmo;
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
// Purpose: The drag's transform shown as it goes: along the grid (Alt: free), in rotation
//          steps (Preferences; Alt: free).
//-----------------------------------------------------------------------------
void CMapView3D::PreviewGizmo( QPointF at, Qt::KeyboardModifiers modifiers )
{
	if ( !m_DragGizmo || !m_pGizmoScene )
	{
		return;
	}
	const bool bFree = modifiers.testFlag( Qt::AltModifier );
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	const double flGrid = bFree || !settings.m_bSnapToGrid ? 0.0 : static_cast<double>( settings.m_nGridSize );
	const double flStep = bFree ? 0.0 : CPreferences::Instance().GetMapPreferences().m_flRotationSnap;
	const xv::map::Transform_t transform = m_DragGizmo->Drag( GetSizedCamera(), m_nGizmoDrag, m_GizmoFrom, at, flGrid, flStep );
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
	const MapGizmoMode_t nMode = m_DragGizmo ? m_DragGizmo->GetMode() : MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE;
	if ( m_pGizmoScene )
	{
		GetDocument().EndPreview();
	}
	m_nGizmoDrag = -1;
	m_DragGizmo.reset();
	m_pGizmoScene.reset();
	m_GizmoSelection.clear();
	if ( bCommit )
	{
		m_Editor.TransformSelection( transform, nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE ? tr( "Move" ) : nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ? tr( "Rotate" )
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
	if ( m_RadiusFrom && ( pEvent->buttons() & ( Qt::LeftButton | Qt::RightButton ) ) )
	{
		// A hundred pixels right makes the brush about 2.7 times as big.
		m_Editor.GetFaceEdit().SetBrushRadius( std::clamp( m_flRadiusFrom * std::exp( ( at.x() - m_RadiusFrom->x() ) / 100.0 ), 1.0, 8192.0 ) );
		RedrawLabels();
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
	if ( pEvent->buttons() == Qt::NoButton && !m_bMouselook && IsPaintingDisplacements() )
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
	if ( pEvent->buttons() == Qt::NoButton && !m_bMouselook && m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT )
	{
		// The axis under the mouse lights up.
		const std::optional<CMapGizmo> gizmo = GetGizmo();
		const MapPreferences_t &look = CPreferences::Instance().GetMapPreferences();
		const int nHover = gizmo ? gizmo->HitAt( GetSizedCamera(), look.m_flGizmoPixels, pEvent->position(), mapstyle::GIZMO_SLACK * std::max( 1.0, look.m_flGizmoThickness ) ) : -1;
		if ( nHover != m_nGizmoHover )
		{
			m_nGizmoHover = nHover;
			RedrawLabels();
		}
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
	if ( m_nGizmoDrag >= 0 && pEvent->button() == Qt::LeftButton )
	{
		EndGizmo( true, pEvent->position(), pEvent->modifiers() );
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
	if ( !bWasLeft || m_bMoved || m_bSpaceHeld || m_bMouselook || pEvent->button() != Qt::LeftButton )
	{
		return;
	}
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT )
	{
		Select( pEvent->position(), pEvent->modifiers().testFlag( Qt::ControlModifier ) );
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
	return xv::map::TraceScene( frame.MakeScene(), m_Editor.GetSettings().m_bToolTextures, from, dir, m_Camera.m_flFar );
}

//-----------------------------------------------------------------------------
// Purpose: Only the map's own meshes (as read, and the edit mesh) map triangles back to sides.
//-----------------------------------------------------------------------------
std::optional<xv::map::FaceRef_t> CMapView3D::FaceAt( QPointF at )
{
	const MapFrame_t frame = MakeFrame( size() );
	if ( !frame.m_pScene )
	{
		return std::nullopt;
	}
	const xv::map::RasterScene_t scene = frame.MakeScene();
	const auto [ from, dir ] = frame.m_Camera.Ray( at.x(), at.y() );
	const std::vector<xv::map::TraceHit_t> hits = xv::map::TraceScene( scene, m_Editor.GetSettings().m_bToolTextures, from, dir, m_Camera.m_flFar );
	if ( hits.empty() )
	{
		return std::nullopt;
	}
	const xv::map::TraceHit_t &hit = hits.front();
	const xv::map::CMapMesh &mesh = *scene.m_Meshes[ hit.m_nMesh ];
	if ( ( &mesh != &frame.m_pScene->GetMesh() && &mesh != &frame.m_pScene->GetEditMesh() && &mesh != frame.m_pScene->m_pPreviewBaseMesh.get() ) || hit.m_nTriangle >= mesh.m_TriangleFaces.size() )
	{
		return std::nullopt;
	}
	const xv::map::MeshFace_t &face = mesh.m_Faces[ mesh.m_TriangleFaces[ hit.m_nTriangle ] ];
	return xv::map::FaceRef_t{ face.m_nSolid, face.m_nSide };
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
	const std::vector<xv::map::TraceHit_t> hits = xv::map::TraceScene( scene, m_Editor.GetSettings().m_bToolTextures, from, dir, m_Camera.m_flFar );
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
	return tr( "paint: %1 grid, %2 edit, %3 scene, %4 notify (ms)" ).arg( m_flGridPaintMs, 0, 'f', 2 ).arg( m_flDispEditMs, 0, 'f', 2 ).arg( GetDocument().GetPreviewTime(), 0, 'f', 2 ).arg( GetDocument().GetPreviewNotifyTime(), 0, 'f', 2 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::UpdateCursor()
{
	if ( m_bMouselook )
	{
		return;
	}
	const Qt::CursorShape nShape = IsPaintingDisplacements() ? Qt::CrossCursor : Qt::ArrowCursor;
	if ( cursor().shape() != nShape )
	{
		setCursor( nShape );
	}
	if ( !IsPaintingDisplacements() && m_BrushAt && !m_pPaintScene )
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
	return m_Editor.GetTool() == MapTool_t::MAP_TOOL_TEXTURE && m_Editor.GetFaceEdit().GetDispPaintMode() != DispPaintMode_t::DISP_PAINT_NONE;
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
	if ( !m_pPaintPreview->isActive() )
	{
		m_pPaintPreview->start( 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Coalesce samples in one event loop pass without dropping their grid changes.
//-----------------------------------------------------------------------------
void CMapView3D::PreviewPaint()
{
	if ( !m_pPaintScene )
	{
		return;
	}
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
	m_pPaintPreview->stop();
	GetDocument().EndPreview();
	const std::shared_ptr<const xv::map::CScene> pScene = std::move( m_pPaintScene );
	m_pPaintScene.reset();
	std::erase_if( m_Painted, [ this ]( const auto &entry ) { return std::ranges::find( m_PaintChanged, entry.first ) == m_PaintChanged.end(); } );
	if ( bCommit )
	{
		m_Editor.CommitDisplacements( *pScene, m_Painted, m_bPaintSculpt ? tr( "Sculpt" ) : m_bPaintAlpha ? tr( "Paint Alpha" )
																										  : tr( "Paint Geometry" ) );
	}
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
// Purpose: Edges in front of the near plane are cut where they cross it, so a box reaching
//          behind the camera is still drawn where it is in view. A dark edge round each line
//          keeps it seen against faces of its own colour.
//-----------------------------------------------------------------------------
void CMapView3D::PaintTools( QPainter &painter )
{
	UpdateCollapsibleVertices();
	const std::optional<xv::map::Box_t> &box = m_Editor.GetNewBox();
	const std::optional<xv::map::Point_t> &marker = m_Editor.GetEntityMarker();
	const std::optional<CMapGizmo> gizmo = m_DragGizmo ? m_DragGizmo : GetGizmo();
	const std::vector<xv::map::FaceRef_t> &faces = m_Editor.GetSelectedFaces();
	const std::optional<std::vector<xv::map::Point_t>> &pointfile = GetDocument().GetPointfile();
	const bool bBrush = m_BrushAt && IsPaintingDisplacements();
	const bool bWalkable = m_Editor.GetSettings().m_bWalkable && GetDocument().GetScene();
	if ( !box && !marker && !gizmo && faces.empty() && !pointfile && !bBrush && !bWalkable )
	{
		return;
	}
	const xv::map::CViewCamera camera = GetSizedCamera();
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
				QPolygonF polygon;
				for ( std::size_t k = 0; k < 3; k++ )
				{
					const auto at = camera.Project( mesh->m_Points[ mesh->m_Indices[ t * 3 + k ] ] );
					if ( !at )
					{
						polygon.clear();
						break;
					}
					polygon << QPointF{ ( *at )[ 0 ], ( *at )[ 1 ] };
				}
				if ( polygon.size() == 3 )
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
		const bool bShade = !m_Editor.GetFaceEdit().IsSelectMasked();
		const bool bGrid = !m_Editor.GetFaceEdit().IsGridMasked();
		painter.setRenderHint( QPainter::Antialiasing, true );
		auto project = [ & ]( const std::vector<xv::map::Point_t> &points, QPolygonF &polygon ) {
			polygon.clear();
			for ( const xv::map::Point_t &p : points )
			{
				const auto at = camera.Project( p );
				if ( !at )
				{
					polygon.clear();
					return false;
				}
				polygon << QPointF{ ( *at )[ 0 ], ( *at )[ 1 ] };
			}
			return polygon.size() >= 3;
		};
		QPolygonF polygon;
		for ( const xv::map::FaceRef_t &face : faces )
		{
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
						if ( project( { mesh->m_Points[ mesh->m_Indices[ t ] ], mesh->m_Points[ mesh->m_Indices[ t + 1 ] ], mesh->m_Points[ mesh->m_Indices[ t + 2 ] ] }, polygon ) )
						{
							painter.drawPolygon( polygon );
						}
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
			painter.setBrush( fill );
			if ( project( pFace->m_Points, polygon ) )
			{
				painter.drawPolygon( polygon );
			}
		}
		painter.setBrush( Qt::NoBrush );
		painter.setRenderHint( QPainter::Antialiasing, false );
	}
	const xv::map::Point_t forward = camera.GetForward();
	std::vector<QLineF> lines;
	auto line = [ & ]( xv::map::Point_t a, xv::map::Point_t b ) {
		const double flA = xv::map::Dot( xv::map::Subtract( a, camera.m_Eye ), forward ) - camera.m_flNear * 1.01;
		const double flB = xv::map::Dot( xv::map::Subtract( b, camera.m_Eye ), forward ) - camera.m_flNear * 1.01;
		if ( flA < 0.0 && flB < 0.0 )
		{
			return;
		}
		if ( flA < 0.0 || flB < 0.0 )
		{
			const xv::map::Point_t cut = xv::map::Add( a, xv::map::Scale( xv::map::Subtract( b, a ), flA / ( flA - flB ) ) );
			( flA < 0.0 ? a : b ) = cut;
		}
		const auto pa = camera.Project( a );
		const auto pb = camera.Project( b );
		if ( pa && pb )
		{
			lines.emplace_back( QPointF{ ( *pa )[ 0 ], ( *pa )[ 1 ] }, QPointF{ ( *pb )[ 0 ], ( *pb )[ 1 ] } );
		}
	};
	if ( box )
	{
		auto corner = [ & ]( int n ) { return xv::map::Point_t{ ( n & 1 ) ? box->m_Maxs[ 0 ] : box->m_Mins[ 0 ], ( n & 2 ) ? box->m_Maxs[ 1 ] : box->m_Mins[ 1 ], ( n & 4 ) ? box->m_Maxs[ 2 ] : box->m_Mins[ 2 ] }; };
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
	if ( bBrush )
	{
		// The brush's ring on the surface, the radius the sheet has.
		lines.clear();
		const xv::map::Point_t &centre = m_BrushAt->first;
		const xv::map::Point_t normal = xv::map::Length( m_BrushAt->second ) > 1e-9 ? xv::map::Normalized( m_BrushAt->second ) : xv::map::Point_t{ 0, 0, 1 };
		const xv::map::Point_t side = std::abs( normal[ 2 ] ) < 0.9 ? xv::map::Point_t{ 0, 0, 1 } : xv::map::Point_t{ 1, 0, 0 };
		const xv::map::Point_t u = xv::map::Normalized( xv::map::Cross( normal, side ) );
		const xv::map::Point_t v = xv::map::Cross( normal, u );
		const double flRadius = m_Editor.GetFaceEdit().GetBrushRadius();
		constexpr int SEGMENTS = 36;
		for ( int i = 0; i < SEGMENTS; i++ )
		{
			const double a0 = i * 2.0 * std::numbers::pi / SEGMENTS;
			const double a1 = ( i + 1 ) * 2.0 * std::numbers::pi / SEGMENTS;
			line( xv::map::Add( centre, xv::map::Add( xv::map::Scale( u, std::cos( a0 ) * flRadius ), xv::map::Scale( v, std::sin( a0 ) * flRadius ) ) ), xv::map::Add( centre, xv::map::Add( xv::map::Scale( u, std::cos( a1 ) * flRadius ), xv::map::Scale( v, std::sin( a1 ) * flRadius ) ) ) );
		}
		if ( m_Editor.GetFaceEdit().GetDispPaintMode() == DispPaintMode_t::DISP_PAINT_SCULPT )
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
	if ( gizmo )
	{
		// Each axis in its colour, the one under the mouse (or being dragged) in the selection's
		// yellow and thicker; while a drag is on, the pivot stays where the drag began.
		const int nLit = m_nGizmoDrag >= 0 ? m_nGizmoDrag : m_nGizmoHover;
		const MapPreferences_t &look = CPreferences::Instance().GetMapPreferences();
		const double flThick = look.m_flGizmoThickness;
		const int nAlpha = std::clamp( look.m_nGizmoAlpha, 0, 255 );
		for ( int nAxis = 0; nAxis < 3; nAxis++ )
		{
			lines.clear();
			for ( const MapGizmoLine_t &axisLine : gizmo->GetLines( camera, look.m_flGizmoPixels ) )
			{
				if ( axisLine.m_nAxis == nAxis )
				{
					line( axisLine.m_From, axisLine.m_To );
				}
			}
			QColor color = nAxis == nLit ? look.m_Colors.m_SelectionEdge3D : nAxis == 0 ? mapstyle::AXIS_X
				: nAxis == 1                                                            ? mapstyle::AXIS_Y
																						: mapstyle::AXIS_Z;
			color.setAlpha( nAlpha );
			painter.setPen( QPen{ QColor{ 0, 0, 0, 160 * nAlpha / 255 }, ( nAxis == nLit ? 7.0 : 6.0 ) * flThick, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin } );
			painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
			painter.setPen( QPen{ color, ( nAxis == nLit ? 4.5 : 3.5 ) * flThick, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin } );
			painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
		}
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
	if ( pEvent->key() == Qt::Key_Escape && m_pPaintScene )
	{
		EndPaint( false );
		return;
	}
	// Hammer++'s [ and ] shrink and enlarge the brush while painting (they size the grid else).
	if ( IsPaintingDisplacements() && pEvent->modifiers() == Qt::NoModifier && ( pEvent->key() == Qt::Key_BracketLeft || pEvent->key() == Qt::Key_BracketRight ) )
	{
		CMapFaceEdit &sheet = m_Editor.GetFaceEdit();
		sheet.SetBrushRadius( std::clamp( sheet.GetBrushRadius() * ( pEvent->key() == Qt::Key_BracketRight ? 1.25 : 0.8 ), 1.0, 8192.0 ) );
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
		m_nGizmoDrag = pEvent->key() == Qt::Key_X ? 0 : pEvent->key() == Qt::Key_Y ? 1
																				   : 2;
		PreviewGizmo( mapFromGlobal( QCursor::pos() ), QApplication::keyboardModifiers() );
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
			return false;
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
