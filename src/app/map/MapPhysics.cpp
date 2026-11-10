//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapPhysics.h.
//
//===========================================================================//
#include "MapPhysics.h"

#include <algorithm>
#include <cctype>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "Preferences.h"
#include "map/MapConstants.h"
#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"
#include "vfs/Vfs.h"

using namespace xv::map;

namespace
{

// The timer's interval (about 60 a second), and the longest step a slow frame may take.
constexpr int TICK_MS = 16;
constexpr double LONGEST_FRAME = 0.1;
// More edges than this aren't drawn: a big map's world would cost every frame its time.
constexpr std::size_t MOST_EDGES = 200000;

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The panel, laid out as Hammer++'s Physics Simulation box.
//-----------------------------------------------------------------------------
CMapPhysicsTool::CMapPhysicsTool( CMapEditor &editor )
	: m_Editor( editor )
{
	setObjectName( QStringLiteral( "mapPhysics" ) );
	auto *pLayout = new QVBoxLayout( this );
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "physicsStatus" ) );
	m_pStatus->setWordWrap( true );
	pLayout->addWidget( m_pStatus );
	m_pToggle = new QPushButton;
	m_pToggle->setObjectName( QStringLiteral( "physicsToggle" ) );
	m_pToggle->setToolTip( tr( "Start the selected props and brushes falling, or stop and keep where they are (Space)" ) );
	connect( m_pToggle, &QPushButton::clicked, this, &CMapPhysicsTool::Toggle );
	pLayout->addWidget( m_pToggle );

	const auto spin = [ this ]( const char *pszName, double flMin, double flMax, int nDecimals ) {
		auto *pSpin = new QDoubleSpinBox;
		pSpin->setObjectName( QLatin1String( pszName ) );
		pSpin->setRange( flMin, flMax );
		pSpin->setDecimals( nDecimals );
		connect( pSpin, &QDoubleSpinBox::valueChanged, this, &CMapPhysicsTool::SettingsChanged );
		return pSpin;
	};
	const auto check = [ this ]( const char *pszName, const QString &szText, const QString &szTip ) {
		auto *pCheck = new QCheckBox( szText );
		pCheck->setObjectName( QLatin1String( pszName ) );
		pCheck->setToolTip( szTip );
		connect( pCheck, &QCheckBox::toggled, this, &CMapPhysicsTool::SettingsChanged );
		return pCheck;
	};

	auto *pMouse = new QGroupBox( tr( "Mouse" ) );
	auto *pMouseForm = new QFormLayout( pMouse );
	m_pDrag = spin( "physicsDrag", 0.1, 100, 1 );
	m_pDrag->setToolTip( tr( "How hard the left button pulls what it holds" ) );
	pMouseForm->addRow( tr( "Drag magnitude" ), m_pDrag );
	m_pBullet = spin( "physicsBullet", 0, 1000000, 0 );
	m_pBullet->setToolTip( tr( "How hard a right click shoots what it hits" ) );
	pMouseForm->addRow( tr( "Bullet magnitude" ), m_pBullet );
	m_pExplode = spin( "physicsExplode", 0, 1000000, 0 );
	m_pExplode->setToolTip( tr( "How hard a middle click's explosion pushes what is near it" ) );
	pMouseForm->addRow( tr( "Explode magnitude" ), m_pExplode );
	pLayout->addWidget( pMouse );

	auto *pEnvironment = new QGroupBox( tr( "Environment" ) );
	auto *pEnvironmentForm = new QFormLayout( pEnvironment );
	auto *pGravityRow = new QHBoxLayout;
	const char *GRAVITY_NAMES[] = { "physicsGravityX", "physicsGravityY", "physicsGravityZ" };
	for ( std::size_t i = 0; i < 3; i++ )
	{
		m_pGravity[ i ] = spin( GRAVITY_NAMES[ i ], -100000, 100000, 0 );
		pGravityRow->addWidget( m_pGravity[ i ] );
	}
	pEnvironmentForm->addRow( tr( "Gravity (X Y Z)" ), pGravityRow );
	m_pAirDensity = spin( "physicsAirDensity", 0, 100, 2 );
	m_pAirDensity->setToolTip( tr( "How much the air slows what moves through it" ) );
	pEnvironmentForm->addRow( tr( "Air density" ), m_pAirDensity );
	m_pTimeScale = spin( "physicsTimeScale", 0, 10, 2 );
	m_pTimeScale->setToolTip( tr( "Simulated seconds a second: below 1 slow motion" ) );
	pEnvironmentForm->addRow( tr( "Time scale" ), m_pTimeScale );
	auto *pReset = new QPushButton( tr( "Reset" ) );
	pReset->setObjectName( QStringLiteral( "physicsReset" ) );
	pReset->setToolTip( tr( "The environment back to its defaults" ) );
	connect( pReset, &QPushButton::clicked, this, [ this ] {
		const MapPreferences_t defaults;
		m_bPanelUpdating = true;
		m_pGravity[ 0 ]->setValue( defaults.m_flPhysicsGravityX );
		m_pGravity[ 1 ]->setValue( defaults.m_flPhysicsGravityY );
		m_pGravity[ 2 ]->setValue( defaults.m_flPhysicsGravityZ );
		m_pAirDensity->setValue( defaults.m_flPhysicsAirDensity );
		m_pTimeScale->setValue( defaults.m_flPhysicsTimeScale );
		m_bPanelUpdating = false;
		SettingsChanged();
	} );
	pEnvironmentForm->addRow( QString{}, pReset );
	pLayout->addWidget( pEnvironment );

	m_pSleep = check( "physicsSleep", tr( "Sleep objects until touched" ), tr( "What moves starts at rest, until something hits it (or a shot, or the mouse)" ) );
	m_pAutoMass = check( "physicsAutoMass", tr( "Autocalculate mass for entities" ), tr( "A prop's mass from its collision model ($mass), else from its size" ) );
	m_pPropsOnly = check( "physicsPropsOnly", tr( "Simulate props only" ), tr( "Of the selection, only props move; brushes stay where they are" ) );
	pLayout->addWidget( m_pSleep );
	pLayout->addWidget( m_pAutoMass );
	pLayout->addWidget( m_pPropsOnly );

	auto *pDebug = new QGroupBox( tr( "Debug" ) );
	auto *pDebugLayout = new QVBoxLayout( pDebug );
	m_pDrawMoving = check( "physicsDrawMoving", tr( "Draw dynamic collision models" ), tr( "The moving objects' collision shapes as lines over the views" ) );
	m_pDrawStatic = check( "physicsDrawStatic", tr( "Draw static collision models" ), tr( "What they hit, as lines over the views" ) );
	pDebugLayout->addWidget( m_pDrawMoving );
	pDebugLayout->addWidget( m_pDrawStatic );
	pLayout->addWidget( pDebug );
	pLayout->addStretch();

	m_pTimer = new QTimer( this );
	m_pTimer->setInterval( TICK_MS );
	m_pTimer->setTimerType( Qt::PreciseTimer );
	connect( m_pTimer, &QTimer::timeout, this, &CMapPhysicsTool::Tick );
	// Anything else editing the map (an undo, say) while it runs: what it moved goes back.
	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, [ this ] {
		if ( IsRunning() && !m_bPreviewing )
		{
			m_pScene.reset();
			m_pBase.reset();
			m_pTimer->stop();
			UpdateStatus();
		}
	} );
	WritePanel();
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapPhysicsTool::~CMapPhysicsTool() = default;

//-----------------------------------------------------------------------------
// Purpose: The saved settings onto the panel.
//-----------------------------------------------------------------------------
void CMapPhysicsTool::WritePanel()
{
	const MapPreferences_t &p = CPreferences::Instance().GetMapPreferences();
	m_bPanelUpdating = true;
	m_pDrag->setValue( p.m_flPhysicsDrag );
	m_pBullet->setValue( p.m_flPhysicsBullet );
	m_pExplode->setValue( p.m_flPhysicsExplode );
	m_pGravity[ 0 ]->setValue( p.m_flPhysicsGravityX );
	m_pGravity[ 1 ]->setValue( p.m_flPhysicsGravityY );
	m_pGravity[ 2 ]->setValue( p.m_flPhysicsGravityZ );
	m_pAirDensity->setValue( p.m_flPhysicsAirDensity );
	m_pTimeScale->setValue( p.m_flPhysicsTimeScale );
	m_pSleep->setChecked( p.m_bPhysicsSleep );
	m_pAutoMass->setChecked( p.m_bPhysicsAutoMass );
	m_pPropsOnly->setChecked( p.m_bPhysicsPropsOnly );
	m_pDrawMoving->setChecked( p.m_bPhysicsDrawMoving );
	m_pDrawStatic->setChecked( p.m_bPhysicsDrawStatic );
	m_bPanelUpdating = false;
}

//-----------------------------------------------------------------------------
// Purpose: The panel kept in vomit-tools.nut, and a running simulation told.
//-----------------------------------------------------------------------------
void CMapPhysicsTool::SettingsChanged()
{
	if ( m_bPanelUpdating )
	{
		return;
	}
	MapPreferences_t p = CPreferences::Instance().GetMapPreferences();
	p.m_flPhysicsDrag = m_pDrag->value();
	p.m_flPhysicsBullet = m_pBullet->value();
	p.m_flPhysicsExplode = m_pExplode->value();
	p.m_flPhysicsGravityX = m_pGravity[ 0 ]->value();
	p.m_flPhysicsGravityY = m_pGravity[ 1 ]->value();
	p.m_flPhysicsGravityZ = m_pGravity[ 2 ]->value();
	p.m_flPhysicsAirDensity = m_pAirDensity->value();
	p.m_flPhysicsTimeScale = m_pTimeScale->value();
	p.m_bPhysicsSleep = m_pSleep->isChecked();
	p.m_bPhysicsAutoMass = m_pAutoMass->isChecked();
	p.m_bPhysicsPropsOnly = m_pPropsOnly->isChecked();
	p.m_bPhysicsDrawMoving = m_pDrawMoving->isChecked();
	p.m_bPhysicsDrawStatic = m_pDrawStatic->isChecked();
	CPreferences::Instance().SetMapPreferences( p );
	if ( m_pScene )
	{
		m_pScene->SetSettings( GetSettings() );
	}
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PhysicsSettings_t CMapPhysicsTool::GetSettings() const
{
	PhysicsSettings_t settings;
	settings.m_vecGravity = { m_pGravity[ 0 ]->value(), m_pGravity[ 1 ]->value(), m_pGravity[ 2 ]->value() };
	settings.m_flAirDensity = m_pAirDensity->value();
	settings.m_flTimeScale = m_pTimeScale->value();
	settings.m_bSleepUntilTouched = m_pSleep->isChecked();
	settings.m_bAutoMass = m_pAutoMass->isChecked();
	settings.m_bPropsOnly = m_pPropsOnly->isChecked();
	return settings;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapPhysicsTool::UpdateStatus()
{
	if ( m_pScene )
	{
		m_pToggle->setText( tr( "Stop Simulation (Space)" ) );
		QString szText = tr( "Simulation is running: %n object(s) moving.", nullptr, static_cast<int>( m_pScene->GetObjects().size() ) );
		if ( !m_pScene->GetProblems().empty() )
		{
			szText += QLatin1Char( ' ' ) + QString::fromStdString( m_pScene->GetProblems().front() ) + QLatin1Char( '.' );
		}
		m_pStatus->setText( szText );
		return;
	}
	m_pToggle->setText( tr( "Start Simulation (Space)" ) );
	m_pStatus->setText( m_szProblem.empty() ? tr( "Select props or brushes, then press Space." ) : QString::fromStdString( m_szProblem ) );
}

//-----------------------------------------------------------------------------
// Purpose: Leaving the tool stops a simulation and keeps where things are, as Space does.
//-----------------------------------------------------------------------------
void CMapPhysicsTool::SetActive( bool bActive )
{
	if ( bActive == m_bActive )
	{
		return;
	}
	m_bActive = bActive;
	if ( !bActive && IsRunning() )
	{
		Stop( true );
	}
	if ( bActive )
	{
		emit m_Editor.StatusMessage( tr( "Physics: select what should fall, then Space. Space again keeps where it lands; Escape puts it back." ) );
	}
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose: A model's collision model, read from the game the first time it is asked for.
//-----------------------------------------------------------------------------
std::shared_ptr<const PropCollision_t> CMapPhysicsTool::CollisionOf( std::string_view szModel )
{
	std::string szKey{ szModel };
	std::ranges::transform( szKey, szKey.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	std::ranges::replace( szKey, '\\', '/' );
	if ( const auto found = m_Collisions.find( szKey ); found != m_Collisions.end() )
	{
		return found->second;
	}
	const auto &game = m_Editor.GetDocument().GetGame();
	if ( !game )
	{
		return nullptr;
	}
	if ( !m_pFileSystem )
	{
		m_pFileSystem = std::make_unique<xv::vfs::CFileSystem>();
		m_pFileSystem->Mount( *game );
	}
	const xv::model::Reader_t read = [ this ]( std::string_view szPath ) { return m_pFileSystem->Read( szPath ); };
	auto pCollision = ReadPropCollision( szKey, read );
	m_Collisions.emplace( szKey, pCollision );
	return pCollision;
}

//-----------------------------------------------------------------------------
// Purpose: The simulation built on the selection, the map as it is now its start.
//-----------------------------------------------------------------------------
bool CMapPhysicsTool::Start()
{
	if ( IsRunning() )
	{
		return true;
	}
	CMapDocument &document = m_Editor.GetDocument();
	const std::shared_ptr<const CScene> pScene = document.GetSharedScene();
	const std::vector<std::uint32_t> selection = document.GetSelection();
	m_szProblem.clear();
	if ( !pScene || selection.empty() )
	{
		m_szProblem = tr( "Select props or brushes to simulate first." ).toStdString();
		UpdateStatus();
		return false;
	}
	auto pPhysics = std::make_unique<CPhysicsScene>( *pScene, selection, [ this ]( std::string_view szModel ) { return CollisionOf( szModel ); }, GetSettings() );
	if ( pPhysics->GetObjects().empty() )
	{
		m_szProblem = pPhysics->GetProblems().empty() ? tr( "Nothing selected can move: select props (with collision models) or brushes." ).toStdString() : pPhysics->GetProblems().front() + ".";
		UpdateStatus();
		return false;
	}
	m_pScene = std::move( pPhysics );
	m_pBase = pScene;
	m_Clock.start();
	m_pTimer->start();
	UpdateStatus();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The objects kept where they are (one undo step) or put back.
//-----------------------------------------------------------------------------
void CMapPhysicsTool::Stop( bool bKeep )
{
	if ( !IsRunning() )
	{
		return;
	}
	m_pTimer->stop();
	CMapDocument &document = m_Editor.GetDocument();
	MapEdit_t edit = bKeep ? m_pScene->MakeEdit( *m_pBase ) : MapEdit_t{};
	const std::vector<std::uint32_t> selection = document.GetSelection();
	m_pScene.reset();
	m_pBase.reset();
	m_bPreviewing = true;
	document.EndPreview();
	m_bPreviewing = false;
	if ( !edit.IsEmpty() )
	{
		document.Edit( std::move( edit ), tr( "Physics Simulation" ), selection );
	}
	UpdateStatus();
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapPhysicsTool::Toggle()
{
	if ( IsRunning() )
	{
		Stop( true );
	}
	else
	{
		Start();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The time since the last tick (a stall counts as a short one), then the views.
//-----------------------------------------------------------------------------
void CMapPhysicsTool::Tick()
{
	if ( !m_pScene )
	{
		return;
	}
	const double flSeconds = std::min( static_cast<double>( m_Clock.restart() ) / 1000.0, LONGEST_FRAME );
	m_pScene->Step( flSeconds );
	Preview();
}

//-----------------------------------------------------------------------------
// Purpose: For tests and slow machines: simulated seconds in one go (in the scene's own steps).
//-----------------------------------------------------------------------------
void CMapPhysicsTool::Advance( double flSeconds )
{
	if ( !m_pScene )
	{
		return;
	}
	const double flScale = std::max( GetSettings().m_flTimeScale, 1e-6 );
	for ( double flLeft = flSeconds; flLeft > 1e-9; flLeft -= 1.0 / 60.0 )
	{
		m_pScene->Step( std::min( flLeft, 1.0 / 60.0 ) / flScale );
	}
	Preview();
}

//-----------------------------------------------------------------------------
// Purpose: The objects drawn where the simulation has them (the map unchanged until it stops).
//-----------------------------------------------------------------------------
void CMapPhysicsTool::Preview()
{
	if ( !m_pScene || !m_pBase )
	{
		return;
	}
	m_bPreviewing = true;
	m_Editor.GetDocument().Preview( m_pScene->MakeEdit( *m_pBase ) );
	m_bPreviewing = false;
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapPhysicsTool::Refresh()
{
	for ( CMapView *pView : m_Editor.GetViews() )
	{
		if ( auto *p3D = qobject_cast<CMapView3D *>( pView ) )
		{
			p3D->Redraw();
			p3D->RedrawLabels();
		}
		pView->update();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapPhysicsTool::WantsKey( const QKeyEvent &key ) const
{
	if ( !m_bActive || key.modifiers() != Qt::NoModifier )
	{
		return false;
	}
	return key.key() == Qt::Key_Space || ( IsRunning() && key.key() == Qt::Key_Escape );
}

//-----------------------------------------------------------------------------
// Purpose: Space starts and stops, Escape cancels; while it runs, in a 3D view, the left button
//          drags what it holds, the right shoots, the middle explodes.
//-----------------------------------------------------------------------------
bool CMapPhysicsTool::HandleEvent( CMapView &view, QEvent *pEvent )
{
	if ( pEvent->type() == QEvent::KeyPress )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		if ( pKey->modifiers() != Qt::NoModifier || pKey->isAutoRepeat() )
		{
			return pKey->key() == Qt::Key_Space && pKey->isAutoRepeat();
		}
		if ( pKey->key() == Qt::Key_Space )
		{
			Toggle();
			return true;
		}
		if ( pKey->key() == Qt::Key_Escape && IsRunning() )
		{
			Stop( false );
			return true;
		}
		return false;
	}
	if ( pEvent->type() == QEvent::KeyRelease )
	{
		return static_cast<QKeyEvent *>( pEvent )->key() == Qt::Key_Space;
	}
	auto *p3D = qobject_cast<CMapView3D *>( &view );
	if ( !IsRunning() || !p3D )
	{
		return false;
	}
	if ( pEvent->type() != QEvent::MouseButtonPress && pEvent->type() != QEvent::MouseMove && pEvent->type() != QEvent::MouseButtonRelease )
	{
		return false;
	}
	auto *pMouse = static_cast<QMouseEvent *>( pEvent );
	const auto [ from, dir ] = p3D->GetSizedCamera().Ray( pMouse->position().x(), pMouse->position().y() );
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	if ( pEvent->type() == QEvent::MouseMove )
	{
		if ( !m_pScene->IsGrabbing() )
		{
			return false;
		}
		m_pScene->DragTo( Add( from, Scale( dir, m_flGrabDepth ) ), preferences.m_flPhysicsDrag );
		return true;
	}
	if ( pEvent->type() == QEvent::MouseButtonRelease )
	{
		if ( pMouse->button() == Qt::LeftButton && m_pScene->IsGrabbing() )
		{
			m_pScene->Release();
			return true;
		}
		return pMouse->button() == Qt::RightButton || pMouse->button() == Qt::MiddleButton;
	}
	switch ( pMouse->button() )
	{
		case Qt::LeftButton:
			if ( const auto hit = m_pScene->CastRay( from, dir, p3D->GetSizedCamera().m_flFar ) )
			{
				m_flGrabDepth = hit->m_flDistance;
				m_pScene->Grab( hit->m_nBody, hit->m_Point );
				m_pScene->DragTo( hit->m_Point, preferences.m_flPhysicsDrag );
				return true;
			}
			return false;
		case Qt::RightButton: m_pScene->Shoot( from, dir, preferences.m_flPhysicsBullet ); return true;
		case Qt::MiddleButton:
			if ( const auto surface = p3D->SurfaceAt( pMouse->position() ) )
			{
				m_pScene->Explode( surface->first, preferences.m_flPhysicsExplode );
			}
			return true;
		default: return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The collision models as lines, when the debug options ask for them.
//-----------------------------------------------------------------------------
void CMapPhysicsTool::Paint( CMapView &view, QPainter &painter ) const
{
	if ( !m_pScene )
	{
		return;
	}
	const auto draw = [ & ]( const std::vector<std::pair<Point_t, Point_t>> &edges, const QColor &colour ) {
		if ( edges.size() > MOST_EDGES )
		{
			return;
		}
		painter.setPen( QPen( colour, 1.0 ) );
		auto *p3D = qobject_cast<CMapView3D *>( &view );
		auto *p2D = qobject_cast<CMapView2D *>( &view );
		const CViewCamera camera = p3D ? p3D->GetSizedCamera() : CViewCamera{};
		for ( const auto &[ a, b ] : edges )
		{
			if ( p3D )
			{
				if ( const auto projected = camera.ProjectLine( a, b ) )
				{
					painter.drawLine( QPointF( projected->first[ 0 ], projected->first[ 1 ] ), QPointF( projected->second[ 0 ], projected->second[ 1 ] ) );
				}
			}
			else if ( p2D )
			{
				painter.drawLine( p2D->ToWidget( p2D->Flatten( a ) ), p2D->ToWidget( p2D->Flatten( b ) ) );
			}
		}
	};
	painter.save();
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	if ( preferences.m_bPhysicsDrawStatic )
	{
		draw( m_pScene->GetEdges( false ), mapstyle::PHYSICS_STATIC_EDGES );
	}
	if ( preferences.m_bPhysicsDrawMoving )
	{
		draw( m_pScene->GetEdges( true ), mapstyle::PHYSICS_MOVING_EDGES );
	}
	painter.restore();
}
