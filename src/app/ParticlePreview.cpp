//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ParticlePreview.h.
//
//===========================================================================//
#include "ParticlePreview.h"

#include "MenuPicker.h"

#ifdef VOMIT_TOOLS_GPU_PREVIEW
#include "ParticleGpuView.h"
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QGuiApplication>
#include <QWidgetAction>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QMenu>
#include <QFileInfo>
#include <QSettings>
#include <QSlider>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace particles = xv::particles;
using xv::CVec3;

namespace
{

constexpr int MAX_RENDER_WIDTH = 800; // pixels drawn across; larger views are scaled up
constexpr float CATCH_UP = 4.0f;      // seconds replayed after an edit, so the effect stays in step
constexpr float LOOP_PAUSE = 0.6f;    // seconds of nothing before a finished system starts again
constexpr float HANDLE_RADIUS = 9.0f; // how close (pixels) a click must be to grab a control point
constexpr int TIMELINE_START = 5000;  // milliseconds the timeline spans at first; it grows to fit
constexpr int TIMELINE_MOST = 120000;
constexpr auto GPU_KEY = "particlePreviewGpu";

// Where each control point starts: 0 at the origin, 1 above it (where effects usually aim), the
// rest spread around.
constexpr std::array<CVec3, particles::CSimulation::CONTROL_POINTS> DEFAULT_CONTROL_POINTS{ {
	{ 0, 0, 0 },
	{ 0, 0, 64 },
	{ 64, 0, 0 },
	{ 0, 64, 0 },
	{ -64, 0, 0 },
	{ 0, -64, 0 },
	{ 0, 0, 128 },
	{ 64, 64, 0 },
} };

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
QColor ControlPointColor( int nIndex )
{
	return nIndex == 0 ? QColor( 255, 210, 80 ) : QColor::fromHsv( ( 190 + nIndex * 47 ) % 360, 150, 255 );
}

//-----------------------------------------------------------------------------
// Purpose: Visits the instance tree of a simulation.
//-----------------------------------------------------------------------------
void ForEachInstance( const particles::CSimulation::Instance_t &instance, const std::function<void( const particles::CSimulation::Instance_t & )> &pfnVisit )
{
	pfnVisit( instance );
	for ( const auto &child : instance.m_Children )
	{
		ForEachInstance( child, pfnVisit );
	}
}

} // namespace

// The drawing area; the preview owns the state, this only paints and takes the mouse.
class CParticleCanvas : public QWidget
{
public:
	explicit CParticleCanvas( CParticlePreview *pPreview )
		: m_pPreview( pPreview )
	{
		setObjectName( QStringLiteral( "particleCanvas" ) );
		setMinimumSize( 200, 150 );
		setMouseTracking( true );
		setCursor( Qt::OpenHandCursor );
		setToolTip( tr( "Drag to turn around the effect, right-drag to move, scroll to zoom, double-click to frame it.\n"
						"Drag a control point (CP) to move it; hold Shift to move it up and down. It snaps to the grid while the grid shows.\n"
						"CP0 is where the effect is: moving it changes only the view. Moving another CP writes it into the file "
						"(Set Control Point Positions); hold Ctrl to move one the file doesn't place in the view only.\n"
						"A model moves by dragging it (its control point moves with it, in the view only) and turns by the ring at its feet" ) );
	}

	// What the cursor shows over a canvas position.
	[[nodiscard]] Qt::CursorShape HoverCursor( QPointF at ) const
	{
		if ( HandleAt( at ) >= 0 )
		{
			return Qt::PointingHandCursor;
		}
		if ( m_pPreview->IsOnModelRing( at ) )
		{
			return Qt::SizeHorCursor;
		}
		return m_pPreview->IsOnModel( at ) ? Qt::SizeAllCursor : Qt::OpenHandCursor;
	}

	// The control point whose handle is under a canvas position, or -1.
	[[nodiscard]] int HandleAt( QPointF at ) const
	{
		if ( !m_pPreview->m_pSimulation )
		{
			return -1;
		}
		const particles::CCamera camera = m_pPreview->GetCanvasCamera();
		int nBest = -1;
		float flBestDistance = HANDLE_RADIUS;
		for ( int nIndex : m_pPreview->GetUsedControlPoints() )
		{
			if ( const auto p = camera.Project( m_pPreview->m_pSimulation->ControlPoint( nIndex ) ) )
			{
				const float flDistance = std::hypot( ( *p )[ 0 ] - static_cast<float>( at.x() ), ( *p )[ 1 ] - static_cast<float>( at.y() ) );
				if ( flDistance <= flBestDistance )
				{
					nBest = nIndex;
					flBestDistance = flDistance;
				}
			}
		}
		return nBest;
	}

protected:
	void paintEvent( QPaintEvent * ) override
	{
		if ( m_pPreview->m_pGpu )
		{
			return; // the GPU view and the overlay above it cover the canvas
		}
		QPainter painter( this );
		m_pPreview->PaintCanvas( painter, rect() );
	}

	void resizeEvent( QResizeEvent * ) override
	{
		for ( QWidget *pChild : { m_pPreview->m_pGpu, m_pPreview->m_pOverlay } )
		{
			if ( pChild )
			{
				pChild->setGeometry( rect() );
			}
		}
		m_pPreview->m_bDirty = true;
	}

	void mousePressEvent( QMouseEvent *pEvent ) override
	{
		m_Last = pEvent->position();
		// Control points first, then the model's ring (turns it), then the model (moves it).
		int nHandle = pEvent->button() == Qt::LeftButton ? HandleAt( pEvent->position() ) : -1;
		if ( nHandle < 0 && pEvent->button() == Qt::LeftButton && m_pPreview->IsOnModelRing( pEvent->position() ) )
		{
			if ( const auto heading = m_pPreview->HeadingAt( pEvent->position() ) )
			{
				m_pPreview->BeginTurn( *heading );
				setCursor( Qt::SizeHorCursor );
				return;
			}
		}
		// The model is the preview's: moving it by its body moves its point in the view only (unless
		// the file places that point).
		bool bViewOnly = pEvent->modifiers() & Qt::ControlModifier;
		if ( nHandle < 0 && pEvent->button() == Qt::LeftButton && m_pPreview->IsOnModel( pEvent->position() ) )
		{
			nHandle = m_pPreview->GetModelControlPoint();
			bViewOnly = true;
		}
		if ( nHandle >= 0 )
		{
			m_pPreview->BeginDrag( nHandle, bViewOnly );
		}
		setCursor( nHandle >= 0 ? Qt::SizeAllCursor : Qt::ClosedHandCursor );
	}

	void mouseReleaseEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->button() == Qt::LeftButton )
		{
			m_pPreview->EndTurn();
			m_pPreview->EndDrag();
		}
		setCursor( HoverCursor( pEvent->position() ) );
	}

	void mouseMoveEvent( QMouseEvent *pEvent ) override
	{
		const QPointF delta = pEvent->position() - m_Last;
		m_Last = pEvent->position();
		if ( pEvent->buttons() == Qt::NoButton )
		{
			setCursor( HoverCursor( pEvent->position() ) );
			return;
		}
		particles::CCamera &camera = m_pPreview->m_Camera;
		const int nDragging = m_pPreview->GetDragged();
		if ( m_pPreview->IsTurning() )
		{
			if ( const auto heading = m_pPreview->HeadingAt( pEvent->position() ) )
			{
				m_pPreview->TurnTo( *heading );
			}
		}
		else if ( nDragging >= 0 && m_pPreview->m_pSimulation )
		{
			// Along a plane through the point facing the camera; with Shift, only up and down.
			const particles::CCamera view = m_pPreview->GetCanvasCamera();
			const CVec3 vecFrom = m_pPreview->m_pSimulation->ControlPoint( nDragging );
			const CVec3 vecEye = view.GetEye();
			const CVec3 vecRay = view.Ray( static_cast<float>( pEvent->position().x() ), static_cast<float>( pEvent->position().y() ) );
			const bool bVertical = pEvent->modifiers() & Qt::ShiftModifier;
			CVec3 vecNormal = view.GetForward();
			if ( bVertical )
			{
				vecNormal = xv::Normalized( CVec3{ vecNormal.m_flX, vecNormal.m_flY, 0 } );
			}
			const float flFacing = xv::Dot( vecRay, vecNormal );
			if ( std::abs( flFacing ) > 1e-4f )
			{
				const float t = xv::Dot( vecFrom - vecEye, vecNormal ) / flFacing;
				if ( t > 0.0f )
				{
					CVec3 vecTo = vecEye + vecRay * t;
					if ( bVertical )
					{
						vecTo = { vecFrom.m_flX, vecFrom.m_flY, vecTo.m_flZ };
					}
					m_pPreview->DragTo( vecTo );
				}
			}
		}
		else if ( pEvent->buttons() & ( Qt::RightButton | Qt::MiddleButton ) )
		{
			// Move the point looked at, by as much as the mouse moved at its distance.
			const CVec3 vecForward = camera.GetForward();
			const CVec3 vecRight = xv::Normalized( xv::Cross( vecForward, { 0, 0, 1 } ) );
			const CVec3 vecUp = xv::Cross( vecRight, vecForward );
			const float flPerPixel = 1.0f / m_pPreview->GetCanvasCamera().Scale( camera.m_flDistance );
			camera.m_vecTarget += ( vecRight * static_cast<float>( -delta.x() ) + vecUp * static_cast<float>( delta.y() ) ) * flPerPixel;
		}
		else if ( pEvent->buttons() & Qt::LeftButton )
		{
			camera.m_flYaw -= static_cast<float>( delta.x() ) * 0.4f;
			camera.m_flPitch = std::clamp( camera.m_flPitch + static_cast<float>( delta.y() ) * 0.4f, -89.0f, 89.0f );
		}
		m_pPreview->Redraw();
	}

	void wheelEvent( QWheelEvent *pEvent ) override
	{
		m_pPreview->m_Camera.m_flDistance = std::clamp( m_pPreview->m_Camera.m_flDistance * std::pow( 0.85f, static_cast<float>( pEvent->angleDelta().y() ) / 120.0f ), 4.0f, 50000.0f );
		m_pPreview->Redraw();
	}

	void mouseDoubleClickEvent( QMouseEvent * ) override
	{
		m_pPreview->FrameView();
	}

private:
	CParticlePreview *m_pPreview;
	QPointF m_Last;
};

// Control points, bounds and counts over the GPU view, which can't be painted on directly.
class CParticleOverlay : public QWidget
{
public:
	explicit CParticleOverlay( CParticlePreview *pPreview, QWidget *pParent )
		: QWidget( pParent ),
		  m_pPreview( pPreview )
	{
		setAttribute( Qt::WA_TransparentForMouseEvents );
		setAttribute( Qt::WA_NoSystemBackground );
		setAttribute( Qt::WA_TranslucentBackground );
	}

protected:
	void paintEvent( QPaintEvent * ) override
	{
		if ( m_pPreview->m_pSimulation )
		{
			QPainter painter( this );
			m_pPreview->PaintOverlays( painter );
		}
	}

private:
	CParticlePreview *m_pPreview;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticlePreview::CParticlePreview( QWidget *pParent )
	: QWidget( pParent ),
	  m_ControlPoints( DEFAULT_CONTROL_POINTS )
{
	m_pCanvas = new CParticleCanvas( this );

	m_pPlay = new QToolButton;
	m_pPlay->setObjectName( QStringLiteral( "particlePlay" ) );
	m_pPlay->setCheckable( true );
	m_pPlay->setChecked( true );
	m_pPlay->setText( QStringLiteral( "❚❚" ) );
	m_pPlay->setToolTip( tr( "Pause or play" ) );
	connect( m_pPlay, &QToolButton::toggled, this, &CParticlePreview::SetPlaying );
	auto *pRestart = new QToolButton;
	pRestart->setText( QStringLiteral( "⟲" ) );
	pRestart->setToolTip( tr( "Start again" ) );
	connect( pRestart, &QToolButton::clicked, this, &CParticlePreview::Restart );
	m_pSpeedBox = new QComboBox;
	for ( const float flSpeed : { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f } )
	{
		m_pSpeedBox->addItem( QStringLiteral( "%1×" ).arg( static_cast<double>( flSpeed ) ), flSpeed );
	}
	m_pSpeedBox->setCurrentIndex( 3 );
	m_pSpeedBox->setToolTip( tr( "Playback speed" ) );
	connect( m_pSpeedBox, &QComboBox::currentIndexChanged, this, [ this ] { m_flSpeed = m_pSpeedBox->currentData().toFloat(); } );
	m_pLoop = new QCheckBox( tr( "Loop" ) );
	m_pLoop->setChecked( true );
	m_pLoop->setToolTip( tr( "Start again when the effect is over" ) );
	m_pGrid = new QCheckBox( tr( "Grid" ) );
	m_pGrid->setChecked( true );
	m_pGrid->setToolTip( tr( "The ground grid. Control points snap to it while it shows" ) );
	connect( m_pGrid, &QCheckBox::toggled, this, &CParticlePreview::Redraw );
	m_pBounds = new QCheckBox( tr( "Bounds" ) );
	m_pBounds->setObjectName( QStringLiteral( "particleBounds" ) );
	m_pBounds->setToolTip( tr( "Each system's bounding box (yellow), where its particles are now (grey), and the "
							   "spheres and boxes its particles start in (blue). The game culls a system whose box is out of view.\n"
							   "The selected operator's shape shows either way" ) );
	connect( m_pBounds, &QCheckBox::toggled, this, &CParticlePreview::Redraw );
	auto *pFrame = new QToolButton;
	pFrame->setText( tr( "Frame" ) );
	pFrame->setToolTip( tr( "Fit the view to the effect (or double-click it)" ) );
	connect( pFrame, &QToolButton::clicked, this, &CParticlePreview::FrameView );
	m_pGpuBox = new QCheckBox( tr( "GPU" ) );
	m_pGpuBox->setObjectName( QStringLiteral( "particleGpu" ) );
	m_pGpuBox->setToolTip( tr( "Draw on the graphics card (faster, full size). Off: draw in software" ) );
#ifdef VOMIT_TOOLS_GPU_PREVIEW
	// The offscreen and minimal platforms (tests, headless runs) have no GPU to draw with.
	const QString szPlatform = QGuiApplication::platformName();
	const bool bGpuPossible = szPlatform != u"offscreen" && szPlatform != u"minimal";
#else
	const bool gpuPossible = false;
#endif
	m_pGpuBox->setVisible( bGpuPossible );
	connect( m_pGpuBox, &QCheckBox::toggled, this, [ this ]( bool bOn ) {
		SetUseGpu( bOn );
		QSettings{}.setValue( GPU_KEY, bOn );
	} );
	m_pModelButton = new QToolButton;
	m_pModelButton->setObjectName( QStringLiteral( "particleModel" ) );
	m_pModelButton->setText( tr( "Model" ) );
	m_pModelButton->setToolTip( tr( "A model standing on a control point, for effects made for players and props: "
									"Position on Model Random puts particles in its hitboxes" ) );
	m_pModelButton->setPopupMode( QToolButton::InstantPopup );
	auto *pModelMenu = new QMenu( m_pModelButton );
	connect( pModelMenu, &QMenu::aboutToShow, this, [ this, pModelMenu ] { FillModelMenu( pModelMenu ); } );
	m_pModelButton->setMenu( pModelMenu );
	auto *pResetPoints = new QToolButton;
	pResetPoints->setText( tr( "Reset CPs" ) );
	pResetPoints->setToolTip( tr( "Put the control points the file doesn't place (CP0 among them) back where they started. "
								  "Points the file places move back with Undo" ) );
	connect( pResetPoints, &QToolButton::clicked, this, &CParticlePreview::ResetControlPoints );

	m_pTimeline = new QSlider( Qt::Horizontal );
	m_pTimeline->setObjectName( QStringLiteral( "particleTimeline" ) );
	m_pTimeline->setRange( 0, TIMELINE_START );
	m_pTimeline->setToolTip( tr( "Where the effect is in its time; drag to go to a moment (it pauses)" ) );
	connect( m_pTimeline, &QSlider::sliderMoved, this, [ this ]( int nMilliseconds ) {
		SetPlaying( false );
		Seek( static_cast<float>( nMilliseconds ) / 1000.0f );
	} );

	m_pInfo = new QLabel;
	m_pInfo->setObjectName( QStringLiteral( "particleInfo" ) );
	m_pInfo->setWordWrap( true );
	m_pInfo->setTextFormat( Qt::RichText );

	auto *pBar = new QHBoxLayout;
	pBar->setContentsMargins( 0, 0, 0, 0 );
	pBar->addWidget( m_pPlay );
	pBar->addWidget( pRestart );
	pBar->addWidget( m_pSpeedBox );
	pBar->addWidget( m_pLoop );
	pBar->addWidget( m_pGrid );
	pBar->addWidget( m_pBounds );
	pBar->addWidget( m_pGpuBox );
	pBar->addStretch();
	pBar->addWidget( m_pModelButton );
	pBar->addWidget( pFrame );
	pBar->addWidget( pResetPoints );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 4 );
	pLayout->addWidget( m_pCanvas, 1 );
	pLayout->addLayout( pBar );
	pLayout->addWidget( m_pTimeline );
	pLayout->addWidget( m_pInfo );

	m_pTimer = new QTimer( this );
	connect( m_pTimer, &QTimer::timeout, this, &CParticlePreview::Tick );
	m_pTimer->start( 16 );
	m_Clock.start();
	UpdateInfo();
	if ( bGpuPossible && QSettings{}.value( GPU_KEY, true ).toBool() )
	{
		m_pGpuBox->setChecked( true );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draws on the graphics card, or in software.
//-----------------------------------------------------------------------------
void CParticlePreview::SetUseGpu( bool bOn )
{
	{
		const QSignalBlocker blocker{ m_pGpuBox };
		m_pGpuBox->setChecked( bOn );
	}
	delete m_pGpu;
	delete m_pOverlay;
	m_pGpu = nullptr;
	m_pOverlay = nullptr;
#ifdef VOMIT_TOOLS_GPU_PREVIEW
	if ( bOn )
	{
		auto *pView = new CParticleGpuView( [ this ]( int nWidth, int nHeight, std::vector<particles::ScreenQuad_t> &quads, std::vector<particles::ScreenMesh_t> &meshes, std::array<float, 3> & ) {
			if ( !m_pSimulation )
			{
				return;
			}
			if ( !m_bCameraSet )
			{
				FrameView();
			}
			particles::CCamera camera = m_Camera;
			camera.m_nWidth = nWidth;
			camera.m_nHeight = nHeight;
			if ( m_pGrid->isChecked() )
			{
				quads = particles::GridQuads( camera );
			}
			const auto drawn = particles::Layout( *m_pSimulation, camera, [ this ]( std::size_t i ) { return Sprite( i ); } );
			quads.insert( quads.end(), drawn.begin(), drawn.end() );
			meshes = particles::LayoutModel( *m_pSimulation, camera, [ this ]( std::size_t i ) { return ModelSprite( i ); } );
		},
			m_pCanvas );
		pView->setAttribute( Qt::WA_TransparentForMouseEvents );
		pView->setGeometry( m_pCanvas->rect() );
		// If the GPU can't be used after all, draw in software.
		connect( pView, &QRhiWidget::renderFailed, this, [ this ] {
			QTimer::singleShot( 0, this, [ this ] {
				SetUseGpu( false );
				m_pGpuBox->setEnabled( false );
				m_pGpuBox->setToolTip( tr( "The graphics card couldn't be used; drawing in software" ) );
			} );
		} );
		m_pGpu = pView;
		m_pOverlay = new CParticleOverlay( this, m_pCanvas );
		m_pOverlay->setGeometry( m_pCanvas->rect() );
		m_pGpu->show();
		m_pOverlay->show();
		m_pOverlay->raise();
	}
#endif
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Asks for the picture to be drawn again.
//-----------------------------------------------------------------------------
void CParticlePreview::Redraw()
{
	m_bDirty = true;
	if ( m_pGpu )
	{
		m_pGpu->update();
		m_pOverlay->update();
	}
	else
	{
		m_pCanvas->update();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticlePreview::~CParticlePreview() = default;

//-----------------------------------------------------------------------------
// Purpose: Where materials and textures are read from.
//-----------------------------------------------------------------------------
void CParticlePreview::SetReader( particles::FileReader_t pfnReader )
{
	m_pfnReader = std::move( pfnReader );
#ifdef VOMIT_TOOLS_GPU_PREVIEW
	if ( auto *pView = qobject_cast<CParticleGpuView *>( m_pGpu ) )
	{
		pView->ForgetTextures();
	}
#endif
	m_SpriteCache.clear();
	m_ModelSprites.clear();
	m_Sprites.assign( m_pLibrary ? m_pLibrary->m_Definitions.size() : 0, nullptr );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: The systems of the file being edited. The current system keeps playing, from about
//          where it
//          was, so edits show at once.
//-----------------------------------------------------------------------------
void CParticlePreview::SetDocument( const xv::dmx::Document_t &document )
{
	m_pLibrary = std::make_shared<const particles::CLibrary>( particles::Compile( document ) );
	if ( m_Drag.m_bEnded )
	{
		m_Drag = {}; // the file places the point now
	}
	Rebuild( true );
}

//-----------------------------------------------------------------------------
// Purpose: Shows a system by name, from the start, framed.
//-----------------------------------------------------------------------------
void CParticlePreview::SetSystem( const std::string &szName )
{
	if ( szName == m_szSystem && m_pSimulation )
	{
		return;
	}
	m_szSystem = szName;
	Rebuild( false );
	FrameView();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::Rebuild( bool bKeepTime )
{
	const float flTime = bKeepTime && m_pSimulation ? m_pSimulation->GetTime() : 0.0f;
	m_pSimulation.reset();
	m_Sprites.assign( m_pLibrary ? m_pLibrary->m_Definitions.size() : 0, nullptr );
	m_flIdle = 0.0f;
	if ( m_pLibrary )
	{
		if ( const auto index = m_pLibrary->Find( m_szSystem ) )
		{
			m_pSimulation = std::make_unique<particles::CSimulation>( m_pLibrary, *index );
			m_pSimulation->SetSequenceFrames( [ this ]( std::size_t nDefinition, int nSequence ) {
				const particles::Sprite_t *pDrawn = this->Sprite( nDefinition );
				const auto *pFound = pDrawn ? pDrawn->m_Sheet.Sequence( nSequence ) : nullptr;
				return pFound ? static_cast<int>( pFound->m_Frames.size() ) : 0;
			} );
			for ( int i = 0; i < particles::CSimulation::CONTROL_POINTS; i++ )
			{
				m_pSimulation->SetControlPoint( i, m_ControlPoints[ static_cast<std::size_t>( i ) ] );
			}
			m_pSimulation->SetModel( m_pModel, m_nModelPoint );
			m_pSimulation->SetModelAngles( m_ModelAngles );
			if ( m_Drag.m_nIndex >= 0 && m_Drag.m_bInFile && m_Drag.m_bMoved )
			{
				// Still being dragged, or let go and not back from the file yet.
				m_pSimulation->HoldControlPoint( m_Drag.m_nIndex, m_Drag.m_vecOrigin + m_Drag.m_vecLocation );
			}
			m_pSimulation->Restart(); // from the control points' places, not from where they were dragged from
			Advance( std::min( flTime, CATCH_UP ) );
		}
	}
	Redraw();
	UpdateInfo();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::SetPlaying( bool bPlaying )
{
	const QSignalBlocker blocker{ m_pPlay };
	m_pPlay->setChecked( bPlaying );
	m_pPlay->setText( bPlaying ? QStringLiteral( "❚❚" ) : QStringLiteral( "▶" ) );
	m_Clock.restart();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::Restart()
{
	if ( m_pSimulation )
	{
		m_pSimulation->Restart();
		m_flIdle = 0.0f;
		Redraw();
		UpdateInfo();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Moves the camera to take in what the system shows.
//-----------------------------------------------------------------------------
void CParticlePreview::FrameView()
{
	if ( !m_bCameraSet )
	{
		m_Camera = {};
		m_bCameraSet = true;
	}
	if ( m_pSimulation )
	{
		m_Camera = particles::Framed( *m_pSimulation, m_Camera );
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Advances the simulation by seconds (the timer does this while playing).
//-----------------------------------------------------------------------------
void CParticlePreview::Advance( float flSeconds )
{
	if ( !m_pSimulation )
	{
		return;
	}
	m_pSimulation->SetViewer( m_Camera.GetEye() ); // the player, for Set Control Point To Player
	m_pSimulation->Advance( flSeconds );
	Redraw();
	UpdateInfo();
}

//-----------------------------------------------------------------------------
// Purpose: Shows the effect as it is so many seconds after it starts.
//-----------------------------------------------------------------------------
void CParticlePreview::Seek( float flSeconds )
{
	if ( !m_pSimulation )
	{
		return;
	}
	flSeconds = std::max( 0.0f, flSeconds );
	if ( flSeconds < m_pSimulation->GetTime() )
	{
		m_pSimulation->Restart();
		m_flIdle = 0.0f;
	}
	Advance( flSeconds - m_pSimulation->GetTime() );
}

//-----------------------------------------------------------------------------
// Purpose: Puts a control point somewhere else, without starting over.
//-----------------------------------------------------------------------------
void CParticlePreview::MoveControlPoint( int nIndex, CVec3 vecPosition )
{
	if ( nIndex < 0 || nIndex >= particles::CSimulation::CONTROL_POINTS )
	{
		return;
	}
	m_ControlPoints[ static_cast<std::size_t>( nIndex ) ] = vecPosition;
	if ( m_pSimulation )
	{
		m_pSimulation->SetControlPoint( nIndex, vecPosition );
	}
	if ( m_pModel && nIndex == m_nModelPoint )
	{
		FollowModel();
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::ResetControlPoints()
{
	const ControlPoints_t before = m_ControlPoints;
	SetControlPoints( DEFAULT_CONTROL_POINTS );
	if ( before != m_ControlPoints )
	{
		emit ControlPointsMoved( before, m_ControlPoints, tr( "Reset control points" ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::SetControlPoints( const ControlPoints_t &points )
{
	for ( int i = 0; i < particles::CSimulation::CONTROL_POINTS; i++ )
	{
		MoveControlPoint( i, points[ static_cast<std::size_t>( i ) ] );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Whether a "Set Control Point Positions" operator of the shown systems places a point.
//-----------------------------------------------------------------------------
bool CParticlePreview::IsPlacedByFile( int nIndex ) const
{
	return m_pSimulation && m_pSimulation->FindControlPointSetter( nIndex ).has_value();
}

//-----------------------------------------------------------------------------
// Purpose: Starts dragging a control point. Points other than 0 go into the file: into the slots
//          of the operator that already places them, else a spare slot of the system's own
//          operators, else a new operator.
//-----------------------------------------------------------------------------
void CParticlePreview::BeginDrag( int nIndex, bool bViewOnly )
{
	if ( m_Drag.m_bEnded && m_pSimulation )
	{
		m_pSimulation->ReleaseControlPoints(); // the last one never came back from the file
	}
	m_Drag = {};
	if ( !m_pSimulation || nIndex < 0 || nIndex >= particles::CSimulation::CONTROL_POINTS )
	{
		return;
	}
	m_Drag.m_nIndex = nIndex;
	m_Drag.m_Before = m_ControlPoints;
	m_Attached.erase( nIndex ); // moved by hand: off its attachment
	const auto setter = m_pSimulation->FindControlPointSetter( nIndex );
	m_Drag.m_bInFile = m_pfnPlace && ( setter || ( nIndex > 0 && !bViewOnly ) );
	if ( !m_Drag.m_bInFile )
	{
		return;
	}
	const auto slot = setter ? setter : m_pSimulation->FindSpareSetter();
	if ( slot )
	{
		m_Drag.m_nOperator = slot->m_pOperator->m_nElement;
		m_Drag.m_nSlot = setter ? -1 : slot->m_nSlot;
		m_Drag.m_vecOrigin = m_pSimulation->SetterOrigin( *slot->m_pOperator );
	}
	else
	{
		m_Drag.m_vecOrigin = m_pSimulation->ControlPoint( 0 ); // where a new operator measures from
	}
	m_Drag.m_vecLocation = m_pSimulation->ControlPoint( nIndex ) - m_Drag.m_vecOrigin;
}

//-----------------------------------------------------------------------------
// Purpose: Moves the dragged point towards a place in the world, snapped to the grid while it
//          shows. For the file the location is snapped, so its numbers stay round.
//-----------------------------------------------------------------------------
void CParticlePreview::DragTo( CVec3 vecPosition )
{
	if ( m_Drag.m_nIndex < 0 || m_Drag.m_bEnded || !m_pSimulation )
	{
		return;
	}
	const CVec3 vecOrigin = m_Drag.m_bInFile ? m_Drag.m_vecOrigin : CVec3{};
	CVec3 vecOffset = vecPosition - vecOrigin;
	if ( m_pGrid->isChecked() )
	{
		const float flStep = particles::GridSpacing( m_Camera );
		const auto snap = [ flStep ]( float v ) { return std::round( v / flStep ) * flStep + 0.0f; }; // + 0 turns -0 into 0
		vecOffset = { snap( vecOffset.m_flX ), snap( vecOffset.m_flY ), snap( vecOffset.m_flZ ) };
	}
	if ( !m_Drag.m_bInFile )
	{
		MoveControlPoint( m_Drag.m_nIndex, vecOffset );
		m_Drag.m_bMoved = m_ControlPoints != m_Drag.m_Before;
		return;
	}
	if ( vecOffset == m_Drag.m_vecLocation )
	{
		return;
	}
	m_Drag.m_vecLocation = vecOffset;
	m_Drag.m_bMoved = true;
	m_pSimulation->HoldControlPoint( m_Drag.m_nIndex, vecOrigin + vecOffset );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Lets go of the dragged point: into the file, or as a view step for undo.
//-----------------------------------------------------------------------------
void CParticlePreview::EndDrag()
{
	if ( m_Drag.m_nIndex < 0 || m_Drag.m_bEnded )
	{
		return;
	}
	const Drag_t drag = m_Drag;
	if ( !drag.m_bMoved )
	{
		m_Drag = {};
		return;
	}
	if ( !drag.m_bInFile )
	{
		m_Drag = {};
		emit ControlPointsMoved( drag.m_Before, m_ControlPoints, m_pModel && drag.m_nIndex == m_nModelPoint ? tr( "Move model" ) : tr( "Move CP%1" ).arg( drag.m_nIndex ) );
		return;
	}
	m_Drag.m_bEnded = true;
	if ( !m_pfnPlace( drag.m_nIndex, drag.m_vecLocation, drag.m_nOperator, drag.m_nSlot ) && m_Drag.m_bEnded )
	{
		// The file didn't change (the point is back where it was): nothing will replace the hold.
		if ( m_pSimulation )
		{
			m_pSimulation->ReleaseControlPoints();
		}
		m_Drag = {};
		Redraw();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The control points the shown systems use (always 0).
//-----------------------------------------------------------------------------
std::vector<int> CParticlePreview::GetUsedControlPoints() const
{
	std::set<int> used{ 0 };
	if ( m_pSimulation )
	{
		ForEachInstance( m_pSimulation->GetRoot(), [ this, &used ]( const particles::CSimulation::Instance_t &instance ) {
			// Not the points a parent sets from its particles: they aren't the shared ones.
			const auto shared = m_pSimulation->GetSharedControlPoints( instance );
			for ( int nPoint : instance.m_pDefinition->GetControlPoints() )
			{
				if ( std::ranges::find( shared, nPoint ) != shared.end() )
				{
					used.insert( nPoint );
				}
			}
		} );
	}
	return { used.begin(), used.end() };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::Tick()
{
	const float flElapsed = std::min( 0.1f, static_cast<float>( m_Clock.restart() ) / 1000.0f );
	if ( !isVisible() || !m_pSimulation || !m_pPlay->isChecked() )
	{
		return;
	}
	Advance( flElapsed * m_flSpeed );
	if ( m_pLoop->isChecked() && m_pSimulation->GetFinished() )
	{
		m_flIdle += flElapsed;
		if ( m_flIdle >= LOOP_PAUSE )
		{
			Restart();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const particles::Sprite_t *CParticlePreview::Sprite( std::size_t nDefinition )
{
	if ( !m_pLibrary || nDefinition >= m_pLibrary->m_Definitions.size() )
	{
		return nullptr;
	}
	if ( m_Sprites.size() != m_pLibrary->m_Definitions.size() )
	{
		m_Sprites.assign( m_pLibrary->m_Definitions.size(), nullptr );
	}
	if ( !m_Sprites[ nDefinition ] )
	{
		const std::string &szMaterial = m_pLibrary->m_Definitions[ nDefinition ].m_szMaterial;
		auto &pCached = m_SpriteCache[ szMaterial ];
		if ( !pCached )
		{
			pCached = std::make_shared<const particles::Sprite_t>( particles::LoadSprite( szMaterial, m_pfnReader ) );
		}
		m_Sprites[ nDefinition ] = pCached.get();
	}
	return m_Sprites[ nDefinition ];
}

//-----------------------------------------------------------------------------
// Purpose: The picture as it is drawn now, without the overlays.
//-----------------------------------------------------------------------------
QImage CParticlePreview::Frame()
{
	if ( !m_bCameraSet )
	{
		FrameView();
	}
	particles::CCamera camera = m_Camera;
	camera.m_nWidth = std::clamp( m_pCanvas->width(), 64, MAX_RENDER_WIDTH );
	camera.m_nHeight = std::max( 48, camera.m_nWidth * std::max( 1, m_pCanvas->height() ) / std::max( 1, m_pCanvas->width() ) );
	xv::texture::CImage out;
	if ( m_pSimulation )
	{
		particles::Render( *m_pSimulation, camera, [ this ]( std::size_t i ) { return Sprite( i ); }, out,
			{ .m_bGrid = m_pGrid->isChecked(), .m_pfnModelSprites = [ this ]( std::size_t i ) { return ModelSprite( i ); } } );
	}
	else
	{
		out = xv::texture::CImage( camera.m_nWidth, camera.m_nHeight );
	}
	return QImage( out.m_Pixels.data(), out.m_nWidth, out.m_nHeight, out.m_nWidth * 4, QImage::Format_RGBA8888 ).copy();
}

//-----------------------------------------------------------------------------
// Purpose: A small square picture of a system of the current file, a moment after it starts,
//          framed.
//-----------------------------------------------------------------------------
QImage CParticlePreview::Thumbnail( const std::string &szName, int nSize )
{
	const auto index = m_pLibrary ? m_pLibrary->Find( szName ) : std::nullopt;
	if ( !index )
	{
		return {};
	}
	particles::CSimulation simulation{ m_pLibrary, *index };
	simulation.SetSequenceFrames( [ this ]( std::size_t nDefinition, int nSequence ) {
		const particles::Sprite_t *pDrawn = this->Sprite( nDefinition );
		const auto *pFound = pDrawn ? pDrawn->m_Sheet.Sequence( nSequence ) : nullptr;
		return pFound ? static_cast<int>( pFound->m_Frames.size() ) : 0;
	} );
	for ( int i = 0; i < particles::CSimulation::CONTROL_POINTS; i++ )
	{
		simulation.SetControlPoint( i, m_ControlPoints[ static_cast<std::size_t>( i ) ] );
	}
	simulation.Restart();
	// Long enough for most effects to show, short enough to catch quick bursts.
	simulation.Advance( 0.5f );
	for ( int i = 0; i < 8 && simulation.GetParticleCount() == 0; i++ )
	{
		simulation.Advance( 0.25f );
	}
	particles::CCamera camera = particles::Framed( simulation, {}, 1.0f );
	camera.m_nWidth = nSize;
	camera.m_nHeight = nSize;
	xv::texture::CImage out;
	particles::Render( simulation, camera, [ this ]( std::size_t i ) { return Sprite( i ); }, out, { .m_bGrid = false, .m_pfnModelSprites = {} } );
	return QImage( out.m_Pixels.data(), out.m_nWidth, out.m_nHeight, out.m_nWidth * 4, QImage::Format_RGBA8888 ).copy();
}

//-----------------------------------------------------------------------------
// Purpose: The camera as it maps onto the canvas (the picture itself may be drawn smaller).
//-----------------------------------------------------------------------------
particles::CCamera CParticlePreview::GetCanvasCamera() const
{
	particles::CCamera camera = m_Camera;
	camera.m_nWidth = std::max( 1, m_pCanvas->width() );
	camera.m_nHeight = std::max( 1, m_pCanvas->height() );
	return camera;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::PaintCanvas( QPainter &painter, const QRect &area )
{
	if ( !m_pSimulation )
	{
		painter.fillRect( area, palette().window() );
		painter.drawText( area, Qt::AlignCenter | Qt::TextWordWrap, m_pLibrary && !m_szSystem.empty() ? tr( "%1 isn't in the file any more." ).arg( Qs( m_szSystem ) ) : tr( "Pick a system to see it play." ) );
		return;
	}
	if ( m_bDirty || m_Image.isNull() || m_Image.width() != std::clamp( area.width(), 64, MAX_RENDER_WIDTH ) )
	{
		m_Image = Frame();
		m_bDirty = false;
	}
	painter.setRenderHint( QPainter::SmoothPixmapTransform );
	painter.drawImage( area, m_Image );
	PaintOverlays( painter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::PaintOverlays( QPainter &painter )
{
	painter.setRenderHint( QPainter::Antialiasing );
	const particles::CCamera camera = GetCanvasCamera();
	if ( m_pBounds->isChecked() )
	{
		PaintBounds( painter, camera );
	}
	PaintShapes( painter, camera );
	// The model's attachments, where control points can be put (with Bounds on).
	if ( m_pModel && m_pBounds->isChecked() )
	{
		const xv::model::CTransform toWorld = m_pSimulation->ModelToWorld();
		painter.setPen( QColor( 170, 230, 170, 200 ) );
		for ( std::size_t i = 0; i < m_pModel->m_Attachments.size(); i++ )
		{
			if ( const auto at = camera.Project( toWorld.Apply( m_pModel->AttachmentToModel( i ).GetOrigin() ) ) )
			{
				const QPointF p{ ( *at )[ 0 ], ( *at )[ 1 ] };
				painter.drawEllipse( p, 2.0, 2.0 );
				painter.drawText( p + QPointF{ 4, 3 }, Qs( m_pModel->m_Attachments[ i ].m_szName ) );
			}
		}
	}
	// The ring the model turns by, with a tick the way it faces.
	if ( m_pModel )
	{
		const auto [ vecCenter, flRadius ] = ModelRing();
		painter.setPen( QPen( m_bTurning ? QColor( 255, 220, 140, 230 ) : QColor( 255, 200, 120, 130 ), m_bTurning ? 2.0 : 1.2 ) );
		painter.setBrush( Qt::NoBrush );
		constexpr int SEGMENTS = 64;
		std::optional<QPointF> previous;
		for ( int i = 0; i <= SEGMENTS; i++ )
		{
			const float flAngle = 2.0f * std::numbers::pi_v<float> * static_cast<float>( i ) / SEGMENTS;
			const auto p = camera.Project( vecCenter + CVec3{ std::cos( flAngle ), std::sin( flAngle ), 0.0f } * flRadius );
			const std::optional<QPointF> here = p ? std::optional{ QPointF{ ( *p )[ 0 ], ( *p )[ 1 ] } } : std::nullopt;
			if ( here && previous )
			{
				painter.drawLine( *previous, *here );
			}
			previous = here;
		}
		const float flYaw = m_ModelAngles[ 1 ] * std::numbers::pi_v<float> / 180.0f;
		const CVec3 vecFacing{ std::cos( flYaw ), std::sin( flYaw ), 0.0f };
		const auto inner = camera.Project( vecCenter + vecFacing * ( flRadius * 0.8f ) );
		const auto outer = camera.Project( vecCenter + vecFacing * ( flRadius * 1.15f ) );
		if ( inner && outer )
		{
			painter.drawLine( QPointF{ ( *inner )[ 0 ], ( *inner )[ 1 ] }, QPointF{ ( *outer )[ 0 ], ( *outer )[ 1 ] } );
		}
	}
	// The control points the systems use, as handles.
	for ( int nIndex : GetUsedControlPoints() )
	{
		const auto at = camera.Project( m_pSimulation->ControlPoint( nIndex ) );
		if ( !at )
		{
			continue;
		}
		const QPointF p{ ( *at )[ 0 ], ( *at )[ 1 ] };
		const QColor color = ControlPointColor( nIndex );
		painter.setPen( QPen( color, 1.5 ) );
		painter.setBrush( QColor( color.red(), color.green(), color.blue(), 60 ) );
		if ( IsPlacedByFile( nIndex ) || ( m_Drag.m_nIndex == nIndex && m_Drag.m_bInFile ) )
		{
			painter.drawRect( QRectF{ p - QPointF{ 4.5, 4.5 }, QSizeF{ 9.0, 9.0 } } ); // a square: the file places it
		}
		else
		{
			painter.drawEllipse( p, 5.0, 5.0 );
		}
		painter.drawLine( p - QPointF{ 9, 0 }, p - QPointF{ 5, 0 } );
		painter.drawLine( p + QPointF{ 5, 0 }, p + QPointF{ 9, 0 } );
		painter.drawLine( p - QPointF{ 0, 9 }, p - QPointF{ 0, 5 } );
		painter.drawLine( p + QPointF{ 0, 5 }, p + QPointF{ 0, 9 } );
		painter.drawText( p + QPointF{ 10, -6 }, QStringLiteral( "CP%1" ).arg( nIndex ) );
	}
	PaintCounts( painter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::PaintBounds( QPainter &painter, const particles::CCamera &camera )
{
	const auto box = [ & ]( CVec3 vecLow, CVec3 vecHigh, const QPen &pen ) {
		const auto corner = [ & ]( int i ) { return CVec3{ ( i & 1 ) ? vecHigh.m_flX : vecLow.m_flX, ( i & 2 ) ? vecHigh.m_flY : vecLow.m_flY, ( i & 4 ) ? vecHigh.m_flZ : vecLow.m_flZ }; };
		painter.setPen( pen );
		painter.setBrush( Qt::NoBrush );
		for ( int a = 0; a < 8; a++ )
		{
			for ( int nBit : { 1, 2, 4 } )
			{
				const int b = a | nBit;
				if ( b == a )
				{
					continue;
				}
				const auto pa = camera.Project( corner( a ) );
				const auto pb = camera.Project( corner( b ) );
				if ( pa && pb )
				{
					painter.drawLine( QPointF{ ( *pa )[ 0 ], ( *pa )[ 1 ] }, QPointF{ ( *pb )[ 0 ], ( *pb )[ 1 ] } );
				}
			}
		}
	};
	ForEachInstance( m_pSimulation->GetRoot(), [ & ]( const particles::CSimulation::Instance_t &instance ) {
		const particles::CDefinition &definition = *instance.m_pDefinition;
		const CVec3 vecOrigin = m_pSimulation->ControlPoint( definition.m_nBoundsControlPoint );
		box( vecOrigin + definition.m_vecBoundsMin, vecOrigin + definition.m_vecBoundsMax, QPen( QColor( 255, 220, 90, 170 ), 1.0 ) );
		if ( instance.m_Particles.empty() )
		{
			return;
		}
		CVec3 vecLow = instance.m_Particles.front().m_vecPosition;
		CVec3 vecHigh = vecLow;
		for ( const particles::Particle_t &p : instance.m_Particles )
		{
			vecLow = { std::min( vecLow.m_flX, p.m_vecPosition.m_flX - p.m_flRadius ), std::min( vecLow.m_flY, p.m_vecPosition.m_flY - p.m_flRadius ), std::min( vecLow.m_flZ, p.m_vecPosition.m_flZ - p.m_flRadius ) };
			vecHigh = { std::max( vecHigh.m_flX, p.m_vecPosition.m_flX + p.m_flRadius ), std::max( vecHigh.m_flY, p.m_vecPosition.m_flY + p.m_flRadius ), std::max( vecHigh.m_flZ, p.m_vecPosition.m_flZ + p.m_flRadius ) };
		}
		box( vecLow, vecHigh, QPen( QColor( 200, 200, 200, 120 ), 1.0, Qt::DashLine ) );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The operator selected in the editor: its emission shape shows even without Bounds.
//-----------------------------------------------------------------------------
void CParticlePreview::SetSelectedElement( std::int32_t nElement )
{
	if ( nElement != m_nSelectedElement )
	{
		m_nSelectedElement = nElement;
		Redraw();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Where the position initializers put particles, around their control points: a
//          sphere's outer (and inner) shell as three rings, squashed by its bias the way the
//          initializer squashes it; a box; an offset's range as a dashed box. All of them with
//          Bounds on, the selected one brighter either way.
//-----------------------------------------------------------------------------
void CParticlePreview::PaintShapes( QPainter &painter, const particles::CCamera &camera )
{
	const auto project = [ &camera ]( CVec3 v ) -> std::optional<QPointF> {
		if ( const auto p = camera.Project( v ) )
		{
			return QPointF{ ( *p )[ 0 ], ( *p )[ 1 ] };
		}
		return std::nullopt;
	};
	const auto polyline = [ & ]( const std::vector<CVec3> &points ) {
		for ( std::size_t i = 0; i + 1 < points.size(); i++ )
		{
			const auto a = project( points[ i ] );
			const auto b = project( points[ i + 1 ] );
			if ( a && b )
			{
				painter.drawLine( *a, *b );
			}
		}
	};
	const auto box = [ & ]( CVec3 vecLow, CVec3 vecHigh ) {
		const auto corner = [ & ]( int i ) { return CVec3{ ( i & 1 ) ? vecHigh.m_flX : vecLow.m_flX, ( i & 2 ) ? vecHigh.m_flY : vecLow.m_flY, ( i & 4 ) ? vecHigh.m_flZ : vecLow.m_flZ }; };
		for ( int a = 0; a < 8; a++ )
		{
			for ( int nBit : { 1, 2, 4 } )
			{
				if ( !( a & nBit ) )
				{
					polyline( { corner( a ), corner( a | nBit ) } );
				}
			}
		}
	};
	const auto sphere = [ & ]( CVec3 vecCenter, float flRadius, CVec3 vecBias, CVec3 vecAbsolute ) {
		constexpr int SEGMENTS = 48;
		for ( int nPlane = 0; nPlane < 3; nPlane++ )
		{
			std::vector<CVec3> ring;
			for ( int i = 0; i <= SEGMENTS; i++ )
			{
				const float flAngle = 2.0f * std::numbers::pi_v<float> * static_cast<float>( i ) / SEGMENTS;
				const float c = std::cos( flAngle );
				const float d = std::sin( flAngle );
				CVec3 v = nPlane == 0 ? CVec3{ c, d, 0 } : nPlane == 1 ? CVec3{ c, 0, d }
																	   : CVec3{ 0, c, d };
				v = { v.m_flX * vecBias.m_flX, v.m_flY * vecBias.m_flY, v.m_flZ * vecBias.m_flZ };
				v = { vecAbsolute.m_flX != 0.0f ? std::abs( v.m_flX ) : v.m_flX, vecAbsolute.m_flY != 0.0f ? std::abs( v.m_flY ) : v.m_flY, vecAbsolute.m_flZ != 0.0f ? std::abs( v.m_flZ ) : v.m_flZ };
				ring.push_back( vecCenter + v * flRadius );
			}
			polyline( ring );
		}
	};
	const bool bAll = m_pBounds->isChecked();
	painter.setBrush( Qt::NoBrush );
	ForEachInstance( m_pSimulation->GetRoot(), [ & ]( const particles::CSimulation::Instance_t &instance ) {
		for ( const particles::CCompiledOperator &op : instance.m_pDefinition->operators )
		{
			const bool bSelected = op.m_nElement >= 0 && op.m_nElement == m_nSelectedElement;
			if ( op.m_nCategory != particles::Category_t::CATEGORY_INITIALIZERS || !( bAll || bSelected ) )
			{
				continue;
			}
			const QColor color = bSelected ? QColor( 120, 210, 255, 230 ) : QColor( 90, 170, 255, 140 );
			const qreal flWidth = bSelected ? 1.5 : 1.0;
			painter.setPen( QPen( color, flWidth ) );
			if ( op.m_szFunction == "Position Within Sphere Random" )
			{
				const CVec3 vecCenter = m_pSimulation->ControlPoint( op.Integer( "control_point_number" ) );
				const CVec3 vecBias = op.Vector( "distance_bias", { 1, 1, 1 } );
				const CVec3 vecAbsolute = op.Vector( "distance_bias_absolute_value" );
				const float flMax = op.Number( "distance_max" );
				const float flMin = op.Number( "distance_min" );
				sphere( vecCenter, flMax, vecBias, vecAbsolute );
				if ( flMin > 0.0f && flMin < flMax )
				{
					painter.setPen( QPen( color, flWidth, Qt::DashLine ) );
					sphere( vecCenter, flMin, vecBias, vecAbsolute );
				}
			}
			else if ( op.m_szFunction == "Position Within Box Random" )
			{
				const CVec3 vecCenter = m_pSimulation->ControlPoint( op.Integer( "control point number" ) );
				box( vecCenter + op.Vector( "min" ), vecCenter + op.Vector( "max" ) );
			}
			else if ( op.m_szFunction == "Position Modify Offset Random" )
			{
				// Added to wherever the particle was put; shown around its control point.
				const CVec3 vecCenter = m_pSimulation->ControlPoint( op.Integer( "control_point_number" ) );
				const float flScale = op.Flag( "offset proportional to radius 0/1" ) ? instance.m_pDefinition->m_flRadius : 1.0f;
				painter.setPen( QPen( color, flWidth, Qt::DashLine ) );
				box( vecCenter + op.Vector( "offset min" ) * flScale, vecCenter + op.Vector( "offset max" ) * flScale );
			}
			else if ( op.m_szFunction == "Position on Model Random" && m_pModel && op.Integer( "control_point_number" ) == m_nModelPoint )
			{
				// The hitboxes the particles start in, each turned with its bone.
				const xv::model::CTransform toWorld = m_pSimulation->ModelToWorld();
				for ( std::size_t nBox = 0; nBox < m_pModel->m_Hitboxes.size(); nBox++ )
				{
					const auto corner = [ & ]( int i ) { return toWorld.Apply( m_pModel->HitboxPoint( nBox, { ( i & 1 ) ? 1.0f : 0.0f, ( i & 2 ) ? 1.0f : 0.0f, ( i & 4 ) ? 1.0f : 0.0f } ) ); };
					for ( int a = 0; a < 8; a++ )
					{
						for ( int nBit : { 1, 2, 4 } )
						{
							if ( !( a & nBit ) )
							{
								polyline( { corner( a ), corner( a | nBit ) } );
							}
						}
					}
				}
			}
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose: How full each shown system is.
//-----------------------------------------------------------------------------
std::vector<CParticlePreview::Count_t> CParticlePreview::GetCounts() const
{
	std::vector<Count_t> counts;
	if ( m_pSimulation )
	{
		ForEachInstance( m_pSimulation->GetRoot(), [ &counts ]( const particles::CSimulation::Instance_t &instance ) {
			counts.push_back( { instance.m_pDefinition->m_szName, static_cast<int>( instance.m_Particles.size() ), instance.m_pDefinition->m_nMaxParticles } );
		} );
	}
	return counts;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::PaintCounts( QPainter &painter )
{
	// One row per system: its name, a bar of how many particles it has out of max_particles, and
	// the numbers. A full bar turns orange: the system can't emit more until some die.
	const auto rows = GetCounts();
	if ( rows.empty() )
	{
		return;
	}
	QFont font = painter.font();
	font.setPointSizeF( font.pointSizeF() * 0.85 );
	painter.setFont( font );
	const QFontMetrics metrics{ font };
	const int nShown = std::min<int>( static_cast<int>( rows.size() ), 8 );
	int nNameWidth = 0;
	for ( int i = 0; i < nShown; i++ )
	{
		nNameWidth = std::max( nNameWidth, metrics.horizontalAdvance( Qs( rows[ static_cast<std::size_t>( i ) ].m_szSystem ) ) );
	}
	nNameWidth = std::min( nNameWidth, 160 );
	const int nBarWidth = 70;
	const int nRowHeight = metrics.height() + 2;
	const int nLines = nShown + ( static_cast<int>( rows.size() ) > nShown ? 1 : 0 );
	const QRect panel{ 6, 6, nNameWidth + nBarWidth + 90, nRowHeight * nLines + 6 };
	painter.setPen( Qt::NoPen );
	painter.setBrush( QColor( 0, 0, 0, 110 ) );
	painter.drawRoundedRect( panel, 4, 4 );
	for ( int i = 0; i < nShown; i++ )
	{
		const Count_t &row = rows[ static_cast<std::size_t>( i ) ];
		const int y = panel.top() + 3 + i * nRowHeight;
		const bool bFull = row.m_nLimit > 0 && row.m_nParticles >= row.m_nLimit;
		painter.setPen( QColor( 230, 230, 230 ) );
		painter.drawText( QRect{ panel.left() + 6, y, nNameWidth, nRowHeight }, Qt::AlignVCenter | Qt::AlignLeft,
			metrics.elidedText( Qs( row.m_szSystem ), Qt::ElideRight, nNameWidth ) );
		const QRect bar{ panel.left() + 12 + nNameWidth, y + nRowHeight / 2 - 3, nBarWidth, 6 };
		painter.setPen( Qt::NoPen );
		painter.setBrush( QColor( 255, 255, 255, 40 ) );
		painter.drawRect( bar );
		const qreal flShare = row.m_nLimit > 0 ? std::min( 1.0, static_cast<qreal>( row.m_nParticles ) / static_cast<qreal>( row.m_nLimit ) ) : 0.0;
		painter.setBrush( bFull ? QColor( 255, 150, 40 ) : QColor( 110, 190, 255 ) );
		painter.drawRect( QRectF{ static_cast<qreal>( bar.left() ), static_cast<qreal>( bar.top() ), bar.width() * flShare, static_cast<qreal>( bar.height() ) } );
		painter.setPen( bFull ? QColor( 255, 170, 70 ) : QColor( 200, 200, 200 ) );
		painter.drawText( QRect{ bar.right() + 6, y, 80, nRowHeight }, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral( "%1 / %2" ).arg( row.m_nParticles ).arg( row.m_nLimit ) );
	}
	if ( static_cast<int>( rows.size() ) > nShown )
	{
		painter.setPen( QColor( 200, 200, 200 ) );
		painter.drawText( QRect{ panel.left() + 6, panel.top() + 3 + nShown * nRowHeight, panel.width(), nRowHeight }, Qt::AlignVCenter,
			tr( "and %n more system(s)", nullptr, static_cast<int>( rows.size() ) - nShown ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The models the game has, for the Model menu.
//-----------------------------------------------------------------------------
void CParticlePreview::SetModelList( QStringList models )
{
	m_Models = std::move( models );
	m_Models.sort( Qt::CaseInsensitive );
}

//-----------------------------------------------------------------------------
// Purpose: Stands a model on a control point; an empty path takes it away.
//-----------------------------------------------------------------------------
bool CParticlePreview::SetModel( const QString &szPath, int nControlPoint, QString *pError )
{
	std::shared_ptr<const xv::model::CModel> pModel;
	if ( !szPath.isEmpty() )
	{
		std::string szError;
		auto loaded = m_pfnReader ? xv::model::LoadModel( szPath.toStdString(), m_pfnReader, &szError ) : std::nullopt;
		if ( !loaded )
		{
			if ( pError )
			{
				*pError = szError.empty() ? tr( "No game files to read the model from" ) : Qs( szError );
			}
			return false;
		}
		pModel = std::make_shared<const xv::model::CModel>( std::move( *loaded ) );
	}
	m_pModel = std::move( pModel );
	m_szModelPath = m_pModel ? szPath : QString{};
	if ( !m_pModel )
	{
		m_ModelAngles = {};
	}
	m_nModelPoint = std::clamp( nControlPoint, 0, particles::CSimulation::CONTROL_POINTS - 1 );
	m_Attached.clear();
	m_ModelSprites.clear();
#ifdef VOMIT_TOOLS_GPU_PREVIEW
	if ( auto *pView = qobject_cast<CParticleGpuView *>( m_pGpu ) )
	{
		pView->ForgetTextures();
	}
#endif
	m_pModelButton->setText( m_pModel ? tr( "Model: %1" ).arg( QFileInfo{ m_szModelPath }.completeBaseName() ) : tr( "Model" ) );
	Rebuild( false );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Puts a control point on one of the model's attachments.
//-----------------------------------------------------------------------------
void CParticlePreview::AttachControlPoint( int nIndex, int nAttachment )
{
	if ( !m_pModel || nIndex == m_nModelPoint || nIndex < 0 || nIndex >= particles::CSimulation::CONTROL_POINTS || nAttachment < 0 ||
		static_cast<std::size_t>( nAttachment ) >= m_pModel->m_Attachments.size() )
	{
		return;
	}
	const ControlPoints_t before = m_ControlPoints;
	m_Attached[ nIndex ] = nAttachment;
	FollowModel();
	if ( before != m_ControlPoints )
	{
		emit ControlPointsMoved( before, m_ControlPoints, tr( "Put CP%1 on %2" ).arg( nIndex ).arg( Qs( m_pModel->m_Attachments[ static_cast<std::size_t>( nAttachment ) ].m_szName ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Moves the points on attachments to where their attachments are now.
//-----------------------------------------------------------------------------
void CParticlePreview::FollowModel()
{
	if ( !m_pModel )
	{
		return;
	}
	const xv::model::CTransform toWorld = xv::model::CTransform::Translation( m_ControlPoints[ static_cast<std::size_t>( m_nModelPoint ) ] ) *
		xv::model::CTransform::Rotation( m_ModelAngles[ 0 ], m_ModelAngles[ 1 ], m_ModelAngles[ 2 ] );
	for ( const auto &[ nPoint, nAttachment ] : m_Attached )
	{
		const CVec3 vecAt = toWorld.Apply( m_pModel->AttachmentToModel( static_cast<std::size_t>( nAttachment ) ).GetOrigin() );
		m_ControlPoints[ static_cast<std::size_t>( nPoint ) ] = vecAt;
		if ( m_pSimulation )
		{
			m_pSimulation->SetControlPoint( nPoint, vecAt );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The model's turn; points on its attachments turn with it.
//-----------------------------------------------------------------------------
void CParticlePreview::SetModelAngles( const Angles_t &angles )
{
	m_ModelAngles = angles;
	if ( m_pSimulation )
	{
		m_pSimulation->SetModelAngles( angles );
	}
	FollowModel();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: The ring the model turns by: around its feet, a little wider than it.
//-----------------------------------------------------------------------------
std::pair<CVec3, float> CParticlePreview::ModelRing() const
{
	if ( !m_pModel )
	{
		return { {}, 0.0f };
	}
	const CVec3 vecSize = m_pModel->m_vecMax - m_pModel->m_vecMin;
	return { m_ControlPoints[ static_cast<std::size_t>( m_nModelPoint ) ], 0.6f * std::max( { vecSize.m_flX, vecSize.m_flY, 16.0f } ) + 6.0f };
}

//-----------------------------------------------------------------------------
// Purpose: The direction (degrees) from the model's feet to where a canvas position meets the
//          ground the model stands on.
//-----------------------------------------------------------------------------
std::optional<float> CParticlePreview::HeadingAt( QPointF at ) const
{
	if ( !m_pModel )
	{
		return std::nullopt;
	}
	const CVec3 vecCenter = ModelRing().first;
	const particles::CCamera camera = GetCanvasCamera();
	const CVec3 vecEye = camera.GetEye();
	const CVec3 vecRay = camera.Ray( static_cast<float>( at.x() ), static_cast<float>( at.y() ) );
	if ( std::abs( vecRay.m_flZ ) < 1e-4f )
	{
		return std::nullopt;
	}
	const float t = ( vecCenter.m_flZ - vecEye.m_flZ ) / vecRay.m_flZ;
	if ( t <= 0.0f )
	{
		return std::nullopt;
	}
	const CVec3 vecOn = vecEye + vecRay * t - vecCenter;
	return std::atan2( vecOn.m_flY, vecOn.m_flX ) * 180.0f / std::numbers::pi_v<float>;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a canvas position is on the ring (within a few pixels of it on screen).
//-----------------------------------------------------------------------------
bool CParticlePreview::IsOnModelRing( QPointF at ) const
{
	if ( !m_pModel || !m_pSimulation )
	{
		return false;
	}
	const auto [ vecCenter, flRadius ] = ModelRing();
	const particles::CCamera camera = GetCanvasCamera();
	constexpr int SEGMENTS = 64;
	std::optional<QPointF> previous;
	for ( int i = 0; i <= SEGMENTS; i++ )
	{
		const float flAngle = 2.0f * std::numbers::pi_v<float> * static_cast<float>( i ) / SEGMENTS;
		const auto p = camera.Project( vecCenter + CVec3{ std::cos( flAngle ), std::sin( flAngle ), 0.0f } * flRadius );
		const std::optional<QPointF> here = p ? std::optional{ QPointF{ ( *p )[ 0 ], ( *p )[ 1 ] } } : std::nullopt;
		if ( here && previous )
		{
			// Distance to this piece of the ring.
			const QPointF d = *here - *previous;
			const double flLength = QPointF::dotProduct( d, d );
			const double t = flLength > 0.0 ? std::clamp( QPointF::dotProduct( at - *previous, d ) / flLength, 0.0, 1.0 ) : 0.0;
			const QPointF closest = *previous + d * t;
			if ( std::hypot( closest.x() - at.x(), closest.y() - at.y() ) <= 5.0 )
			{
				return true;
			}
		}
		previous = here;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a canvas position is on the model (inside its box as seen on screen).
//-----------------------------------------------------------------------------
bool CParticlePreview::IsOnModel( QPointF at ) const
{
	if ( !m_pModel || !m_pSimulation )
	{
		return false;
	}
	const particles::CCamera camera = GetCanvasCamera();
	const xv::model::CTransform toWorld = m_pSimulation->ModelToWorld();
	std::optional<QRectF> area;
	for ( int i = 0; i < 8; i++ )
	{
		const CVec3 vecCorner = toWorld.Apply( { ( i & 1 ) ? m_pModel->m_vecMax.m_flX : m_pModel->m_vecMin.m_flX, ( i & 2 ) ? m_pModel->m_vecMax.m_flY : m_pModel->m_vecMin.m_flY,
			( i & 4 ) ? m_pModel->m_vecMax.m_flZ : m_pModel->m_vecMin.m_flZ } );
		if ( const auto p = camera.Project( vecCorner ) )
		{
			const QRectF corner{ QPointF{ ( *p )[ 0 ], ( *p )[ 1 ] }, QSizeF{ 0.01, 0.01 } };
			area = area ? area->united( corner ) : corner;
		}
	}
	return area && area->contains( at );
}

//-----------------------------------------------------------------------------
// Purpose: Turning by the ring: the yaw follows the mouse around the model.
//-----------------------------------------------------------------------------
void CParticlePreview::BeginTurn( float flHeading )
{
	m_bTurning = true;
	m_flTurnFrom = flHeading;
	m_TurnBefore = m_ModelAngles;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::TurnTo( float flHeading )
{
	if ( !m_bTurning )
	{
		return;
	}
	float flYaw = m_TurnBefore[ 1 ] + std::remainder( flHeading - m_flTurnFrom, 360.0f );
	if ( m_pGrid->isChecked() )
	{
		flYaw = std::round( flYaw / 15.0f ) * 15.0f;
	}
	flYaw = std::remainder( flYaw, 360.0f ) + 0.0f; // + 0 turns -0 into 0
	SetModelAngles( { m_TurnBefore[ 0 ], flYaw, m_TurnBefore[ 2 ] } );
}

//-----------------------------------------------------------------------------
// Purpose: Lets go of the ring: the turn is an undo step.
//-----------------------------------------------------------------------------
void CParticlePreview::EndTurn()
{
	if ( !m_bTurning )
	{
		return;
	}
	m_bTurning = false;
	if ( m_TurnBefore != m_ModelAngles )
	{
		emit ModelTurned( m_TurnBefore, m_ModelAngles );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The model's textures, read when first drawn. One that can't be read draws plain grey.
//-----------------------------------------------------------------------------
const particles::Sprite_t *CParticlePreview::ModelSprite( std::size_t nMaterial )
{
	if ( !m_pModel || nMaterial >= m_pModel->m_Materials.size() )
	{
		return nullptr;
	}
	auto &pSprite = m_ModelSprites[ nMaterial ];
	if ( !pSprite )
	{
		pSprite = std::make_shared<const particles::Sprite_t>( particles::LoadSprite( m_pModel->m_Materials[ nMaterial ], m_pfnReader ) );
	}
	return pSprite->m_szProblem.empty() ? pSprite.get() : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The Model menu: pick one of the game's models, the control point it stands on, and
//          control points to put on its attachments.
//-----------------------------------------------------------------------------
void CParticlePreview::FillModelMenu( QMenu *pMenu )
{
	ClearMenu( pMenu );
	QMenu *pChoose = pMenu->addMenu( m_pModel ? tr( "Other Model" ) : tr( "Choose Model" ) );
	std::vector<PickerItem_t> items;
	for ( const QString &szPath : std::as_const( m_Models ) )
	{
		items.push_back( { szPath, {}, [ this, szPath ] {
							  QString szError;
							  if ( !SetModel( szPath, m_nModelPoint, &szError ) )
							  {
								  QMessageBox::warning( this, tr( "Model" ), tr( "Could not read %1:\n\n%2" ).arg( szPath, szError ) );
							  }
						  } } );
	}
	FillPicker( pChoose, std::move( items ), tr( "No models: open a game" ) );
	if ( !m_pModel )
	{
		return;
	}
	QMenu *pPoint = pMenu->addMenu( tr( "Stands on" ) );
	for ( int i = 0; i < particles::CSimulation::CONTROL_POINTS; i++ )
	{
		QAction *pAction = pPoint->addAction( QStringLiteral( "CP%1" ).arg( i ), this, [ this, i ] { SetModel( m_szModelPath, i ); } );
		pAction->setCheckable( true );
		pAction->setChecked( i == m_nModelPoint );
	}
	QMenu *pAttach = pMenu->addMenu( tr( "Put a CP on an Attachment" ) );
	if ( m_pModel->m_Attachments.empty() )
	{
		pAttach->addAction( tr( "This model has no attachments" ) )->setEnabled( false );
	}
	for ( std::size_t a = 0; a < m_pModel->m_Attachments.size(); a++ )
	{
		QMenu *pTo = pAttach->addMenu( Qs( m_pModel->m_Attachments[ a ].m_szName ) );
		for ( int i = 0; i < particles::CSimulation::CONTROL_POINTS; i++ )
		{
			if ( i == m_nModelPoint )
			{
				continue;
			}
			const auto found = m_Attached.find( i );
			QAction *pAction = pTo->addAction( QStringLiteral( "CP%1" ).arg( i ), this, [ this, i, a ] { AttachControlPoint( i, static_cast<int>( a ) ); } );
			pAction->setCheckable( true );
			pAction->setChecked( found != m_Attached.end() && found->second == static_cast<int>( a ) );
		}
	}
	// Pitch, yaw and roll as numbers (the ring at its feet turns the yaw).
	QMenu *pTurn = pMenu->addMenu( tr( "Turn" ) );
	auto *pBox = new QWidget;
	auto *pForm = new QFormLayout( pBox );
	pForm->setContentsMargins( 8, 4, 8, 4 );
	const std::array<QString, 3> labels{ tr( "Pitch" ), tr( "Yaw" ), tr( "Roll" ) };
	for ( std::size_t i = 0; i < 3; i++ )
	{
		auto *pSpin = new QDoubleSpinBox;
		pSpin->setObjectName( QStringLiteral( "modelAngle%1" ).arg( i ) );
		pSpin->setRange( -360.0, 360.0 );
		pSpin->setSingleStep( 15.0 );
		pSpin->setDecimals( 1 );
		pSpin->setSuffix( QStringLiteral( "°" ) );
		pSpin->setWrapping( true );
		pSpin->setValue( static_cast<double>( m_ModelAngles[ i ] ) );
		pSpin->setKeyboardTracking( false );
		connect( pSpin, &QDoubleSpinBox::valueChanged, this, [ this, i ]( double flValue ) {
			Angles_t angles = m_ModelAngles;
			angles[ i ] = static_cast<float>( flValue );
			const Angles_t before = m_ModelAngles;
			SetModelAngles( angles );
			emit ModelTurned( before, m_ModelAngles );
		} );
		pForm->addRow( labels[ i ], pSpin );
	}
	auto *pTurnAction = new QWidgetAction( pTurn );
	pTurnAction->setDefaultWidget( pBox );
	pTurn->addAction( pTurnAction );
	pTurn->addAction( tr( "Face Forward" ), this, [ this ] {
		const Angles_t before = m_ModelAngles;
		SetModelAngles( {} );
		if ( before != m_ModelAngles )
		{
			emit ModelTurned( before, m_ModelAngles );
		}
	} );
	pMenu->addSeparator();
	pMenu->addAction( tr( "Remove Model" ), this, [ this ] { SetModel( {} ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Functions in the shown systems that the preview leaves out, and materials it couldn't
//          load.
//-----------------------------------------------------------------------------
QStringList CParticlePreview::GetNotes() const
{
	QStringList notes;
	if ( !m_pSimulation )
	{
		return notes;
	}
	std::set<std::string> functions;
	std::set<std::string> problems;
	std::set<int> modelPoints; // where Position on Model Random looks for a model
	ForEachInstance( m_pSimulation->GetRoot(), [ & ]( const particles::CSimulation::Instance_t &instance ) {
		for ( const particles::CCompiledOperator &op : instance.m_pDefinition->operators )
		{
			if ( !op.m_bSimulated )
			{
				functions.insert( op.m_szFunction );
			}
			if ( op.m_szFunction == "Position on Model Random" )
			{
				modelPoints.insert( op.Integer( "control_point_number" ) );
			}
		}
		const auto nIndex = static_cast<std::size_t>( instance.m_pDefinition - m_pLibrary->m_Definitions.data() );
		if ( nIndex < m_Sprites.size() && m_Sprites[ nIndex ] && !m_Sprites[ nIndex ]->m_szProblem.empty() )
		{
			problems.insert( m_Sprites[ nIndex ]->m_szProblem );
		}
	} );
	if ( !m_pModel || std::ranges::any_of( modelPoints, [ this ]( int n ) { return n != m_nModelPoint; } ) )
	{
		for ( int nPoint : modelPoints )
		{
			if ( !m_pModel || nPoint != m_nModelPoint )
			{
				notes << tr( "Position on Model Random needs a model on CP%1: choose one under Model" ).arg( nPoint );
				break;
			}
		}
	}
	if ( !functions.empty() )
	{
		QStringList names;
		for ( const std::string &f : functions )
		{
			names << Qs( f );
		}
		notes << tr( "Not in the preview yet: %1" ).arg( names.join( QStringLiteral( ", " ) ) );
	}
	for ( const std::string &szProblem : problems )
	{
		notes << Qs( szProblem );
	}
	return notes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticlePreview::UpdateInfo()
{
	if ( !m_pSimulation )
	{
		m_pInfo->clear();
		return;
	}
	// The timeline follows the effect, and grows (by whole seconds) to keep up with it.
	const int nNow = static_cast<int>( std::lround( m_pSimulation->GetTime() * 1000.0f ) );
	if ( nNow > m_pTimeline->maximum() && m_pTimeline->maximum() < TIMELINE_MOST )
	{
		m_pTimeline->setMaximum( std::min( TIMELINE_MOST, ( nNow / 1000 + 2 ) * 1000 ) );
	}
	if ( !m_pTimeline->isSliderDown() )
	{
		const QSignalBlocker blocker{ m_pTimeline };
		m_pTimeline->setValue( nNow );
	}
	QString szText = tr( "%1 s · %n particle(s)", nullptr, static_cast<int>( m_pSimulation->GetParticleCount() ) )
						 .arg( QString::number( static_cast<double>( m_pSimulation->GetTime() ), 'f', 2 ) );
	const QStringList notes = this->GetNotes();
	if ( !notes.isEmpty() )
	{
		const QString szGrey = palette().color( QPalette::PlaceholderText ).name();
		QStringList escaped;
		for ( const QString &szNote : notes )
		{
			escaped << szNote.toHtmlEscaped();
		}
		szText += QStringLiteral( "<br><span style=\"color: %1\">%2</span>" ).arg( szGrey, escaped.join( QStringLiteral( "<br>" ) ) );
	}
	if ( m_pInfo->text() != szText )
	{
		m_pInfo->setText( szText );
	}
}
