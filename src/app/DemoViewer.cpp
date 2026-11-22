//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoViewer.h.
//
//===========================================================================//
#include "DemoViewer.h"
#include "util/Parallel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <unordered_map>

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QDialog>
#include <QSlider>
#include <QWidgetAction>
#include <QSpinBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QShortcut>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "DemoGraphs.h"
#include "DemoHud.h"
#include "DemoVoice.h"
#include "Preferences.h"
#include "DemoWorld.h"
#include "ImageFiles.h"
#include "kv1/Kv1.h"
#include "map/Materials.h"
#include "texture/Vtf.h"
#include "util/Numbers.h"
#include "vfs/Vfs.h"

namespace
{

constexpr int TIMELINE_TALL = 72;
constexpr int PATH_MARGIN = 24;
// How often the heat is drawn again while the view moves, in milliseconds.
constexpr int HEAT_INTERVAL = 50;
// The isometric view's slant above the horizon: the angle that shows the three axes alike.
constexpr double ISOMETRIC_PITCH = 35.264;
// The isometric view cuts the map this far above the highest place a player stood, so roofs
// don't hide the paths.
constexpr float ISOMETRIC_CUT = 128.0f;
// The floors are drawn again this long after the view last changed (milliseconds).
constexpr int FLOOR_DELAY = 120;
// Milliseconds between the floors drawn again while a cut is being changed.
constexpr int CUT_INTERVAL = 40;
constexpr int PATH_INTERVAL = 60;
constexpr int CUT_LIMIT = 65536;
const std::array<QColor, 3> CUT_COLORS{ QColor( 230, 106, 96 ), QColor( 117, 201, 124 ), QColor( 108, 163, 246 ) };
// Each wheel step zooms by this much.
constexpr double ZOOM_STEP = 1.2;
// Degrees the isometric view turns for each pixel dragged.
constexpr double TURN_SPEED = 0.4;
// Two camera places further apart than this between packets are a respawn or a teleport,
// not a move: no line joins them.
constexpr float PATH_JUMP = 512.0f;
// The tick length played at when the header gives none (a demo cut off while recording):
// the usual 66.67 ticks a second.
constexpr double FALLBACK_TICK_INTERVAL = 0.015;
constexpr int COMMAND_COLUMN_TIME = 0;
constexpr int COMMAND_COLUMN_TICK = 1;
constexpr int COMMAND_COLUMN_TEXT = 2;
constexpr int PLAYER_COLUMN_NAME = 0;
constexpr int PLAYER_COLUMN_TEAM = 1;
constexpr int PLAYER_COLUMN_HEALTH = 2;
constexpr int PLAYER_COLUMN_ARMOR = 3;
constexpr int PLAYER_COLUMN_MONEY = 4;
constexpr int PLAYER_COLUMN_SCORE = 5;
constexpr int PLAYER_COLUMN_DEATHS = 6;
constexpr int PLAYER_COLUMN_PING = 7;
constexpr int PLAYER_COLUMN_STEAM_ID = 8;
constexpr int PLAYER_COLUMN_CONNECTION = 9;
constexpr int PLAYER_COLUMN_COUNT = 10;
// Counter-Strike: Source's ammunition types (a weapon's m_iPrimaryAmmoType) and the HUD's
// icon for each, from 1.
constexpr const char *AMMO_ICONS[] = { "", "ammo_50ae", "ammo_762", "ammo_556", "ammo_556", "ammo_338", "ammo_9mm", "ammo_buckshot", "ammo_45acp", "ammo_357sig", "ammo_57mm" };
// A playing demo's player list is brought up to date this often (milliseconds).
constexpr qint64 LIST_INTERVAL = 250;

//-----------------------------------------------------------------------------
// Purpose: A value label whose text can be selected and copied. bWrap for text of any
//          length (a server's name, a warning), which then takes the lines it needs.
//-----------------------------------------------------------------------------
QLabel *ValueLabel( const QString &szText, bool bWrap = false )
{
	auto *pLabel = new QLabel( szText );
	pLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pLabel->setWordWrap( bWrap );
	return pLabel;
}

//-----------------------------------------------------------------------------
// Purpose: A read-only table with whole rows picked and the last column taking the room left.
//-----------------------------------------------------------------------------
QTableWidget *ListTable( const QStringList &headers )
{
	auto *pTable = new QTableWidget( 0, static_cast<int>( headers.size() ) );
	pTable->setHorizontalHeaderLabels( headers );
	pTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
	pTable->setSelectionBehavior( QAbstractItemView::SelectRows );
	pTable->setSelectionMode( QAbstractItemView::SingleSelection );
	pTable->verticalHeader()->hide();
	pTable->horizontalHeader()->setStretchLastSection( true );
	return pTable;
}

//-----------------------------------------------------------------------------
// Purpose: A player's Steam id in each of its forms, for a tooltip; empty for bots, SourceTV
//          and nobody.
//-----------------------------------------------------------------------------
QString SteamIdTip( const xv::demo::PlayerIdentity_t *pIdentity )
{
	const auto nId64 = pIdentity ? xv::demo::SteamId64( *pIdentity ) : std::nullopt;
	if ( !nId64 )
	{
		return {};
	}
	return QStringLiteral( "%1\n%2\n%3\n%4" ).arg( QString::fromStdString( xv::demo::SteamId2( *nId64 ) ), QString::fromStdString( xv::demo::SteamId3( *nId64 ) ), QString::number( *nId64 ), QString::fromStdString( xv::demo::SteamProfileUrl( *nId64 ) ) );
}

// The player list's roles: the track a row is (on its name), and what a cell sorts by when
// not its text (numbers as numbers, the dead and the empty last).
constexpr int ROLE_TRACK = Qt::UserRole + 1;
constexpr int ROLE_SORT = Qt::UserRole + 2;

// A cell that sorts by its ROLE_SORT value when it has one.
class CSortItem : public QTableWidgetItem
{
public:
	bool operator<( const QTableWidgetItem &other ) const override
	{
		const QVariant mine = data( ROLE_SORT );
		const QVariant theirs = other.data( ROLE_SORT );
		if ( mine.isValid() && theirs.isValid() )
		{
			return QVariant::compare( mine, theirs ) == QPartialOrdering::Less;
		}
		if ( mine.isValid() != theirs.isValid() )
		{
			return mine.isValid();
		}
		return text().localeAwareCompare( other.text() ) < 0;
	}
};

//-----------------------------------------------------------------------------
// Purpose: A small speaker, for who is talking.
//-----------------------------------------------------------------------------
QIcon SpeakerIcon( bool bMuted, bool bTalking )
{
	const auto make = []( const QColor &color, bool bMuted ) {
		QPixmap pixmap( 16, 16 );
		pixmap.fill( Qt::transparent );
		QPainter painter( &pixmap );
		painter.setRenderHint( QPainter::Antialiasing, true );
		painter.setPen( Qt::NoPen );
		painter.setBrush( color );
		painter.drawPolygon( QPolygonF( { QPointF( 2, 6 ), QPointF( 5, 6 ), QPointF( 9, 2 ), QPointF( 9, 14 ), QPointF( 5, 10 ), QPointF( 2, 10 ) } ) );
		painter.setPen( QPen( color, 1.5 ) );
		painter.setBrush( Qt::NoBrush );
		if ( bMuted )
		{
			painter.drawLine( QPointF( 10.5, 6 ), QPointF( 14.5, 10 ) );
			painter.drawLine( QPointF( 14.5, 6 ), QPointF( 10.5, 10 ) );
		}
		else
		{
			painter.drawArc( QRectF( 7, 4, 6, 8 ), -60 * 16, 120 * 16 );
			painter.drawArc( QRectF( 7, 1.5, 9, 13 ), -60 * 16, 120 * 16 );
		}
		return QIcon( pixmap );
	};
	static const QIcon s_Icon = make( QColor( 90, 190, 90 ), false );
	static const QIcon s_MutedTalking = make( QColor( 220, 100, 100 ), true );
	static const QIcon s_Muted = make( QColor( 140, 140, 140 ), true );
	return !bMuted ? s_Icon : bTalking ? s_MutedTalking
									   : s_Muted;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoTimeline::CDemoTimeline( CDemoViewer &viewer, QWidget *pParent )
	: QWidget( pParent ),
	  m_Viewer( viewer )
{
	setObjectName( QStringLiteral( "demoTimeline" ) );
	setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
	setCursor( Qt::PointingHandCursor );
	setToolTip( tr( "How much the server sent over the demo's length. Click or drag to move through it; "
					"the marks along the top are console commands" ) );
	connect( &viewer, &CDemoViewer::TickChanged, this, qOverload<>( &QWidget::update ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QSize CDemoTimeline::sizeHint() const
{
	return { 400, TIMELINE_TALL };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemoTimeline::TickAt( int x ) const
{
	const int nLast = m_Viewer.GetDemo().GetLastTick();
	if ( width() <= 1 || nLast <= 0 )
	{
		return 0;
	}
	return static_cast<int>( std::clamp<qint64>( static_cast<qint64>( x ) * nLast / ( width() - 1 ), 0, nLast ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemoTimeline::XAt( int nTick ) const
{
	const int nLast = m_Viewer.GetDemo().GetLastTick();
	if ( nLast <= 0 )
	{
		return 0;
	}
	return static_cast<int>( static_cast<qint64>( nTick ) * ( width() - 1 ) / nLast );
}

//-----------------------------------------------------------------------------
// Purpose: The bars are counted once for each width, not at every repaint.
//-----------------------------------------------------------------------------
void CDemoTimeline::resizeEvent( QResizeEvent *pEvent )
{
	QWidget::resizeEvent( pEvent );
	m_Bars = m_Viewer.GetDemo().PacketBytesOverTime( width() );
}

//-----------------------------------------------------------------------------
// Purpose: Bars up from the bottom, as tall as their share of the busiest one; what has
//          played is in the highlight colour.
//-----------------------------------------------------------------------------
void CDemoTimeline::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	painter.fillRect( rect(), palette().color( QPalette::Base ) );
	const int nPlayhead = XAt( m_Viewer.GetTick() );
	const std::uint64_t nMost = m_Bars.empty() ? 0 : *std::max_element( m_Bars.begin(), m_Bars.end() );
	if ( nMost > 0 )
	{
		QColor played = palette().color( QPalette::Highlight );
		QColor ahead = palette().color( QPalette::Text );
		ahead.setAlphaF( 0.35f );
		const int nRoom = height() - 8;
		for ( int x = 0; x < static_cast<int>( m_Bars.size() ); x++ )
		{
			const int nTall = static_cast<int>( m_Bars[ static_cast<std::size_t>( x ) ] * static_cast<std::uint64_t>( nRoom ) / nMost );
			if ( nTall > 0 )
			{
				painter.fillRect( x, height() - nTall, 1, nTall, x <= nPlayhead ? played : ahead );
			}
		}
	}
	else
	{
		painter.setPen( palette().color( QPalette::PlaceholderText ) );
		painter.drawText( rect(), Qt::AlignCenter, tr( "No packets were read" ) );
	}
	painter.setPen( palette().color( QPalette::Link ) );
	for ( const xv::demo::Command_t &command : m_Viewer.GetDemo().GetCommands() )
	{
		const int x = XAt( command.m_nTick );
		painter.drawLine( x, 0, x, 5 );
	}
	// The rounds' starts across the whole height, deaths in red and chat in green under
	// the commands' marks.
	const xv::demo::CFeed &feed = m_Viewer.GetFeed();
	painter.setPen( QPen( QColor( 230, 160, 40, 200 ), 1, Qt::DashLine ) );
	for ( const xv::demo::Round_t &round : feed.GetRounds() )
	{
		if ( round.m_nStart > 0 )
			painter.drawLine( XAt( round.m_nStart ), 0, XAt( round.m_nStart ), height() );
	}
	painter.setPen( QColor( 220, 50, 50 ) );
	for ( const xv::demo::Kill_t &kill : feed.GetKills() )
		painter.drawLine( XAt( kill.m_nTick ), 7, XAt( kill.m_nTick ), 12 );
	for ( const xv::demo::LogicGuess_t &guess : m_Viewer.GetLogicGuesses() )
	{
		painter.setPen( guess.IsLikely() ? QColor( 190, 110, 255 ) : QColor( 190, 110, 255, 90 ) );
		painter.drawLine( XAt( guess.m_nTick ), 20, XAt( guess.m_nTick ), 25 );
	}
	painter.setPen( QColor( 90, 190, 90 ) );
	for ( const xv::demo::FeedLine_t &line : feed.GetLines() )
	{
		if ( line.m_nKind == xv::demo::FeedKind_t::FEED_KIND_CHAT && line.m_nEntity != 0 )
			painter.drawLine( XAt( line.m_nTick ), 14, XAt( line.m_nTick ), 18 );
	}
	painter.setPen( QPen( palette().color( QPalette::Text ), 1 ) );
	painter.drawLine( nPlayhead, 0, nPlayhead, height() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoTimeline::mousePressEvent( QMouseEvent *pEvent )
{
	if ( pEvent->button() == Qt::LeftButton )
	{
		m_Viewer.SetTick( TickAt( pEvent->position().toPoint().x() ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoTimeline::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( pEvent->buttons() & Qt::LeftButton )
	{
		m_Viewer.SetTick( TickAt( pEvent->position().toPoint().x() ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoPathView::CDemoPathView( CDemoViewer &viewer, QWidget *pParent )
	: QWidget( pParent ),
	  m_Viewer( viewer )
{
	setObjectName( QStringLiteral( "demoPath" ) );
	setMinimumSize( 200, 160 );
	setToolTip( tr( "Drag to move, the wheel to zoom, double click to see it all again; in the isometric view, drag with the right button to turn it" ) );
	m_nMode = viewer.GetDemo().HasCamera() || viewer.GetPlayers().IsEmpty() ? DemoPathMode_t::DEMO_PATH_CAMERA : DemoPathMode_t::DEMO_PATH_PLAYERS;
	FitBounds();
	m_pFloorTimer = new QTimer( this );
	m_pFloorTimer->setSingleShot( true );
	m_pFloorTimer->setInterval( FLOOR_DELAY );
	connect( m_pFloorTimer, &QTimer::timeout, this, [ this ] {
		BuildFloor();
		update();
	} );
	m_pCutTimer = new QTimer( this );
	m_pCutTimer->setSingleShot( true );
	m_pCutTimer->setInterval( CUT_INTERVAL );
	connect( m_pCutTimer, &QTimer::timeout, this, [ this ] {
		BuildFloor();
		update();
	} );
	m_pPathTimer = new QTimer( this );
	m_pPathTimer->setSingleShot( true );
	m_pPathTimer->setInterval( PATH_INTERVAL );
	connect( m_pPathTimer, &QTimer::timeout, this, &CDemoPathView::InvalidatePaths );
	m_pHeatTimer = new QTimer( this );
	m_pHeatTimer->setSingleShot( true );
	m_pHeatTimer->setInterval( HEAT_INTERVAL );
	connect( m_pHeatTimer, &QTimer::timeout, this, [ this ] {
		BuildHeat();
		update();
	} );
	connect( &viewer, &CDemoViewer::TickChanged, this, [ this ] {
		// Heat up to the playhead is counted again a second of play at a time.
		if ( m_bUpToPlayhead && ( m_nMode == DemoPathMode_t::DEMO_PATH_PRESENCE || m_nMode == DemoPathMode_t::DEMO_PATH_DEATHS ) && std::abs( m_Viewer.GetTick() - m_nHeatTick ) * m_Viewer.GetDemo().GetTickInterval() >= 1.0 )
			InvalidateHeat();
		update();
	} );
}

//-----------------------------------------------------------------------------
// Purpose: What the paths cover: the camera's places and every living player's.
//-----------------------------------------------------------------------------
void CDemoPathView::FitBounds()
{
	bool bFirst = true;
	const auto add = [ & ]( const xv::CVec3 &vecAt ) {
		if ( vecAt.m_flX == 0.0f && vecAt.m_flY == 0.0f && vecAt.m_flZ == 0.0f )
			return;
		if ( bFirst )
		{
			m_vecMins = m_vecMaxs = vecAt;
			bFirst = false;
		}
		m_vecMins = { std::min( m_vecMins.m_flX, vecAt.m_flX ), std::min( m_vecMins.m_flY, vecAt.m_flY ), std::min( m_vecMins.m_flZ, vecAt.m_flZ ) };
		m_vecMaxs = { std::max( m_vecMaxs.m_flX, vecAt.m_flX ), std::max( m_vecMaxs.m_flY, vecAt.m_flY ), std::max( m_vecMaxs.m_flZ, vecAt.m_flZ ) };
	};
	for ( const xv::demo::View_t &view : m_Viewer.GetDemo().GetViews() )
		add( view.m_vecOrigin );
	for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
	{
		for ( const xv::demo::PlayerSample_t &sample : track.m_Samples )
		{
			if ( sample.m_bAlive && sample.m_bThere && sample.m_nTeam >= 2 )
				add( sample.m_vecOrigin );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A map place turned and tilted about the bounds' middle, then scaled and moved:
//          x across, y down the widget, and the depth into it.
//-----------------------------------------------------------------------------
QPointF CDemoPathView::Project( const Projection_t &projection, double x, double y, double z, double *pDepth ) const
{
	const double dx = x - ( m_vecMins.m_flX + m_vecMaxs.m_flX ) * 0.5;
	const double dy = y - ( m_vecMins.m_flY + m_vecMaxs.m_flY ) * 0.5;
	const double dz = z - ( m_vecMins.m_flZ + m_vecMaxs.m_flZ ) * 0.5;
	const double flAcross = dx * projection.m_flCosYaw - dy * projection.m_flSinYaw;
	const double flAway = dx * projection.m_flSinYaw + dy * projection.m_flCosYaw;
	if ( pDepth )
		*pDepth = flAway * projection.m_flCosPitch - dz * projection.m_flSinPitch;
	const double flUp = flAway * projection.m_flSinPitch + dz * projection.m_flCosPitch;
	return projection.m_Offset + QPointF( flAcross, -flUp ) * projection.m_flScale;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QPointF CDemoPathView::ToWidget( const xv::CVec3 &vecAt, double *pDepth ) const
{
	// A page not shown gets no resize event: its size is looked at here.
	if ( size() != m_ProjectedFor )
		UpdateProjection();
	return Project( m_Projection, vecAt.m_flX, vecAt.m_flY, vecAt.m_flZ, pDepth );
}

//-----------------------------------------------------------------------------
// Purpose: The bounds' corners as the view turns them fitted inside the margin, the same
//          scale both ways, centred; then the hand's zoom and pan.
//-----------------------------------------------------------------------------
void CDemoPathView::UpdateProjection() const
{
	m_ProjectedFor = size();
	Projection_t projection;
	projection.m_flYaw = m_nView == DemoPathView_t::DEMO_PATH_VIEW_TOP ? 0.0 : m_flYaw;
	projection.m_flPitch = m_nView == DemoPathView_t::DEMO_PATH_VIEW_TOP ? 90.0 : ISOMETRIC_PITCH;
	const double flYaw = projection.m_flYaw * std::numbers::pi / 180.0;
	const double flPitch = projection.m_flPitch * std::numbers::pi / 180.0;
	projection.m_flCosYaw = std::cos( flYaw );
	projection.m_flSinYaw = std::sin( flYaw );
	projection.m_flCosPitch = std::cos( flPitch );
	projection.m_flSinPitch = std::sin( flPitch );
	double flLeft = 0.0, flRight = 0.0, flTop = 0.0, flBottom = 0.0;
	for ( int i = 0; i < 8; i++ )
	{
		const QPointF at = Project( projection, i & 1 ? m_vecMaxs.m_flX : m_vecMins.m_flX, i & 2 ? m_vecMaxs.m_flY : m_vecMins.m_flY, i & 4 ? m_vecMaxs.m_flZ : m_vecMins.m_flZ );
		flLeft = std::min( flLeft, at.x() );
		flRight = std::max( flRight, at.x() );
		flTop = std::min( flTop, at.y() );
		flBottom = std::max( flBottom, at.y() );
	}
	const double flWide = std::max( 1.0, flRight - flLeft );
	const double flTall = std::max( 1.0, flBottom - flTop );
	projection.m_flScale = std::min( ( width() - 2 * PATH_MARGIN ) / flWide, ( height() - 2 * PATH_MARGIN ) / flTall ) * m_flZoom;
	projection.m_Offset = QPointF( width() * 0.5, height() * 0.5 ) - QPointF( flLeft + flRight, flTop + flBottom ) * 0.5 * projection.m_flScale + m_Pan;
	m_Projection = projection;
}

//-----------------------------------------------------------------------------
// Purpose: Give the heat, path and floor painting their own cadence; paint warps the old
//          pictures meanwhile.
//-----------------------------------------------------------------------------
void CDemoPathView::ViewChanged()
{
	UpdateProjection();
	if ( ( m_nMode == DemoPathMode_t::DEMO_PATH_PRESENCE || m_nMode == DemoPathMode_t::DEMO_PATH_DEATHS ) && !m_pHeatTimer->isActive() )
		m_pHeatTimer->start();
	if ( !m_FaintPaths.isNull() && !m_pPathTimer->isActive() )
		m_pPathTimer->start();
	// At most every FLOOR_DELAY while the view keeps moving, so a long turn doesn't leave the
	// warped floor drifting from the heat and paths drawn over it.
	if ( !m_pFloorTimer->isActive() )
		m_pFloorTimer->start();
	update();
}

//-----------------------------------------------------------------------------
// Purpose: The way a yaw faces from a place, as the view shows it, flLength pixels long.
//-----------------------------------------------------------------------------
QPointF CDemoPathView::Facing( const xv::CVec3 &vecAt, double flYaw, double flLength ) const
{
	const double flRadians = flYaw * std::numbers::pi / 180.0;
	const QPointF from = ToWidget( vecAt );
	const QPointF to = ToWidget( xv::CVec3{ vecAt.m_flX + static_cast<float>( std::cos( flRadians ) * 64.0 ), vecAt.m_flY + static_cast<float>( std::sin( flRadians ) * 64.0 ), vecAt.m_flZ } );
	const QPointF way = to - from;
	const double flWay = std::hypot( way.x(), way.y() );
	return flWay > 0.0 ? way * ( flLength / flWay ) : QPointF{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::SetView( DemoPathView_t nView )
{
	if ( nView == m_nView )
		return;
	m_nView = nView;
	InvalidatePaths();
	m_flZoom = 1.0;
	m_Pan = {};
	ViewChanged();
	emit ViewModeChanged( m_nView );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::SetYaw( double flYaw )
{
	m_flYaw = std::remainder( flYaw, 360.0 );
	ViewChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::ResetView()
{
	InvalidatePaths();
	m_flZoom = 1.0;
	m_Pan = {};
	ViewChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::SetMode( DemoPathMode_t nMode )
{
	m_nMode = nMode;
	InvalidatePaths();
	InvalidateHeat();
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::SetUpToPlayhead( bool bUpTo )
{
	m_bUpToPlayhead = bUpTo;
	InvalidateHeat();
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::SetTeam( int nTeam )
{
	m_nTeam = nTeam;
	InvalidatePaths();
	InvalidateHeat();
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::MapArrived()
{
	UpdateProjection();
	BuildFloor();
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Load a placed overview through the same mounts as the demo's map.
//-----------------------------------------------------------------------------
void CDemoPathView::LoadRadar()
{
	m_Radar = {};
	update();
	const auto files = m_Viewer.GetWorld().GetMapFiles();
	if ( !files )
		return;
	const std::string &szMap = m_Viewer.GetDemo().GetHeader().m_szMap;
	const auto text = files->ReadText( "resource/overviews/" + szMap + ".txt" );
	if ( !text )
		return;
	const auto parsed = xv::kv1::Parse( *text );
	const auto *pMap = parsed.m_Document.Find( szMap );
	if ( !pMap || !pMap->IsBlock() )
		return;
	const auto value = [ pMap ]( std::string_view szKey ) {
		const auto *pValue = pMap->Find( szKey );
		return pValue && pValue->IsPair() ? pValue->GetValue() : std::string{};
	};
	const auto x = xv::ParseNumber<double>( value( "pos_x" ) );
	const auto y = xv::ParseNumber<double>( value( "pos_y" ) );
	const auto scale = xv::ParseNumber<double>( value( "scale" ) );
	if ( !x || !y || !scale || !std::isfinite( *x ) || !std::isfinite( *y ) || !std::isfinite( *scale ) || *scale <= 0.0 )
		return;
	std::string szMaterial( value( "material" ) );
	if ( szMaterial.empty() )
		szMaterial = "overviews/" + szMap;
	if ( !szMaterial.starts_with( "materials/" ) )
		szMaterial.insert( 0, "materials/" );
	if ( szMaterial.ends_with( ".vmt" ) || szMaterial.ends_with( ".vtf" ) )
		szMaterial.resize( szMaterial.size() - 4 );
	const auto material = xv::map::ReadMaterialInfo( std::string_view( szMaterial ).substr( std::string_view( "materials/" ).size() ),
		[ &files ]( std::string_view szPath ) { return files->Read( szPath ); } );
	const std::string szTexture = material.m_szTexture.empty() ? szMaterial : "materials/" + material.m_szTexture;
	for ( const auto &szPath : { szTexture + ".vtf", szMaterial + "_radar.vtf", "materials/overviews/" + szMap + "_radar.vtf" } )
	{
		const auto bytes = files->Read( szPath );
		const auto vtf = bytes ? xv::texture::ReadVtf( *bytes ) : std::nullopt;
		const auto picture = vtf ? vtf->Decode() : std::nullopt;
		if ( picture )
		{
			m_Radar = ToQImage( *picture );
			break;
		}
	}
	if ( m_Radar.isNull() )
		return;
	if ( xv::ParseNumber<int>( value( "rotate" ) ).value_or( 0 ) != 0 )
	{
		// Some CS:S overviews (including Dust II) already match the world axes with rotate set.
		// Use the spawn points as anchors when they distinguish the stored and rotated images.
		QImage rotated = m_Radar.transformed( QTransform().rotate( 90 ) );
		const auto &entities = m_Viewer.GetWorld().GetLogicMap().m_Entities;
		int nNorth = 0, nRotated = 0;
		const auto covers = []( const QImage &image, const QPointF &uv ) {
			return uv.x() >= 0.0 && uv.y() >= 0.0 && uv.x() < image.width() && uv.y() < image.height() &&
				image.pixelColor( static_cast<int>( uv.x() ), static_cast<int>( uv.y() ) ).alpha() > 127;
		};
		for ( const auto &entity : entities )
		{
			if ( !entity.Get( "classname" ).starts_with( "info_player_" ) )
				continue;
			double flX, flY;
			if ( std::sscanf( std::string( entity.Get( "origin" ) ).c_str(), "%lf %lf", &flX, &flY ) != 2 )
				continue;
			const QPointF uv( ( flX - *x ) / *scale, ( *y - flY ) / *scale );
			nNorth += covers( m_Radar, uv );
			nRotated += covers( rotated, uv );
		}
		if ( nRotated >= nNorth )
			m_Radar = std::move( rotated );
	}
	m_RadarOrigin = { *x, *y, 0.0 };
	m_flRadarScale = *scale;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Draw the north-up overview at its world origin and units per pixel.
//-----------------------------------------------------------------------------
void CDemoPathView::PaintRadar( QPainter &painter ) const
{
	const auto &origin = m_RadarOrigin;
	const auto topLeft = Project( m_Projection, origin[ 0 ], origin[ 1 ], origin[ 2 ] );
	const auto bottomRight = Project( m_Projection, origin[ 0 ] + m_Radar.width() * m_flRadarScale,
		origin[ 1 ] - m_Radar.height() * m_flRadarScale, origin[ 2 ] );
	painter.setRenderHint( QPainter::SmoothPixmapTransform );
	painter.drawImage( QRectF( topLeft, bottomRight ), m_Radar );
}

//-----------------------------------------------------------------------------
// Purpose: Project each continuous path and keep points at least a pixel apart. Break
//          stretches on teleports, deaths, respawns and team changes.
//-----------------------------------------------------------------------------
void CDemoPathView::BuildStretches()
{
	m_Stretches.clear();
	const auto add = [ & ]( const xv::CVec3 &vecAt, int nTick, bool bBreak, const QColor &color ) {
		const QPointF at = ToWidget( vecAt );
		if ( m_Stretches.empty() || bBreak )
		{
			m_Stretches.emplace_back();
			m_Stretches.back().m_Color = color;
		}
		Stretch_t &stretch = m_Stretches.back();
		if ( !stretch.m_Points.isEmpty() && ( stretch.m_Points.back() - at ).manhattanLength() < 1.0 )
			return;
		stretch.m_Points.push_back( at );
		stretch.m_Ticks.push_back( nTick );
	};
	const auto jump = []( const xv::CVec3 &a, const xv::CVec3 &b ) { return std::abs( a.m_flX - b.m_flX ) > PATH_JUMP || std::abs( a.m_flY - b.m_flY ) > PATH_JUMP || std::abs( a.m_flZ - b.m_flZ ) > PATH_JUMP; };
	if ( m_nMode == DemoPathMode_t::DEMO_PATH_CAMERA )
	{
		const xv::CVec3 *pLast = nullptr;
		for ( const xv::demo::View_t &view : m_Viewer.GetDemo().GetViews() )
		{
			const xv::CVec3 &vecAt = view.m_vecOrigin;
			if ( vecAt.m_flX == 0.0f && vecAt.m_flY == 0.0f && vecAt.m_flZ == 0.0f )
				continue;
			add( vecAt, view.m_nTick, pLast && jump( vecAt, *pLast ), palette().color( QPalette::Highlight ) );
			pLast = &vecAt;
		}
		return;
	}
	if ( m_nMode != DemoPathMode_t::DEMO_PATH_PLAYERS )
		return;
	for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
	{
		const xv::demo::PlayerSample_t *pLast = nullptr;
		for ( const xv::demo::PlayerSample_t &sample : track.m_Samples )
		{
			const bool bShown = sample.m_bAlive && sample.m_bThere && sample.m_nTeam >= 2 && ( m_nTeam == 0 || sample.m_nTeam == m_nTeam );
			if ( !bShown )
			{
				pLast = nullptr;
				continue;
			}
			const bool bBreak = !pLast || jump( sample.m_vecOrigin, pLast->m_vecOrigin ) || pLast->m_nTeam != sample.m_nTeam;
			add( sample.m_vecOrigin, sample.m_nTick, bBreak, DemoTeamColor( sample.m_nTeam ) );
			pLast = &sample;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The next paint will draw both path layers for the current view and filters.
//-----------------------------------------------------------------------------
void CDemoPathView::InvalidatePaths()
{
	m_pPathTimer->stop();
	m_FaintPaths = {};
	m_PlayedPaths = {};
	m_Stretches.clear();
	m_nPathTick = -1;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Keep the whole path faint and advance a separate image to the playhead. A rewind
//          clears the latter so the strong path ends at the new tick.
//-----------------------------------------------------------------------------
void CDemoPathView::UpdatePlayedPath( int nTick )
{
	if ( nTick == m_nPathTick )
		return;
	const bool bRestart = m_nPathTick < 0 || nTick < m_nPathTick;
	if ( bRestart )
		m_PlayedPaths.fill( Qt::transparent );

	QPainter painter( &m_PlayedPaths );
	painter.setRenderHint( QPainter::Antialiasing );
	const bool bCamera = m_nMode == DemoPathMode_t::DEMO_PATH_CAMERA;
	for ( const Stretch_t &stretch : m_Stretches )
	{
		const auto first = stretch.m_Ticks.begin();
		const auto end = stretch.m_Ticks.end();
		const auto nNow = std::upper_bound( first, end, nTick ) - first;
		const auto nWas = bRestart ? 0 : std::upper_bound( first, end, m_nPathTick ) - first;
		const auto nBegin = std::max<qsizetype>( 0, nWas - 1 );
		if ( nNow - nBegin < 2 )
			continue;
		QColor strong = stretch.m_Color;
		strong.setAlphaF( bCamera ? 1.0f : 0.7f );
		painter.setPen( QPen( strong, bCamera ? 2.0 : 1.2 ) );
		painter.drawPolyline( stretch.m_Points.constData() + nBegin, static_cast<int>( nNow - nBegin ) );
	}
	m_nPathTick = nTick;
}

//-----------------------------------------------------------------------------
// Purpose: Draw the cached paths, warping the old images during a pan, zoom or turn.
//-----------------------------------------------------------------------------
void CDemoPathView::PaintPaths( QPainter &painter, int nTick )
{
	const bool bCamera = m_nMode == DemoPathMode_t::DEMO_PATH_CAMERA;
	if ( !bCamera && m_nMode != DemoPathMode_t::DEMO_PATH_PLAYERS )
		return;

	const double flDpr = devicePixelRatioF();
	const qint64 nPalette = palette().cacheKey();
	if ( !m_FaintPaths.isNull() && ( m_FaintPaths.devicePixelRatio() != flDpr || m_nPathPalette != nPalette ) )
		InvalidatePaths();
	if ( m_FaintPaths.isNull() )
	{
		m_nPathPalette = nPalette;
		BuildStretches();
		m_PathProjection = m_Projection;
		const QSize pixels( qCeil( width() * flDpr ), qCeil( height() * flDpr ) );
		m_FaintPaths = QImage( pixels, QImage::Format_ARGB32_Premultiplied );
		m_PlayedPaths = QImage( pixels, QImage::Format_ARGB32_Premultiplied );
		m_FaintPaths.setDevicePixelRatio( flDpr );
		m_PlayedPaths.setDevicePixelRatio( flDpr );
		m_FaintPaths.fill( Qt::transparent );
		QPainter faintPainter( &m_FaintPaths );
		faintPainter.setRenderHint( QPainter::Antialiasing );
		for ( const Stretch_t &stretch : m_Stretches )
		{
			QColor faint = bCamera ? palette().color( QPalette::Text ) : stretch.m_Color;
			faint.setAlphaF( bCamera ? 0.25f : 0.18f );
			faintPainter.setPen( QPen( faint, 1 ) );
			faintPainter.drawPolyline( stretch.m_Points );
		}
	}
	UpdatePlayedPath( nTick );
	const QTransform warp = Reprojection( m_PathProjection );
	painter.save();
	painter.setTransform( warp, true );
	painter.setRenderHint( QPainter::SmoothPixmapTransform, !warp.isIdentity() );
	painter.drawImage( 0, 0, m_FaintPaths );
	painter.drawImage( 0, 0, m_PlayedPaths );
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: The world's faces and every brush entity's where the lump places it, as the view
//          sees them, the furthest first so the nearer cover them. From the top: the level
//          faces within the heights players stood at, lighter the higher (floors and
//          ceilings alike, by their shape). Isometric: every face not turned down, cut off
//          above the highest place a player stood, lit from one side and lighter the higher.
//-----------------------------------------------------------------------------
void CDemoPathView::BuildFloor()
{
	m_Floor = {};
	m_FloorProjection = m_Projection;
	if ( HasRadarBackground() )
		return;
	const xv::bsp::CompiledMap_t *pMap = m_Viewer.GetWorld().GetCompiled();
	if ( !pMap || pMap->m_Models.empty() || width() <= 0 || height() <= 0 )
		return;
	const bool bTop = m_nView == DemoPathView_t::DEMO_PATH_VIEW_TOP;
	struct Polygon_t
	{
		double m_flDepth;
		float m_flShade; // 0 dark to 1 light
		int m_nCorners;
		std::array<QPointF, 9> m_Corners; // a triangle cut by up to six planes
		float m_flTop = 0.0f, m_flBottom = 0.0f; // in the widget
	};
	std::vector<Polygon_t> polygons;
	// Heights drawn: those players stood at, or the hand's cut.
	const bool bCutZ = m_Cut.m_bOn[ 2 ];
	const float flLow = bCutZ ? static_cast<float>( m_Cut.m_flMin[ 2 ] ) : m_vecMins.m_flZ - 128.0f;
	const float flHigh = bCutZ ? static_cast<float>( std::max( m_Cut.m_flMin[ 2 ] + 1.0, m_Cut.m_flMax[ 2 ] ) ) : bTop ? std::max( flLow + 1.0f, m_vecMaxs.m_flZ + 16.0f )
																													   : m_vecMaxs.m_flZ + ISOMETRIC_CUT;
	// The light of the isometric view, from up and to one side.
	const xv::map::Point_t light = xv::map::Scale( xv::map::Point_t{ -0.45, -0.3, 0.84 }, 1.0 / xv::map::Length( xv::map::Point_t{ -0.45, -0.3, 0.84 } ) );
	const QRectF shown = QRectF( rect() ).adjusted( -2, -2, 2, 2 );
	// The sky's shell and the tools' faces (nodraw, clips) aren't the map as players see it:
	// the shell's walls would stand in front of everything.
	std::vector<bool> hidden( pMap->m_Materials.size(), false );
	for ( std::size_t i = 0; i < pMap->m_Materials.size(); i++ )
		hidden[ i ] = QString::fromStdString( pMap->m_Materials[ i ] ).startsWith( QStringLiteral( "tools/" ), Qt::CaseInsensitive );
	// The triangles drawn, in runs a thread takes at a time: the world's batches and each
	// brush entity's, where the lump places it.
	struct Run_t
	{
		const xv::map::CMapMesh *m_pMesh;
		xv::map::Transform_t m_Place;
		std::size_t m_nFirst, m_nEnd; // indices
	};
	std::vector<Run_t> runs;
	const auto addMesh = [ & ]( const xv::map::CMapMesh &mesh, const xv::map::Transform_t &place ) {
		constexpr std::size_t RUN = 3 * 4096;
		const auto addRange = [ & ]( std::size_t nFirst, std::size_t nEnd ) {
			nEnd = std::min( nEnd, mesh.m_Indices.size() );
			for ( std::size_t n = nFirst; n < nEnd; n += RUN )
				runs.push_back( { &mesh, place, n, std::min( nEnd, n + RUN ) } );
		};
		for ( const xv::map::MeshBatch_t &batch : mesh.m_Batches )
		{
			if ( batch.m_nMaterial >= hidden.size() || !hidden[ batch.m_nMaterial ] )
				addRange( batch.m_nFirstIndex, batch.m_nFirstIndex + batch.m_nIndexCount );
		}
		if ( mesh.m_Batches.empty() )
			addRange( 0, mesh.m_Indices.size() );
	};
	const auto gather = [ & ]( const Run_t &run, std::vector<Polygon_t> &out ) {
		const xv::map::CMapMesh &mesh = *run.m_pMesh;
		const xv::map::Transform_t &place = run.m_Place;
		{
			for ( std::size_t i = run.m_nFirst; i + 2 < run.m_nEnd; i += 3 )
			{
				std::array<xv::map::Point_t, 3> at;
				for ( int n = 0; n < 3; n++ )
					at[ static_cast<std::size_t>( n ) ] = place.Apply( xv::map::ToPoint( mesh.m_Vertices[ mesh.m_Indices[ i + static_cast<std::size_t>( n ) ] ].m_vecPosition ) );
				xv::map::Point_t normal = xv::map::Cross( xv::map::Subtract( at[ 1 ], at[ 0 ] ), xv::map::Subtract( at[ 2 ], at[ 0 ] ) );
				const double flLength = xv::map::Length( normal );
				if ( flLength <= 0.0 )
					continue;
				// Which way the face looks is its vertices' normals' (the winding may run either
				// way), turned as the model is.
				const xv::map::Point_t facing = xv::map::Subtract( place.Apply( xv::map::Add( xv::map::ToPoint( mesh.m_Vertices[ mesh.m_Indices[ i ] ].m_vecPosition ), xv::map::ToPoint( mesh.m_Vertices[ mesh.m_Indices[ i ] ].m_vecNormal ) ) ), at[ 0 ] );
				if ( xv::map::Dot( normal, facing ) < 0.0 )
					normal = xv::map::Scale( normal, -1.0 );
				const double flUp = normal[ 2 ] / flLength;
				const double flLowest = std::min( { at[ 0 ][ 2 ], at[ 1 ][ 2 ], at[ 2 ][ 2 ] } );
				const double flHighest = std::max( { at[ 0 ][ 2 ], at[ 1 ][ 2 ], at[ 2 ][ 2 ] } );
				std::vector<xv::map::Point_t> corners( at.begin(), at.end() );
				if ( bTop )
				{
					const double flMiddle = ( at[ 0 ][ 2 ] + at[ 1 ][ 2 ] + at[ 2 ][ 2 ] ) / 3.0;
					if ( std::abs( flUp ) < 0.6 || flMiddle < flLow || flMiddle > flHigh )
						continue;
				}
				else
				{
					if ( flUp < -0.2 || flHighest < flLow || flLowest > flHigh )
						continue;
					if ( flHighest > flHigh )
						corners = ClipPolygon( std::move( corners ), 2, flHigh, false );
				}
				// The hand's cut: each axis kept between its two values.
				for ( int nAxis = 0; nAxis < 3; nAxis++ )
				{
					if ( !m_Cut.m_bOn[ static_cast<std::size_t>( nAxis ) ] )
						continue;
					corners = ClipPolygon( std::move( corners ), nAxis, m_Cut.m_flMin[ static_cast<std::size_t>( nAxis ) ], true );
					corners = ClipPolygon( std::move( corners ), nAxis, m_Cut.m_flMax[ static_cast<std::size_t>( nAxis ) ], false );
				}
				if ( corners.size() < 3 )
					continue;
				Polygon_t polygon{ 0.0, 0.0f, static_cast<int>( corners.size() ), {} };
				// The corners' extent by hand: QRectF::united drops empty rects, a point's among them.
				double flLeft = std::numeric_limits<double>::max(), flTop = flLeft;
				double flRight = std::numeric_limits<double>::lowest(), flBottom = flRight;
				for ( std::size_t n = 0; n < corners.size(); n++ )
				{
					double flDepth = 0.0;
					const QPointF point = bTop ? Project( m_Projection, corners[ n ][ 0 ], corners[ n ][ 1 ], 0.0 ) : Project( m_Projection, corners[ n ][ 0 ], corners[ n ][ 1 ], corners[ n ][ 2 ], &flDepth );
					polygon.m_Corners[ n ] = point;
					polygon.m_flDepth += flDepth / static_cast<double>( corners.size() );
					flLeft = std::min( flLeft, point.x() );
					flRight = std::max( flRight, point.x() );
					flTop = std::min( flTop, point.y() );
					flBottom = std::max( flBottom, point.y() );
				}
				if ( flRight < shown.left() - 1 || flLeft > shown.right() + 1 || flBottom < shown.top() - 1 || flTop > shown.bottom() + 1 )
					continue;
				polygon.m_flTop = static_cast<float>( flTop );
				polygon.m_flBottom = static_cast<float>( flBottom );
				const double flMiddle = ( flLowest + flHighest ) * 0.5;
				const float flHeight = static_cast<float>( std::clamp( ( flMiddle - flLow ) / ( flHigh - flLow ), 0.0, 1.0 ) );
				if ( bTop )
				{
					// Lower first, so the higher cover them.
					polygon.m_flDepth = -flMiddle;
					polygon.m_flShade = flHeight;
				}
				else
				{
					const double flLit = std::abs( xv::map::Dot( normal, light ) ) / flLength;
					polygon.m_flShade = static_cast<float>( 0.15 + 0.55 * flLit + 0.3 * flHeight );
				}
				out.push_back( polygon );
			}
		}
	};
	if ( pMap->m_Models.front().m_pMesh )
		addMesh( *pMap->m_Models.front().m_pMesh, {} );
	for ( const xv::bsp::ModelPlacement_t &placement : pMap->m_Placements )
	{
		if ( placement.m_nModel > 0 && static_cast<std::size_t>( placement.m_nModel ) < pMap->m_Models.size() && pMap->m_Models[ static_cast<std::size_t>( placement.m_nModel ) ].m_pMesh )
			addMesh( *pMap->m_Models[ static_cast<std::size_t>( placement.m_nModel ) ].m_pMesh, xv::map::Transform_t::FromAngles( placement.m_Origin, placement.m_Angles ) );
	}
	std::vector<std::vector<Polygon_t>> gathered( runs.size() );
	xv::ParallelFor( runs.size(), 1, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t n = nBegin; n < nEnd; n++ )
			gather( runs[ n ], gathered[ n ] );
	} );
	std::size_t nPolygons = 0;
	for ( const std::vector<Polygon_t> &part : gathered )
		nPolygons += part.size();
	polygons.reserve( nPolygons );
	for ( const std::vector<Polygon_t> &part : gathered )
		polygons.insert( polygons.end(), part.begin(), part.end() );
	std::ranges::sort( polygons, std::greater<>{}, &Polygon_t::m_flDepth );
	m_Floor = QImage( size(), QImage::Format_ARGB32_Premultiplied );
	m_Floor.fill( Qt::transparent );
	const bool bDark = palette().color( QPalette::Base ).lightness() < 128;
	// A big map is a hundred thousand faces and more: each thread paints a band of rows, every
	// face that reaches it in the same far-to-near order, straight into the picture's rows.
	const int nBands = static_cast<int>( std::clamp<std::size_t>( xv::ThreadCount(), 1, 16 ) );
	const int nBandRows = ( m_Floor.height() + nBands - 1 ) / nBands;
	xv::ParallelFor( static_cast<std::size_t>( nBands ), 1, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t nBand = nBegin; nBand < nEnd; nBand++ )
		{
			const int nTop = static_cast<int>( nBand ) * nBandRows;
			const int nRows = std::min( nBandRows, m_Floor.height() - nTop );
			if ( nRows <= 0 )
				continue;
			QImage band( m_Floor.scanLine( nTop ), m_Floor.width(), nRows, m_Floor.bytesPerLine(), m_Floor.format() );
			QPainter painter( &band );
			painter.translate( 0, -nTop );
			int nLastShade = -1;
			for ( const Polygon_t &polygon : polygons )
			{
				if ( polygon.m_flBottom < nTop - 1 || polygon.m_flTop > nTop + nRows + 1 )
					continue;
				const int nShade = bDark ? 40 + static_cast<int>( 100 * polygon.m_flShade ) : 140 + static_cast<int>( 100 * polygon.m_flShade );
				if ( nShade != nLastShade )
				{
					// The isometric faces' edges in their own colour, so no seams show between them.
					painter.setPen( bTop ? QPen( Qt::NoPen ) : QPen( QColor( nShade, nShade, nShade ), 0 ) );
					painter.setBrush( QColor( nShade, nShade, nShade ) );
					nLastShade = nShade;
				}
				painter.drawPolygon( polygon.m_Corners.data(), polygon.m_nCorners );
			}
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose: A polygon cut by the plane where an axis is flAt: the part above it kept, or the
//          part below.
//-----------------------------------------------------------------------------
std::vector<xv::map::Point_t> CDemoPathView::ClipPolygon( std::vector<xv::map::Point_t> corners, int nAxis, double flAt, bool bKeepAbove )
{
	std::vector<xv::map::Point_t> kept;
	const auto inside = [ & ]( const xv::map::Point_t &p ) { return bKeepAbove ? p[ static_cast<std::size_t>( nAxis ) ] >= flAt : p[ static_cast<std::size_t>( nAxis ) ] <= flAt; };
	for ( std::size_t n = 0; n < corners.size(); n++ )
	{
		const xv::map::Point_t &a = corners[ n ];
		const xv::map::Point_t &b = corners[ ( n + 1 ) % corners.size() ];
		const bool bA = inside( a ), bB = inside( b );
		if ( bA )
			kept.push_back( a );
		if ( bA != bB )
		{
			const double t = ( flAt - a[ static_cast<std::size_t>( nAxis ) ] ) / ( b[ static_cast<std::size_t>( nAxis ) ] - a[ static_cast<std::size_t>( nAxis ) ] );
			kept.push_back( xv::map::Add( a, xv::map::Scale( xv::map::Subtract( b, a ), t ) ) );
		}
	}
	return kept;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a place is inside the hand's cut.
//-----------------------------------------------------------------------------
bool CDemoPathView::IsInCut( const xv::CVec3 &vecAt ) const
{
	const double at[ 3 ] = { vecAt.m_flX, vecAt.m_flY, vecAt.m_flZ };
	for ( std::size_t nAxis = 0; nAxis < 3; nAxis++ )
	{
		if ( m_Cut.m_bOn[ nAxis ] && ( at[ nAxis ] < m_Cut.m_flMin[ nAxis ] || at[ nAxis ] > m_Cut.m_flMax[ nAxis ] ) )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::SetCut( const PathCut_t &cut )
{
	m_Cut = cut;
	InvalidateHeat();
	if ( !m_pCutTimer->isActive() )
		m_pCutTimer->start();
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Show a cut's guides independently of whether the cut is enabled.
//-----------------------------------------------------------------------------
void CDemoPathView::SetCutGrid( int nAxis, bool bShow )
{
	m_CutGrids[ static_cast<std::size_t>( nAxis ) ] = bShow;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Project each guide and its handle. In the top view the vertical planes are
//          edge-on, so draw full-width or full-height lines with staggered handles.
//-----------------------------------------------------------------------------
std::vector<CDemoPathView::CutPlane_t> CDemoPathView::CutPlanes() const
{
	std::vector<CutPlane_t> planes;
	const bool bTop = m_nView == DemoPathView_t::DEMO_PATH_VIEW_TOP;
	const xv::map::Point_t mins{ m_vecMins.m_flX - 16.0, m_vecMins.m_flY - 16.0, m_vecMins.m_flZ - 16.0 };
	const xv::map::Point_t maxs{ m_vecMaxs.m_flX + 16.0, m_vecMaxs.m_flY + 16.0, m_vecMaxs.m_flZ + 128.0 };
	const auto project = [ this ]( const xv::map::Point_t &point ) {
		return ToWidget( xv::map::ToVec3( point ) );
	};
	for ( int nAxis = 0; nAxis < ( bTop ? 2 : 3 ); ++nAxis )
	{
		if ( !m_CutGrids[ nAxis ] )
			continue;
		const int nU = ( nAxis + 1 ) % 3, nV = ( nAxis + 2 ) % 3;
		for ( const bool bUpper : { false, true } )
		{
			const double flValue = bUpper ? m_Cut.m_flMax[ nAxis ] : m_Cut.m_flMin[ nAxis ];
			CutPlane_t plane{ nAxis, bUpper, flValue, {}, {}, {} };
			xv::map::Point_t corner = mins;
			corner[ nAxis ] = flValue;
			const QPointF first = project( corner );
			corner[ nAxis ] += 1.0;
			plane.m_Direction = project( corner ) - first;
			if ( bTop )
			{
				const double flFraction = bUpper ? 0.60 : 0.40;
				if ( nAxis == 0 )
				{
					plane.m_Outline << QPointF( first.x(), 0 ) << QPointF( first.x(), height() );
					plane.m_Handle = { first.x(), height() * flFraction };
				}
				else
				{
					plane.m_Outline << QPointF( 0, first.y() ) << QPointF( width(), first.y() );
					plane.m_Handle = { width() * flFraction, first.y() };
				}
			}
			else
			{
				corner[ nAxis ] = flValue;
				plane.m_Outline << first;
				corner[ nU ] = maxs[ nU ];
				plane.m_Outline << project( corner );
				corner[ nV ] = maxs[ nV ];
				plane.m_Outline << project( corner );
				corner[ nU ] = mins[ nU ];
				plane.m_Outline << project( corner );
				const double flFraction = bUpper ? 0.62 : 0.38;
				plane.m_Handle = first + ( plane.m_Outline[ 2 ] - first ) * flFraction;
				const QRectF viewport = QRectF( rect() ).adjusted( 6, 6, -6, -6 );
				if ( !viewport.contains( plane.m_Handle ) )
				{
					// Keep a handle reachable when only a corner of its plane is in view.
					const QPolygonF visible = plane.m_Outline.intersected( QPolygonF( viewport ) );
					if ( !visible.isEmpty() )
					{
						QPointF center;
						for ( const QPointF &point : visible )
							center += point;
						plane.m_Handle = center / static_cast<double>( visible.size() );
					}
				}
			}
			if ( !plane.m_Direction.isNull() )
				planes.push_back( std::move( plane ) );
		}
	}
	return planes;
}

//-----------------------------------------------------------------------------
// Purpose: Overlay the cut grids and give each draggable bound a distinct handle.
//-----------------------------------------------------------------------------
void CDemoPathView::PaintCutGrid( QPainter &painter ) const
{
	const auto planes = CutPlanes();
	for ( const CutPlane_t &plane : planes )
	{
		QColor color = CUT_COLORS[ plane.m_nAxis ];
		color.setAlpha( m_Cut.m_bOn[ plane.m_nAxis ] ? 155 : 95 );
		painter.setBrush( Qt::NoBrush );
		painter.setPen( QPen( color, 1 ) );
		const auto &outline = plane.m_Outline;
		if ( outline.size() == 2 )
		{
			painter.drawLine( outline[ 0 ], outline[ 1 ] );
			continue;
		}
		painter.drawPolygon( outline );
		color.setAlpha( color.alpha() / 2 );
		painter.setPen( QPen( color, 1 ) );
		for ( int i = 1; i < 8; ++i )
		{
			const double t = i / 8.0;
			painter.drawLine( outline[ 0 ] + ( outline[ 1 ] - outline[ 0 ] ) * t, outline[ 3 ] + ( outline[ 2 ] - outline[ 3 ] ) * t );
			painter.drawLine( outline[ 0 ] + ( outline[ 3 ] - outline[ 0 ] ) * t, outline[ 1 ] + ( outline[ 2 ] - outline[ 1 ] ) * t );
		}
	}
	for ( const CutPlane_t &plane : planes )
	{
		painter.setBrush( CUT_COLORS[ plane.m_nAxis ] );
		painter.setPen( QPen( palette().color( QPalette::Base ), 1.5 ) );
		const QRectF handle( plane.m_Handle - QPointF( 5, 5 ), QSizeF( 10, 10 ) );
		painter.drawRoundedRect( handle, 2, 2 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::SetShowingMap( bool bShow )
{
	m_bShowMap = bShow;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Choose the top view's background without moving the camera path.
//-----------------------------------------------------------------------------
void CDemoPathView::SetPreferRadar( bool bPrefer )
{
	const bool bHadRadar = HasRadarBackground();
	m_bPreferRadar = bPrefer;
	if ( bHadRadar != HasRadarBackground() )
	{
		m_pFloorTimer->start();
		update();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The heat in the world, binned a fraction of the spread across: the seconds living
//          players spent in each bin (from the time to their next sample, a second at most,
//          laid along the way they walked), or the deaths there. Counted once for what it
//          counts, not for the view, so a pan, zoom or turn only projects the bins.
//-----------------------------------------------------------------------------
void CDemoPathView::GatherHeat()
{
	m_HeatPoints.clear();
	m_bHeatGathered = true;
	m_nHeatTick = m_Viewer.GetTick();
	if ( m_nMode != DemoPathMode_t::DEMO_PATH_PRESENCE && m_nMode != DemoPathMode_t::DEMO_PATH_DEATHS )
		return;
	const float flBin = std::max( 4.0f, m_flHeatSpread / 3.0f );
	struct Bin_t
	{
		double m_flX = 0.0, m_flY = 0.0, m_flZ = 0.0;
		float m_flAmount = 0.0f;
	};
	std::unordered_map<std::uint64_t, Bin_t> bins;
	const auto binOf = [ flBin ]( float fl ) { return static_cast<std::uint64_t>( static_cast<std::int64_t>( std::floor( fl / flBin ) ) & 0x1fffff ); };
	const auto add = [ & ]( const xv::CVec3 &vecAt, float flAmount ) {
		Bin_t &bin = bins[ binOf( vecAt.m_flX ) | binOf( vecAt.m_flY ) << 21 | binOf( vecAt.m_flZ ) << 42 ];
		bin.m_flX += static_cast<double>( vecAt.m_flX ) * flAmount;
		bin.m_flY += static_cast<double>( vecAt.m_flY ) * flAmount;
		bin.m_flZ += static_cast<double>( vecAt.m_flZ ) * flAmount;
		bin.m_flAmount += flAmount;
	};
	const int nUpTo = m_bUpToPlayhead ? m_Viewer.GetTick() : std::numeric_limits<int>::max();
	const double flInterval = m_Viewer.GetDemo().GetTickInterval() > 0.0 ? m_Viewer.GetDemo().GetTickInterval() : FALLBACK_TICK_INTERVAL;
	if ( m_nMode == DemoPathMode_t::DEMO_PATH_PRESENCE )
	{
		// Farther than this between two samples is a teleport or a respawn, not a walk.
		constexpr float WALK_LIMIT = 256.0f;
		for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
		{
			for ( std::size_t i = 0; i < track.m_Samples.size(); i++ )
			{
				const xv::demo::PlayerSample_t &sample = track.m_Samples[ i ];
				if ( sample.m_nTick > nUpTo )
					break;
				if ( !sample.m_bAlive || !sample.m_bThere || sample.m_nTeam < 2 || ( m_nTeam != 0 && sample.m_nTeam != m_nTeam ) || !IsInCut( sample.m_vecOrigin ) )
					continue;
				const xv::demo::PlayerSample_t *pNext = i + 1 < track.m_Samples.size() ? &track.m_Samples[ i + 1 ] : nullptr;
				const int nNext = pNext ? std::min( pNext->m_nTick, nUpTo ) : sample.m_nTick + 1;
				const float flSeconds = static_cast<float>( std::min( 1.0, ( nNext - sample.m_nTick ) * flInterval ) );
				// The time between two samples is spread along the line between them, so a run
				// reads as a trail rather than a row of dots.
				const float flWalked = pNext ? xv::Length( pNext->m_vecOrigin - sample.m_vecOrigin ) : 0.0f;
				if ( pNext && pNext->m_bAlive && pNext->m_bThere && pNext->m_nTick <= nUpTo && flWalked < WALK_LIMIT && flWalked > flBin )
				{
					const int nSteps = std::clamp( static_cast<int>( flWalked / flBin ) + 1, 1, 64 );
					for ( int n = 0; n < nSteps; n++ )
					{
						const float t = ( static_cast<float>( n ) + 0.5f ) / static_cast<float>( nSteps );
						add( sample.m_vecOrigin + ( pNext->m_vecOrigin - sample.m_vecOrigin ) * t, flSeconds / static_cast<float>( nSteps ) );
					}
				}
				else
				{
					add( sample.m_vecOrigin, flSeconds );
				}
			}
		}
	}
	else
	{
		for ( const xv::demo::Kill_t &kill : m_Viewer.GetFeed().GetKills() )
		{
			if ( kill.m_nTick > nUpTo )
				break;
			// Where the victim last stood alive.
			if ( const auto state = m_Viewer.GetPlayers().At( kill.m_nVictim, kill.m_nTick - 1 ) )
			{
				if ( ( m_nTeam == 0 || state->m_nTeam == m_nTeam ) && IsInCut( state->m_vecOrigin ) )
					add( state->m_vecOrigin, 1.0f );
			}
		}
	}
	m_HeatPoints.reserve( bins.size() );
	for ( const auto &[ nKey, bin ] : bins )
	{
		if ( bin.m_flAmount <= 0.0f )
			continue;
		const double flShare = 1.0 / bin.m_flAmount;
		m_HeatPoints.push_back( { xv::CVec3{ static_cast<float>( bin.m_flX * flShare ), static_cast<float>( bin.m_flY * flShare ), static_cast<float>( bin.m_flZ * flShare ) }, bin.m_flAmount } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::InvalidateHeat()
{
	m_bHeatGathered = false;
	BuildHeat();
	update();
}

//-----------------------------------------------------------------------------
// Purpose: The gathered heat projected into cells sized to the spread (so the blur is always
//          a few cells wide, whatever the zoom), blurred, then as colours from clear through
//          blue and yellow to red, on a log scale so the crowded places don't wash out the
//          rest.
//-----------------------------------------------------------------------------
void CDemoPathView::BuildHeat()
{
	m_Heat.clear();
	m_HeatImage = {};
	if ( ( m_nMode != DemoPathMode_t::DEMO_PATH_PRESENCE && m_nMode != DemoPathMode_t::DEMO_PATH_DEATHS ) || width() <= 0 || height() <= 0 )
		return;
	if ( !m_bHeatGathered )
		GatherHeat();
	if ( size() != m_ProjectedFor )
		UpdateProjection();
	m_HeatProjection = m_Projection;
	// The blur's width in pixels, and cells that make it about HEAT_SIGMA_CELLS of them.
	constexpr double HEAT_SIGMA_CELLS = 2.5;
	const double flSigmaPixels = std::max( 0.75, m_flHeatSpread * m_Projection.m_flScale );
	m_flHeatCell = std::max( 1.0, flSigmaPixels / HEAT_SIGMA_CELLS );
	const int nCols = static_cast<int>( std::ceil( width() / m_flHeatCell ) );
	const int nRows = static_cast<int>( std::ceil( height() / m_flHeatCell ) );
	const auto cell = [ nCols ]( int nRow, int nCol ) { return static_cast<std::size_t>( nRow ) * static_cast<std::size_t>( nCols ) + static_cast<std::size_t>( nCol ); };
	m_Heat.assign( static_cast<std::size_t>( nCols ) * static_cast<std::size_t>( nRows ), 0.0f );
	const float flSigma = static_cast<float>( flSigmaPixels / m_flHeatCell );
	const int nRadius = static_cast<int>( std::ceil( flSigma * 3.0f ) );
	// Shared between the four cells around the point, so a spot sits where it was, not on the
	// cell grid. Points just off the edge still warm the cells inside it.
	for ( const HeatPoint_t &point : m_HeatPoints )
	{
		const QPointF at = Project( m_Projection, point.m_vecAt.m_flX, point.m_vecAt.m_flY, point.m_vecAt.m_flZ );
		const double flX = at.x() / m_flHeatCell - 0.5, flY = at.y() / m_flHeatCell - 0.5;
		if ( flX < -nRadius || flY < -nRadius || flX > nCols + nRadius || flY > nRows + nRadius )
			continue;
		const int nCol = static_cast<int>( std::floor( flX ) ), nRow = static_cast<int>( std::floor( flY ) );
		const float fx = static_cast<float>( flX - nCol ), fy = static_cast<float>( flY - nRow );
		const float weights[ 4 ] = { ( 1 - fx ) * ( 1 - fy ), fx * ( 1 - fy ), ( 1 - fx ) * fy, fx * fy };
		for ( int k = 0; k < 4; k++ )
		{
			const int c = std::clamp( nCol + ( k & 1 ), 0, nCols - 1 ), r = std::clamp( nRow + ( k >> 1 ), 0, nRows - 1 );
			m_Heat[ cell( r, c ) ] += point.m_flAmount * weights[ k ];
		}
	}
	// A Gaussian blur, rows then columns, so single samples read as round spots and paths as
	// soft bands.
	std::vector<float> kernel( static_cast<std::size_t>( nRadius ) * 2 + 1 );
	for ( int k = -nRadius; k <= nRadius; k++ )
		kernel[ static_cast<std::size_t>( k + nRadius ) ] = std::exp( -0.5f * static_cast<float>( k * k ) / ( flSigma * flSigma ) );
	std::vector<float> across( m_Heat.size(), 0.0f );
	std::vector<float> spread( m_Heat.size(), 0.0f );
	for ( int nRow = 0; nRow < nRows; nRow++ )
	{
		for ( int nCol = 0; nCol < nCols; nCol++ )
		{
			const float flValue = m_Heat[ cell( nRow, nCol ) ];
			if ( flValue == 0.0f )
				continue;
			for ( int k = std::max( -nRadius, -nCol ); k <= std::min( nRadius, nCols - 1 - nCol ); k++ )
				across[ cell( nRow, nCol + k ) ] += flValue * kernel[ static_cast<std::size_t>( k + nRadius ) ];
		}
	}
	for ( int nRow = 0; nRow < nRows; nRow++ )
	{
		for ( int nCol = 0; nCol < nCols; nCol++ )
		{
			const float flValue = across[ cell( nRow, nCol ) ];
			if ( flValue == 0.0f )
				continue;
			for ( int k = std::max( -nRadius, -nRow ); k <= std::min( nRadius, nRows - 1 - nRow ); k++ )
				spread[ cell( nRow + k, nCol ) ] += flValue * kernel[ static_cast<std::size_t>( k + nRadius ) ];
		}
	}
	const float flMost = spread.empty() ? 0.0f : *std::ranges::max_element( spread );
	if ( flMost <= 0.0f )
		return;
	// Below this share of the log scale it is the blur's tail: left clear.
	constexpr float FAINTEST = 0.02f;
	m_HeatImage = QImage( nCols, nRows, QImage::Format_ARGB32 );
	const auto ramp = []( float flPart ) {
		// Clear, blue, cyan, yellow, red.
		static const QColor stops[] = { QColor( 30, 80, 255, 0 ), QColor( 30, 80, 255, 150 ), QColor( 0, 220, 220, 190 ), QColor( 255, 230, 0, 215 ), QColor( 255, 40, 0, 235 ) };
		const float flAt = std::clamp( flPart, 0.0f, 1.0f ) * 4.0f;
		const int n = std::min( 3, static_cast<int>( flAt ) );
		const float f = flAt - static_cast<float>( n );
		const QColor &a = stops[ n ], &b = stops[ n + 1 ];
		return qRgba( static_cast<int>( a.red() + ( b.red() - a.red() ) * f ), static_cast<int>( a.green() + ( b.green() - a.green() ) * f ), static_cast<int>( a.blue() + ( b.blue() - a.blue() ) * f ), static_cast<int>( a.alpha() + ( b.alpha() - a.alpha() ) * f ) );
	};
	// The log scale spans HEAT_RANGE to one below the hottest cell, whatever the zoom or the
	// cells' size, so the faint edge of a spot fades out instead of ending in a hard ring.
	constexpr float HEAT_RANGE = 150.0f;
	const float flUnit = flMost / HEAT_RANGE;
	const float flTop = std::log1p( HEAT_RANGE );
	for ( int nRow = 0; nRow < nRows; nRow++ )
	{
		auto *pLine = reinterpret_cast<QRgb *>( m_HeatImage.scanLine( nRow ) );
		for ( int nCol = 0; nCol < nCols; nCol++ )
		{
			const float flPart = std::log1p( spread[ cell( nRow, nCol ) ] / flUnit ) / flTop;
			pLine[ nCol ] = flPart > FAINTEST ? ramp( flPart ) : qRgba( 0, 0, 0, 0 );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::SetHeatSpread( float flUnits )
{
	if ( flUnits == m_flHeatSpread )
		return;
	m_flHeatSpread = flUnits;
	InvalidateHeat();
}

//-----------------------------------------------------------------------------
// Purpose: Three points at the paths' middle height, where they were drawn and where they
//          are now, give the affine map between the two pictures.
//-----------------------------------------------------------------------------
QTransform CDemoPathView::Reprojection( const Projection_t &from ) const
{
	if ( size() != m_ProjectedFor )
		UpdateProjection();
	const double flX = ( m_vecMins.m_flX + m_vecMaxs.m_flX ) * 0.5, flY = ( m_vecMins.m_flY + m_vecMaxs.m_flY ) * 0.5, flZ = ( m_vecMins.m_flZ + m_vecMaxs.m_flZ ) * 0.5;
	constexpr double REACH = 1024.0;
	const auto frame = [ & ]( const Projection_t &projection ) {
		const QPointF origin = Project( projection, flX, flY, flZ );
		const QPointF across = Project( projection, flX + REACH, flY, flZ ) - origin;
		const QPointF along = Project( projection, flX, flY + REACH, flZ ) - origin;
		return QTransform( across.x(), across.y(), along.x(), along.y(), origin.x(), origin.y() );
	};
	bool bInvertible = false;
	const QTransform back = frame( from ).inverted( &bInvertible );
	return bInvertible ? back * frame( m_Projection ) : QTransform{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::resizeEvent( QResizeEvent *pEvent )
{
	QWidget::resizeEvent( pEvent );
	ViewChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::mousePressEvent( QMouseEvent *pEvent )
{
	if ( pEvent->button() == Qt::LeftButton )
	{
		double flNearest = 10.0;
		for ( const CutPlane_t &plane : CutPlanes() )
		{
			const double flDistance = ( plane.m_Handle - pEvent->position() ).manhattanLength();
			if ( flDistance < flNearest )
			{
				m_CutDrag = plane;
				flNearest = flDistance;
			}
		}
		if ( m_CutDrag )
		{
			m_CutDragStart = pEvent->position();
			setCursor( Qt::SizeAllCursor );
			return;
		}
	}
	m_DragFrom = pEvent->position().toPoint();
	m_nDragButton = pEvent->button();
	setCursor( m_nDragButton == Qt::RightButton && m_nView == DemoPathView_t::DEMO_PATH_VIEW_ISOMETRIC ? Qt::SizeHorCursor : Qt::ClosedHandCursor );
}

//-----------------------------------------------------------------------------
// Purpose: A drag moves the picture; with the right button, turns the isometric view.
//-----------------------------------------------------------------------------
void CDemoPathView::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( m_CutDrag )
	{
		const CutPlane_t &drag = *m_CutDrag;
		const auto nAxis = static_cast<std::size_t>( drag.m_nAxis );
		const double flDelta = QPointF::dotProduct( pEvent->position() - m_CutDragStart, drag.m_Direction ) /
			QPointF::dotProduct( drag.m_Direction, drag.m_Direction );
		const double flMin = drag.m_bUpper ? m_Cut.m_flMin[ nAxis ] : -CUT_LIMIT;
		const double flMax = drag.m_bUpper ? CUT_LIMIT : m_Cut.m_flMax[ nAxis ];
		const double flValue = std::clamp( drag.m_flValue + flDelta, flMin, flMax );
		emit CutHandleMoved( drag.m_nAxis, drag.m_bUpper, static_cast<int>( std::lround( flValue ) ) );
		return;
	}
	if ( m_nDragButton == Qt::NoButton )
		return;
	const QPoint at = pEvent->position().toPoint();
	const QPoint delta = at - m_DragFrom;
	m_DragFrom = at;
	if ( delta.isNull() )
		return;
	if ( m_nDragButton == Qt::RightButton && m_nView == DemoPathView_t::DEMO_PATH_VIEW_ISOMETRIC )
	{
		SetYaw( m_flYaw + delta.x() * TURN_SPEED );
		return;
	}
	m_Pan += QPointF( delta );
	ViewChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::mouseReleaseEvent( QMouseEvent * )
{
	m_CutDrag.reset();
	m_nDragButton = Qt::NoButton;
	unsetCursor();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathView::mouseDoubleClickEvent( QMouseEvent * )
{
	ResetView();
}

//-----------------------------------------------------------------------------
// Purpose: The wheel zooms about the pointer: what is under it stays under it.
//-----------------------------------------------------------------------------
void CDemoPathView::wheelEvent( QWheelEvent *pEvent )
{
	const double flSteps = pEvent->angleDelta().y() / 120.0;
	if ( flSteps == 0.0 )
		return;
	const double flWas = m_flZoom;
	m_flZoom = std::clamp( m_flZoom * std::pow( ZOOM_STEP, flSteps ), 0.25, 64.0 );
	const double flRatio = m_flZoom / flWas;
	// The offset moves with the pan: keep the point under the pointer where it is.
	const QPointF at = pEvent->position();
	const QPointF offset = m_Projection.m_Offset;
	const QPointF newOffset = at - ( at - offset ) * flRatio;
	UpdateProjection();
	m_Pan += newOffset - m_Projection.m_Offset;
	ViewChanged();
}

//-----------------------------------------------------------------------------
// Purpose: The floors, the heat or the paths (faint, with what has played strong), then the
//          camera or the players where they are now.
//-----------------------------------------------------------------------------
void CDemoPathView::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	painter.fillRect( rect(), palette().color( QPalette::Base ) );
	const xv::demo::CDemo &demo = m_Viewer.GetDemo();
	if ( m_nMode == DemoPathMode_t::DEMO_PATH_CAMERA && !demo.HasCamera() )
	{
		painter.setPen( palette().color( QPalette::PlaceholderText ) );
		painter.drawText( rect().adjusted( 16, 16, -16, -16 ), Qt::AlignCenter | Qt::TextWordWrap,
			demo.IsSourceTv() ? tr( "A SourceTV demo has no camera of its own: the server recorded it, not a player. Every player's path, and the heatmaps, are in the box above." )
							  : tr( "This demo's packets don't say where the camera was." ) );
		return;
	}
	if ( IsShowingRadar() )
	{
		PaintRadar( painter );
	}
	else if ( m_bShowMap && !m_Floor.isNull() )
	{
		// Drawn for another view: warped to fit until it is drawn again, so a turn never
		// leaves the view blank between redraws.
		const QTransform warp = Reprojection( m_FloorProjection );
		painter.save();
		painter.setTransform( warp, true );
		painter.setRenderHint( QPainter::SmoothPixmapTransform, !warp.isIdentity() );
		painter.drawImage( 0, 0, m_Floor );
		painter.restore();
	}
	if ( !m_HeatImage.isNull() )
	{
		painter.save();
		painter.setTransform( Reprojection( m_HeatProjection ), true );
		painter.setRenderHint( QPainter::SmoothPixmapTransform, true );
		painter.drawImage( QRectF( 0, 0, m_HeatImage.width() * m_flHeatCell, m_HeatImage.height() * m_flHeatCell ), m_HeatImage );
		painter.restore();
	}
	const int nTick = m_Viewer.GetTick();
	PaintPaths( painter, nTick );
	painter.setRenderHint( QPainter::Antialiasing );
	PaintCutGrid( painter );
	if ( m_nMode == DemoPathMode_t::DEMO_PATH_CAMERA )
	{
		if ( const xv::demo::View_t *pView = demo.ViewAt( nTick ) )
		{
			const QPointF at = ToWidget( pView->m_vecOrigin );
			const QPointF ahead = Facing( pView->m_vecOrigin, pView->m_vecAngles.m_flY, 18.0 );
			painter.setPen( QPen( palette().color( QPalette::Text ), 2 ) );
			painter.drawLine( at, at + ahead );
			painter.setBrush( palette().color( QPalette::Highlight ) );
			painter.drawEllipse( at, 5.0, 5.0 );
			painter.setPen( palette().color( QPalette::Text ) );
			painter.drawText( QPointF( 8, height() - 8 ), tr( "%1 %2 %3" ).arg( pView->m_vecOrigin.m_flX, 0, 'f', 0 ).arg( pView->m_vecOrigin.m_flY, 0, 'f', 0 ).arg( pView->m_vecOrigin.m_flZ, 0, 'f', 0 ) );
		}
		return;
	}
	// The players where they are now, each facing its way.
	for ( const xv::demo::PlayerState_t &player : m_Viewer.GetPlayers().At( static_cast<double>( nTick ) ) )
	{
		if ( !player.m_bAlive || player.m_nTeam < 2 || ( m_nTeam != 0 && player.m_nTeam != m_nTeam ) || !IsInCut( player.m_vecOrigin ) )
			continue;
		const QPointF at = ToWidget( player.m_vecOrigin );
		const double flYaw = player.m_flYaw * std::numbers::pi / 180.0;
		painter.setPen( QPen( QColor( 0, 0, 0, 160 ), 1 ) );
		painter.setBrush( DemoTeamColor( player.m_nTeam ) );
		painter.drawEllipse( at, 3.5, 3.5 );
		painter.setPen( QPen( DemoTeamColor( player.m_nTeam ), 1.5 ) );
		painter.drawLine( at, at + QPointF( std::cos( flYaw ) * 8.0, -std::sin( flYaw ) * 8.0 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The Cut button: for each axis a switch and the two values kept between, starting
//          at what the paths cover.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildPathCut()
{
	auto *pButton = new QToolButton;
	pButton->setObjectName( QStringLiteral( "demoPathCut" ) );
	pButton->setText( tr( "Cut" ) );
	pButton->setToolTip( tr( "Draw only what lies between two heights (Z), or two places across (X) or along (Y): one floor of a building, one part of a map" ) );
	pButton->setPopupMode( QToolButton::InstantPopup );
	auto *pMenu = new QMenu( pButton );
	auto *pPanel = new QWidget;
	auto *pGrid = new QGridLayout( pPanel );
	pGrid->setContentsMargins( 8, 6, 8, 6 );
	pGrid->addWidget( new QLabel( tr( "From" ) ), 0, 1 );
	pGrid->addWidget( new QLabel( tr( "To" ) ), 0, 2 );
	pGrid->addWidget( new QLabel( tr( "Grid" ) ), 0, 3 );
	const std::array<double, 3> mins = { m_pPath->GetMins().m_flX, m_pPath->GetMins().m_flY, m_pPath->GetMins().m_flZ };
	const std::array<double, 3> maxs = { m_pPath->GetMaxs().m_flX, m_pPath->GetMaxs().m_flY, m_pPath->GetMaxs().m_flZ };
	struct Row_t
	{
		QCheckBox *m_pOn;
		QSpinBox *m_pFrom, *m_pTo;
	};
	auto rows = std::make_shared<std::array<Row_t, 3>>();
	const auto apply = [ this, rows ] {
		PathCut_t cut;
		for ( std::size_t nAxis = 0; nAxis < 3; nAxis++ )
		{
			const Row_t &row = ( *rows )[ nAxis ];
			cut.m_bOn[ nAxis ] = row.m_pOn->isChecked();
			cut.m_flMin[ nAxis ] = std::min( row.m_pFrom->value(), row.m_pTo->value() );
			cut.m_flMax[ nAxis ] = std::max( row.m_pFrom->value(), row.m_pTo->value() );
		}
		m_pPath->SetCut( cut );
	};
	const char *axes[] = { "X", "Y", "Z" };
	for ( std::size_t nAxis = 0; nAxis < 3; nAxis++ )
	{
		Row_t &row = ( *rows )[ nAxis ];
		row.m_pOn = new QCheckBox( QString::fromLatin1( axes[ nAxis ] ) );
		row.m_pOn->setObjectName( QStringLiteral( "demoPathCut%1" ).arg( QLatin1String( axes[ nAxis ] ) ) );
		row.m_pFrom = new QSpinBox;
		row.m_pTo = new QSpinBox;
		const auto name = QStringLiteral( "demoPathCut%1" ).arg( QLatin1String( axes[ nAxis ] ) );
		row.m_pFrom->setObjectName( name + QStringLiteral( "From" ) );
		row.m_pTo->setObjectName( name + QStringLiteral( "To" ) );
		auto *pShowGrid = new QCheckBox;
		pShowGrid->setObjectName( name + QStringLiteral( "Grid" ) );
		pShowGrid->setAccessibleName( tr( "Show %1 cut grid" ).arg( QLatin1String( axes[ nAxis ] ) ) );
		pShowGrid->setToolTip( nAxis == 2 ? tr( "Show Z cut handles in the isometric view" ) : tr( "Show cut handles in the path view" ) );
		connect( pShowGrid, &QCheckBox::toggled, this, [ this, nAxis ]( bool bShow ) { m_pPath->SetCutGrid( static_cast<int>( nAxis ), bShow ); } );
		for ( QSpinBox *pSpin : { row.m_pFrom, row.m_pTo } )
		{
			pSpin->setRange( -CUT_LIMIT, CUT_LIMIT );
			pSpin->setSingleStep( 16 );
			pSpin->setKeyboardTracking( false );
		}
		row.m_pFrom->setValue( static_cast<int>( std::floor( mins[ nAxis ] ) ) - 16 );
		row.m_pTo->setValue( static_cast<int>( std::ceil( maxs[ nAxis ] ) ) + ( nAxis == 2 ? 128 : 16 ) );
		// A value changed turns its axis on.
		for ( QSpinBox *pSpin : { row.m_pFrom, row.m_pTo } )
		{
			connect( pSpin, &QSpinBox::valueChanged, this, [ apply, pOn = row.m_pOn ] {
				if ( pOn->isChecked() )
					apply();
				else
					pOn->setChecked( true );
			} );
		}
		connect( row.m_pOn, &QCheckBox::toggled, this, apply );
		const int nRow = static_cast<int>( nAxis ) + 1;
		pGrid->addWidget( row.m_pOn, nRow, 0 );
		pGrid->addWidget( row.m_pFrom, nRow, 1 );
		pGrid->addWidget( row.m_pTo, nRow, 2 );
		pGrid->addWidget( pShowGrid, nRow, 3, Qt::AlignCenter );
	}
	connect( m_pPath, &CDemoPathView::CutHandleMoved, this, [ rows ]( int nAxis, bool bUpper, int nValue ) {
		const Row_t &row = ( *rows )[ static_cast<std::size_t>( nAxis ) ];
		const bool bReversed = row.m_pFrom->value() > row.m_pTo->value();
		( bUpper != bReversed ? row.m_pTo : row.m_pFrom )->setValue( nValue );
	} );
	apply();
	auto *pAction = new QWidgetAction( pMenu );
	pAction->setDefaultWidget( pPanel );
	pMenu->addAction( pAction );
	pButton->setMenu( pMenu );
	return pButton;
}

//-----------------------------------------------------------------------------
// Purpose: The paths page: what is shown, which team, and the view.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildPaths()
{
	m_pPath = new CDemoPathView( *this );
	auto *pMode = new QComboBox;
	pMode->setObjectName( QStringLiteral( "demoPathMode" ) );
	if ( m_Demo.HasCamera() )
		pMode->addItem( tr( "The recorder's camera" ), static_cast<int>( DemoPathMode_t::DEMO_PATH_CAMERA ) );
	pMode->addItem( tr( "Every player's path" ), static_cast<int>( DemoPathMode_t::DEMO_PATH_PLAYERS ) );
	pMode->addItem( tr( "Heatmap: Player Paths" ), static_cast<int>( DemoPathMode_t::DEMO_PATH_PRESENCE ) );
	pMode->addItem( tr( "Heatmap: Deaths" ), static_cast<int>( DemoPathMode_t::DEMO_PATH_DEATHS ) );
	pMode->setCurrentIndex( pMode->findData( static_cast<int>( m_pPath->GetMode() ) ) );
	connect( pMode, &QComboBox::activated, this, [ this, pMode ] { m_pPath->SetMode( static_cast<DemoPathMode_t>( pMode->currentData().toInt() ) ); } );
	auto *pTeam = new QComboBox;
	pTeam->setObjectName( QStringLiteral( "demoPathTeam" ) );
	pTeam->addItem( tr( "Both teams" ), 0 );
	for ( const int nTeam : { 2, 3 } )
		pTeam->addItem( QString::fromStdString( xv::demo::TeamName( m_Demo.GetHeader().m_szGameDir, nTeam ) ), nTeam );
	connect( pTeam, &QComboBox::activated, this, [ this, pTeam ] { m_pPath->SetTeam( pTeam->currentData().toInt() ); } );
	auto *pUpTo = new QCheckBox( tr( "Up to the playhead" ) );
	pUpTo->setObjectName( QStringLiteral( "demoPathUpTo" ) );
	pUpTo->setToolTip( tr( "Count the heatmap only up to the playhead (else the whole demo)" ) );
	connect( pUpTo, &QCheckBox::toggled, m_pPath, &CDemoPathView::SetUpToPlayhead );
	// The heatmap's granularity: how far each spot spreads, in world units.
	static constexpr int SPREADS[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256 };
	const auto spreadIndex = []( int nUnits ) {
		const auto *pAt = std::ranges::min_element( SPREADS, {}, [ nUnits ]( int n ) { return std::abs( n - nUnits ); } );
		return static_cast<int>( pAt - std::begin( SPREADS ) );
	};
	auto *pSpreadLabel = new QLabel( tr( "Granularity" ) );
	auto *pSpread = new QSlider( Qt::Horizontal );
	pSpread->setObjectName( QStringLiteral( "demoPathHeatSpread" ) );
	pSpread->setRange( 0, static_cast<int>( std::size( SPREADS ) ) - 1 );
	pSpread->setPageStep( 1 );
	pSpread->setFixedWidth( 110 );
	pSpread->setAccessibleName( tr( "Heatmap granularity" ) );
	auto *pSpreadValue = new QLabel;
	pSpreadValue->setMinimumWidth( pSpreadValue->fontMetrics().horizontalAdvance( QStringLiteral( "256 u" ) ) );
	const auto showSpread = [ this, pSpread, pSpreadValue ] {
		const int nUnits = SPREADS[ pSpread->value() ];
		pSpreadValue->setText( tr( "%1 u" ).arg( nUnits ) );
		pSpread->setToolTip( tr( "How far each heatmap spot spreads: %1 units. Left for fine detail, right for the broad picture." ).arg( nUnits ) );
		m_pPath->SetHeatSpread( static_cast<float>( nUnits ) );
		QSettings{}.setValue( QStringLiteral( "demoViewer/heatSpread" ), nUnits );
	};
	pSpread->setValue( spreadIndex( QSettings{}.value( QStringLiteral( "demoViewer/heatSpread" ), static_cast<int>( m_pPath->GetHeatSpread() ) ).toInt() ) );
	showSpread();
	connect( pSpread, &QSlider::valueChanged, this, showSpread );
	const auto enableHeatControls = [ this, pUpTo, pSpreadLabel, pSpread, pSpreadValue ] {
		const bool bHeat = m_pPath->GetMode() == DemoPathMode_t::DEMO_PATH_PRESENCE || m_pPath->GetMode() == DemoPathMode_t::DEMO_PATH_DEATHS;
		for ( QWidget *pWidget : std::initializer_list<QWidget *>{ pUpTo, pSpreadLabel, pSpread, pSpreadValue } )
			pWidget->setEnabled( bHeat );
	};
	enableHeatControls();
	connect( pMode, &QComboBox::activated, this, enableHeatControls );
	auto *pView = new QComboBox;
	pView->setObjectName( QStringLiteral( "demoPathView" ) );
	pView->addItem( tr( "Top" ), static_cast<int>( DemoPathView_t::DEMO_PATH_VIEW_TOP ) );
	pView->addItem( tr( "Isometric" ), static_cast<int>( DemoPathView_t::DEMO_PATH_VIEW_ISOMETRIC ) );
	pView->setToolTip( tr( "Straight down, or from above at a slant (drag with the right button to turn it); both without perspective" ) );
	connect( pView, &QComboBox::activated, this, [ this, pView ] { m_pPath->SetView( static_cast<DemoPathView_t>( pView->currentData().toInt() ) ); } );
	auto *pBar = new QHBoxLayout;
	pBar->addWidget( pMode );
	pBar->addWidget( pTeam );
	pBar->addWidget( pUpTo );
	pBar->addWidget( pSpreadLabel );
	pBar->addWidget( pSpread );
	pBar->addWidget( pSpreadValue );
	pBar->addStretch( 1 );
	auto *pMap = new QCheckBox( tr( "Map" ) );
	pMap->setObjectName( QStringLiteral( "demoPathMap" ) );
	pMap->setToolTip( tr( "Show the map background" ) );
	pMap->setChecked( m_pPath->IsShowingMap() );
	connect( pMap, &QCheckBox::toggled, m_pPath, &CDemoPathView::SetShowingMap );
	pBar->addWidget( pMap );
	auto *pBackground = new QComboBox;
	pBackground->setObjectName( QStringLiteral( "demoPathBackground" ) );
	pBackground->setAccessibleName( tr( "Top view background" ) );
	pBackground->setToolTip( tr( "Choose the top view background. Maps without a radar use the map floors." ) );
	pBackground->addItem( tr( "Radar" ), true );
	pBackground->addItem( tr( "Map floors" ), false );
	connect( pBackground, &QComboBox::currentIndexChanged, m_pPath, [ this, pBackground ] {
		m_pPath->SetPreferRadar( pBackground->currentData().toBool() );
	} );
	connect( m_pPath, &CDemoPathView::ViewModeChanged, pBackground, [ pBackground ]( DemoPathView_t nView ) {
		pBackground->setEnabled( nView == DemoPathView_t::DEMO_PATH_VIEW_TOP );
	} );
	pBar->addWidget( pBackground );
	pBar->addWidget( BuildPathCut() );
	pBar->addWidget( pView );
	auto *pPage = new QWidget;
	auto *pLayout = new QVBoxLayout( pPage );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addLayout( pBar );
	pLayout->addWidget( m_pPath, 1 );
	connect( m_pWorld, &CDemoWorld::MapFilesMounted, m_pPath, &CDemoPathView::LoadRadar );
	connect( m_pWorld, &CDemoWorld::StatusChanged, m_pPath, [ this ] {
		if ( m_pWorld->IsDrawingCompiled() )
			m_pPath->MapArrived();
	} );
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: The graphs page: which graph, which team's players, and what the graph can't say.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildGraphs()
{
	auto *pGraph = new CDemoGraphView( *this );
	auto *pWhich = new QComboBox;
	pWhich->setObjectName( QStringLiteral( "demoGraphWhich" ) );
	pWhich->addItem( tr( "Players on each team" ), static_cast<int>( DemoGraph_t::DEMO_GRAPH_TEAMS ) );
	pWhich->addItem( tr( "Player scores" ), static_cast<int>( DemoGraph_t::DEMO_GRAPH_SCORE ) );
	pWhich->addItem( tr( "Damage taken" ), static_cast<int>( DemoGraph_t::DEMO_GRAPH_DAMAGE ) );
	auto *pTeam = new QComboBox;
	pTeam->setObjectName( QStringLiteral( "demoGraphTeam" ) );
	pTeam->addItem( tr( "Both teams" ), 0 );
	for ( const int nTeam : { 2, 3 } )
		pTeam->addItem( QString::fromStdString( xv::demo::TeamName( m_Demo.GetHeader().m_szGameDir, nTeam ) ), nTeam );
	pTeam->setToolTip( tr( "Only the players who were on this team at some point" ) );
	auto *pNote = new QLabel;
	pNote->setObjectName( QStringLiteral( "demoGraphNote" ) );
	pNote->setWordWrap( true );
	pNote->setForegroundRole( QPalette::PlaceholderText );
	const auto chosen = [ pGraph, pWhich, pTeam, pNote ] {
		const auto nGraph = static_cast<DemoGraph_t>( pWhich->currentData().toInt() );
		pGraph->SetGraph( nGraph );
		pTeam->setEnabled( nGraph != DemoGraph_t::DEMO_GRAPH_TEAMS );
		switch ( nGraph )
		{
			case DemoGraph_t::DEMO_GRAPH_TEAMS:
				pNote->setText( tr( "Solid: players on the team. Dashed: those of them alive." ) );
				break;
			case DemoGraph_t::DEMO_GRAPH_SCORE:
				pNote->setText( tr( "The score the scoreboard showed, sampled every half second." ) );
				break;
			case DemoGraph_t::DEMO_GRAPH_DAMAGE:
				pNote->setText( tr( "Health each player lost while alive, added up (big drops many players had at once, the map setting health, left out). Demos record players' health, not who hurt them, so damage dealt can't be shown." ) );
				break;
		}
	};
	connect( pWhich, &QComboBox::currentIndexChanged, this, chosen );
	connect( pTeam, &QComboBox::currentIndexChanged, this, [ pGraph, pTeam ] { pGraph->SetTeam( pTeam->currentData().toInt() ); } );
	chosen();
	auto *pBar = new QHBoxLayout;
	pBar->addWidget( pWhich );
	pBar->addWidget( pTeam );
	pBar->addWidget( pNote, 1 );
	auto *pPage = new QWidget;
	auto *pLayout = new QVBoxLayout( pPage );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addLayout( pBar );
	pLayout->addWidget( pGraph, 1 );
	return pPage;
}

//-----------------------------------------------------------------------------
// Purpose: The summary beside the pages, the timeline and its controls under both.
//-----------------------------------------------------------------------------
CDemoViewer::CDemoViewer( const QString &szName, xv::demo::CDemo demo, const std::filesystem::path &disk, bool bFromGame, xv::demo::CPlayers players, std::shared_ptr<const xv::demo::CContents> pContents, QWidget *pParent )
	: QWidget( pParent ),
	  m_Demo( std::move( demo ) ),
	  m_Players( std::move( players ) ),
	  m_pContents( std::move( pContents ) ),
	  m_szName( szName ),
	  m_Disk( disk ),
	  m_bFromGame( bFromGame )
{
	setFocusPolicy( Qt::StrongFocus );
	for ( const auto &frame : m_Demo.GetFrames() )
	{
		if ( frame.m_nKind == xv::demo::FrameKind_t::FRAME_KIND_PACKET )
			m_PacketTicks.push_back( std::clamp( frame.m_nTick, 0, m_Demo.GetLastTick() ) );
	}
	std::stable_sort( m_PacketTicks.begin(), m_PacketTicks.end() );
	IndexConnections();
	m_pSound = std::make_unique<CDemoSound>( m_pContents.get(), m_Players, [ this ]( int nEntity, int nTick ) { return IsPlayerMuted( nEntity, nTick ); } );

	m_pPages = new QTabWidget;
	m_pPages->setObjectName( QStringLiteral( "demoPages" ) );
	m_pPages->addTab( BuildWorld(), tr( "World" ) );
	m_pPages->addTab( BuildPaths(), tr( "Paths" ) );
	m_pPages->addTab( BuildGraphs(), tr( "Graphs" ) );
	QWidget *pSide = BuildSide();
	connect( &CPreferences::Instance(), &CPreferences::MutedDemoPlayersChanged, this, &CDemoViewer::UpdatePlayerMutes );
	// A demo with no camera and no players opens its side on what it does have: the summary
	// (with its frames), the commands.
	if ( !m_Demo.HasCamera() && m_Players.IsEmpty() )
	{
		ShowSidePage( QStringLiteral( "summary" ) );
		if ( !m_Demo.GetCommands().empty() )
		{
			ShowSidePage( QStringLiteral( "commands" ) );
		}
	}
	connect( m_pWorld, &CDemoWorld::LogicMapRead, this, [ this ] { m_pSound->SetMap( &m_pWorld->GetLogicMap() ); } );
	connect( m_pWorld, &CDemoWorld::MapFilesMounted, this, &CDemoViewer::UseMapFiles );
	connect( this, &CDemoViewer::TickChanged, this, [ this ] {
		m_pWorld->Redraw();
		UpdateSound();
		if ( !IsPlaying() || !m_ListClock.isValid() || m_ListClock.elapsed() >= LIST_INTERVAL )
		{
			UpdatePlayerList();
			UpdateLogs();
		}
	} );
	BuildFeed();

	auto *pSplitter = new QSplitter( Qt::Horizontal );
	pSplitter->setObjectName( QStringLiteral( "demoSplitter" ) );
	pSplitter->addWidget( pSide );
	pSplitter->addWidget( m_pPages );
	pSplitter->setStretchFactor( 0, 0 );
	pSplitter->setStretchFactor( 1, 1 );

	m_pTimeline = new CDemoTimeline( *this );
	m_pPlay = new QPushButton( tr( "Play" ) );
	m_pPlay->setObjectName( QStringLiteral( "demoPlay" ) );
	m_pPlay->setCheckable( true );
	m_pPlay->setToolTip( tr( "Run the playhead at the demo's own speed (Space)" ) );
	connect( m_pPlay, &QPushButton::toggled, this, &CDemoViewer::SetPlaying );
	m_pSpeed = new QComboBox;
	m_pSpeed->setObjectName( QStringLiteral( "demoSpeed" ) );
	for ( const double flSpeed : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 } )
	{
		m_pSpeed->addItem( tr( "%1×" ).arg( flSpeed ), flSpeed );
	}
	m_pSpeed->setCurrentIndex( m_pSpeed->findData( 1.0 ) );
	m_pSpeed->setToolTip( tr( "How fast Play runs" ) );
	// The clock starts over at the playhead, so a change of speed applies from here on.
	connect( m_pSpeed, &QComboBox::currentIndexChanged, this, [ this ] {
		m_flPlayTick = m_nTick;
		m_PlayClock.restart();
	} );
	m_pTime = new QPushButton;
	m_pTime->setObjectName( QStringLiteral( "demoTime" ) );
	m_pTime->setFlat( true );
	m_pTime->setToolTip( tr( "Go to a tick, time or packet frame (Ctrl+G)" ) );
	connect( m_pTime, &QPushButton::clicked, this, &CDemoViewer::AskGoTo );
	auto *pGoTo = new QShortcut( QKeySequence( Qt::CTRL | Qt::Key_G ), this );
	pGoTo->setContext( Qt::WidgetWithChildrenShortcut );
	connect( pGoTo, &QShortcut::activated, this, &CDemoViewer::AskGoTo );
	auto *pSound = new QCheckBox( tr( "Sound" ) );
	pSound->setObjectName( QStringLiteral( "demoSound" ) );
	pSound->setChecked( m_pSound->IsOn() );
	pSound->setToolTip( tr( "Hear the demo's sounds while it plays (at half to twice its speed), from the view's camera, over the map's soundscape, with the voice chat; Mix sets how loud each kind is" ) );
	connect( pSound, &QCheckBox::toggled, this, [ this ]( bool bOn ) {
		m_pSound->SetOn( bOn );
		UpdateSound();
	} );

	auto *pControls = new QHBoxLayout;
	pControls->addWidget( m_pPlay );
	pControls->addWidget( m_pSpeed );
	pControls->addWidget( pSound );
	pControls->addWidget( BuildMix() );
	pControls->addWidget( m_pTime );
	pControls->addStretch( 1 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( pSplitter, 1 );
	pLayout->addWidget( m_pTimeline );
	pLayout->addLayout( pControls );

	m_pPlayTimer = new QTimer( this );
	m_pPlayTimer->setInterval( 16 );
	connect( m_pPlayTimer, &QTimer::timeout, this, &CDemoViewer::Advance );
	UpdateTimeLabel();
}

//-----------------------------------------------------------------------------
// Purpose: The Mix button: the master volume and each kind's (world, music, soundscape,
//          voice), and the sounds muted; all remembered between demos.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildMix()
{
	QSettings settings;
	m_pSound->SetVolume( settings.value( QStringLiteral( "demoViewer/volume/master" ), 70 ).toInt() / 100.0f );
	const std::array<std::pair<DemoSoundKind_t, const char *>, 4> kinds = { { { DemoSoundKind_t::DEMO_SOUND_WORLD, "world" }, { DemoSoundKind_t::DEMO_SOUND_MUSIC, "music" }, { DemoSoundKind_t::DEMO_SOUND_AMBIENT, "ambient" }, { DemoSoundKind_t::DEMO_SOUND_VOICE, "voice" } } };
	for ( const auto &[ nKind, pszKey ] : kinds )
		m_pSound->SetKindVolume( nKind, settings.value( QStringLiteral( "demoViewer/volume/%1" ).arg( QLatin1String( pszKey ) ), 100 ).toInt() / 100.0f );
	for ( const QString &szName : settings.value( QStringLiteral( "demoViewer/muted" ) ).toStringList() )
		m_pSound->SetMuted( szName.toStdString(), true );

	auto *pButton = new QToolButton;
	pButton->setObjectName( QStringLiteral( "demoMix" ) );
	pButton->setText( tr( "Mix" ) );
	pButton->setToolTip( tr( "How loud each kind of sound is, and which sounds aren't heard" ) );
	pButton->setPopupMode( QToolButton::InstantPopup );
	auto *pMenu = new QMenu( pButton );
	auto *pPanel = new QWidget;
	auto *pGrid = new QGridLayout( pPanel );
	pGrid->setContentsMargins( 8, 6, 8, 6 );
	const auto addSlider = [ & ]( int nRow, const QString &szLabel, const char *pszKey, float flNow, std::function<void( float )> pfnSet ) {
		auto *pSlider = new QSlider( Qt::Horizontal );
		pSlider->setObjectName( QStringLiteral( "demoMix_%1" ).arg( QLatin1String( pszKey ) ) );
		pSlider->setRange( 0, 100 );
		pSlider->setValue( static_cast<int>( std::lround( flNow * 100.0f ) ) );
		pSlider->setMinimumWidth( 160 );
		auto *pValue = new QLabel( QStringLiteral( "%1%" ).arg( pSlider->value() ) );
		pValue->setMinimumWidth( QFontMetrics( pValue->font() ).horizontalAdvance( QStringLiteral( "100%" ) ) );
		connect( pSlider, &QSlider::valueChanged, this, [ pValue, pfnSet, pszKey ]( int nValue ) {
			pValue->setText( QStringLiteral( "%1%" ).arg( nValue ) );
			pfnSet( nValue / 100.0f );
			QSettings().setValue( QStringLiteral( "demoViewer/volume/%1" ).arg( QLatin1String( pszKey ) ), nValue );
		} );
		pGrid->addWidget( new QLabel( szLabel ), nRow, 0 );
		pGrid->addWidget( pSlider, nRow, 1 );
		pGrid->addWidget( pValue, nRow, 2 );
	};
	addSlider( 0, tr( "All" ), "master", m_pSound->GetVolume(), [ this ]( float fl ) { m_pSound->SetVolume( fl ); } );
	const QString names[] = { tr( "World" ), tr( "Music" ), tr( "Soundscape" ), tr( "Voice chat" ) };
	for ( std::size_t i = 0; i < kinds.size(); i++ )
	{
		const DemoSoundKind_t nKind = kinds[ i ].first;
		addSlider( static_cast<int>( i ) + 1, names[ i ], kinds[ i ].second, m_pSound->GetKindVolume( nKind ), [ this, nKind ]( float fl ) { m_pSound->SetKindVolume( nKind, fl ); } );
	}
	auto *pAction = new QWidgetAction( pMenu );
	pAction->setDefaultWidget( pPanel );
	pMenu->addAction( pAction );
	pMenu->addSeparator();
	pMenu->addAction( tr( "Mute Sounds…" ), this, &CDemoViewer::ShowMuteSounds );
	pButton->setMenu( pMenu );
	return pButton;
}

//-----------------------------------------------------------------------------
// Purpose: Every sound the demo starts, once each with how often it does, a box to hear it
//          or not, and a filter; the choice is kept by the sound's name for every demo.
//-----------------------------------------------------------------------------
void CDemoViewer::ShowMuteSounds()
{
	if ( !m_pContents )
		return;
	std::map<std::string, int> counts;
	for ( const xv::demo::Sound_t &sound : m_pContents->GetSounds() )
	{
		if ( sound.IsStop() || sound.IsChange() )
			continue;
		std::string szName = m_pContents->SoundName( sound );
		for ( char &ch : szName )
			ch = ch == '\\' ? '/' : static_cast<char>( std::tolower( static_cast<unsigned char>( ch ) ) );
		counts[ szName ]++;
	}
	QDialog dialog( this );
	dialog.setWindowTitle( tr( "Mute Sounds" ) );
	dialog.resize( 560, 520 );
	auto *pFilter = new QLineEdit;
	pFilter->setPlaceholderText( tr( "Find a sound" ) );
	pFilter->setClearButtonEnabled( true );
	auto *pList = new QListWidget;
	pList->setObjectName( QStringLiteral( "demoMuteList" ) );
	for ( const auto &[ szName, nCount ] : counts )
	{
		auto *pItem = new QListWidgetItem( tr( "%1  (%2×)" ).arg( QString::fromStdString( szName ) ).arg( nCount ) );
		pItem->setData( Qt::UserRole, QString::fromStdString( szName ) );
		pItem->setFlags( pItem->flags() | Qt::ItemIsUserCheckable );
		pItem->setCheckState( m_pSound->IsMuted( szName ) ? Qt::Unchecked : Qt::Checked );
		pItem->setToolTip( tr( "Ticked: heard" ) );
		pList->addItem( pItem );
	}
	connect( pFilter, &QLineEdit::textChanged, &dialog, [ pList ]( const QString &szText ) {
		for ( int i = 0; i < pList->count(); i++ )
			pList->item( i )->setHidden( !pList->item( i )->text().contains( szText, Qt::CaseInsensitive ) );
	} );
	connect( pList, &QListWidget::itemChanged, &dialog, [ this ]( QListWidgetItem *pItem ) {
		m_pSound->SetMuted( pItem->data( Qt::UserRole ).toString().toStdString(), pItem->checkState() != Qt::Checked );
		QStringList muted;
		for ( const std::string &szName : m_pSound->GetMuted() )
			muted << QString::fromStdString( szName );
		QSettings().setValue( QStringLiteral( "demoViewer/muted" ), muted );
	} );
	auto *pShown = new QHBoxLayout;
	const auto setShown = [ pList ]( bool bHeard ) {
		for ( int i = 0; i < pList->count(); i++ )
		{
			if ( !pList->item( i )->isHidden() )
				pList->item( i )->setCheckState( bHeard ? Qt::Checked : Qt::Unchecked );
		}
	};
	auto *pHearAll = new QPushButton( tr( "Hear Those Shown" ) );
	auto *pMuteAll = new QPushButton( tr( "Mute Those Shown" ) );
	connect( pHearAll, &QPushButton::clicked, &dialog, [ setShown ] { setShown( true ); } );
	connect( pMuteAll, &QPushButton::clicked, &dialog, [ setShown ] { setShown( false ); } );
	pShown->addWidget( pHearAll );
	pShown->addWidget( pMuteAll );
	pShown->addStretch( 1 );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close );
	connect( pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
	pShown->addWidget( pButtons );
	auto *pLayout = new QVBoxLayout( &dialog );
	pLayout->addWidget( pFilter );
	pLayout->addWidget( pList, 1 );
	pLayout->addLayout( pShown );
	dialog.exec();
}

//-----------------------------------------------------------------------------
// Purpose: What the header says, and what reading the frames found wrong.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildSummary()
{
	const xv::demo::Header_t &header = m_Demo.GetHeader();
	auto *pPanel = new QWidget;
	pPanel->setObjectName( QStringLiteral( "demoSummary" ) );
	pPanel->setMinimumWidth( 320 );
	auto *pForm = new QFormLayout( pPanel );
	m_pSummaryForm = pForm;
	// Fields kept at their size hints (some styles' way) give a wrapped label a narrow column
	// and one line's height.
	pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
	pForm->addRow( tr( "Server" ), ValueLabel( QString::fromUtf8( header.m_szServer ), true ) );
	pForm->addRow( tr( "Recorded by" ), ValueLabel( m_Demo.IsSourceTv() ? tr( "SourceTV (the server)" ) : QString::fromUtf8( header.m_szClient ) ) );
	pForm->addRow( tr( "Map" ), ValueLabel( QString::fromUtf8( header.m_szMap ) ) );
	pForm->addRow( tr( "Game" ), ValueLabel( QString::fromUtf8( header.m_szGameDir ) ) );
	pForm->addRow( tr( "Length" ), ValueLabel( FormatTime( m_Demo.GetLastTick() ) ) );
	const double flInterval = m_Demo.GetTickInterval();
	pForm->addRow( tr( "Ticks" ), ValueLabel( flInterval > 0.0 ? tr( "%L1, %2 a second" ).arg( m_Demo.GetLastTick() ).arg( 1.0 / flInterval, 0, 'f', 1 ) : tr( "%L1" ).arg( m_Demo.GetLastTick() ) ) );
	pForm->addRow( tr( "Frames" ), ValueLabel( tr( "%L1" ).arg( header.m_nFrames > 0 ? header.m_nFrames : static_cast<int>( m_Demo.GetFrames().size() ) ) ) );
	pForm->addRow( tr( "File size" ), ValueLabel( QLocale{}.formattedDataSize( static_cast<qint64>( m_Demo.GetFileSize() ) ) ) );
	pForm->addRow( tr( "Protocol" ), ValueLabel( tr( "demo %1, network %2" ).arg( header.m_nDemoProtocol ).arg( header.m_nNetworkProtocol ) ) );
	if ( !m_Players.IsEmpty() )
	{
		pForm->addRow( tr( "Players" ), ValueLabel( tr( "%L1, in %L2 snapshots" ).arg( m_Players.GetTracks().size() ).arg( m_Players.GetSnapshotCount() ) ) );
	}
	pForm->addRow( BuildFrameKinds() );
	if ( !m_Demo.GetWarning().empty() )
	{
		QLabel *pWarning = ValueLabel( QString::fromStdString( m_Demo.GetWarning() ), true );
		pWarning->setObjectName( QStringLiteral( "demoWarning" ) );
		pForm->addRow( pWarning );
	}
	if ( !m_Players.GetWarning().empty() )
	{
		QLabel *pWarning = ValueLabel( QString::fromStdString( m_Players.GetWarning() ), true );
		pWarning->setObjectName( QStringLiteral( "demoPlayersWarning" ) );
		pForm->addRow( pWarning );
	}
	return pPanel;
}

//-----------------------------------------------------------------------------
// Purpose: The 3D view with its camera's controls over it, and the players listed beside
//          it: a click on a row follows that player in third person, a double click looks
//          through its eyes.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildWorld()
{
	m_pWorld = new CDemoWorld( *this );
	m_pCamera = new QComboBox;
	m_pCamera->setObjectName( QStringLiteral( "demoCamera" ) );
	m_pCamera->addItem( tr( "Free camera" ), static_cast<int>( DemoCamera_t::DEMO_CAMERA_FREE ) );
	if ( m_Demo.HasCamera() )
	{
		m_pCamera->addItem( tr( "The recorder's view" ), static_cast<int>( DemoCamera_t::DEMO_CAMERA_RECORDER ) );
	}
	m_pCamera->addItem( tr( "First person" ), static_cast<int>( DemoCamera_t::DEMO_CAMERA_FIRST_PERSON ) );
	m_pCamera->addItem( tr( "Third person" ), static_cast<int>( DemoCamera_t::DEMO_CAMERA_CHASE ) );
	m_pCamera->setToolTip( tr( "Where the view is seen from. Drag in the view to look around (in third person, around the player; the wheel goes nearer or further), W A S D to fly (E and Q up and down, Shift faster); click a player to follow it; hold Tab for the scoreboard" ) );
	connect( m_pCamera, &QComboBox::activated, this, [ this ] {
		const auto nMode = static_cast<DemoCamera_t>( m_pCamera->currentData().toInt() );
		// Following needs someone to follow: the first player there now.
		if ( ( nMode == DemoCamera_t::DEMO_CAMERA_FIRST_PERSON || nMode == DemoCamera_t::DEMO_CAMERA_CHASE ) && m_pWorld->GetFollowed() == 0 )
		{
			if ( const auto shown = m_pWorld->GetPlayersShown(); !shown.empty() )
			{
				m_pWorld->SetFollowed( shown.front().m_nEntity );
			}
		}
		m_pWorld->SetCameraMode( nMode );
	} );
	auto *pNames = new QCheckBox( tr( "Names" ) );
	pNames->setObjectName( QStringLiteral( "demoNames" ) );
	pNames->setChecked( m_pWorld->IsShowingNames() );
	connect( pNames, &QCheckBox::toggled, m_pWorld, &CDemoWorld::SetShowingNames );
	auto *pThrough = new QCheckBox( tr( "Players through walls" ) );
	pThrough->setObjectName( QStringLiteral( "demoThroughWalls" ) );
	pThrough->setToolTip( tr( "Outline every player, whatever stands between it and the camera" ) );
	connect( pThrough, &QCheckBox::toggled, m_pWorld, &CDemoWorld::SetSeeingThroughWalls );
	// What of the map is drawn: switches for seeing across big or dark maps.
	auto *pLook = new QToolButton;
	pLook->setObjectName( QStringLiteral( "demoLook" ) );
	pLook->setText( tr( "View" ) );
	pLook->setToolTip( tr( "What of the map is drawn: its fog, its 3D skybox, its particles, its light" ) );
	pLook->setPopupMode( QToolButton::InstantPopup );
	auto *pLookMenu = new QMenu( pLook );
	const auto addSwitch = [ & ]( const QString &szText, const char *pszName, const QString &szTip, bool bOn, auto pfnSet ) {
		QAction *pAction = pLookMenu->addAction( szText );
		pAction->setObjectName( QString::fromLatin1( pszName ) );
		pAction->setToolTip( szTip );
		pAction->setCheckable( true );
		pAction->setChecked( bOn );
		connect( pAction, &QAction::toggled, m_pWorld, pfnSet );
	};
	pLookMenu->setToolTipsVisible( true );
	addSwitch( tr( "Fog" ), "demoFog", tr( "The fog the demo has (its env_fog_controller as networked, else the map's); off to see across a big map" ), m_pWorld->IsShowingFog(), &CDemoWorld::SetShowingFog );
	addSwitch( tr( "3D Skybox" ), "demoSkybox3D", tr( "The map's 3D skybox behind the world; off, the 2D sky alone" ), m_pWorld->IsShowingSkybox3D(), &CDemoWorld::SetShowingSkybox3D );
	addSwitch( tr( "Particles" ), "demoParticles", tr( "The demo's particle effects: info_particle_systems as the demo switches them on and off, and the effects the server dispatched" ), m_pWorld->IsShowingParticles(), &CDemoWorld::SetShowingParticles );
	addSwitch( tr( "Only What Can Be Seen" ), "demoVisibility", tr( "Draw only the parts of the map the camera's place can see, as the game does (the compiler's visibility); off to see the whole map from anywhere" ), m_pWorld->IsUsingVisibility(), &CDemoWorld::SetUsingVisibility );
	addSwitch( tr( "Point Entities" ), "demoEntitySprites", tr( "Show point-entity sprites such as glows and light halos" ), m_pWorld->IsShowingEntitySprites(), &CDemoWorld::SetShowingEntitySprites );
	addSwitch( tr( "View Model" ), "demoViewModel", tr( "Show the held weapon in first person" ), m_pWorld->IsShowingViewModel(), &CDemoWorld::SetShowingViewModel );
	addSwitch( tr( "Sprays" ), "demoSprays", tr( "Players' sprays where they put them, from the game's downloaded copies of them (download/user_custom)" ), m_pWorld->IsShowingSprays(), &CDemoWorld::SetShowingSprays );
	addSwitch( tr( "Fullbright" ), "demoFullbright", tr( "Every face at full light, without the map's lightmaps" ), m_pWorld->IsFullbright(), &CDemoWorld::SetFullbright );
	pLook->setMenu( pLookMenu );
	auto *pFind = new QPushButton( tr( "Find Players" ) );
	pFind->setObjectName( QStringLiteral( "demoFindPlayers" ) );
	pFind->setToolTip( tr( "Put the free camera where it sees the players there now (F in the view)" ) );
	connect( pFind, &QPushButton::clicked, m_pWorld, &CDemoWorld::LookAtPlayers );
	auto *pHud = new QCheckBox( tr( "HUD" ) );
	pHud->setObjectName( QStringLiteral( "demoHud" ) );
	pHud->setChecked( m_bHud );
	pHud->setToolTip( tr( "Draw the followed player's HUD over the picture, in any camera (the recorder's in a free camera when no one is followed): health, armour, ammunition, money, the round's clock, chat, the kill feed and what the server showed (the logs beside say the same)" ) );
	connect( pHud, &QCheckBox::toggled, this, &CDemoViewer::SetShowingHud );
	m_pRound = new QLabel;
	m_pRound->setObjectName( QStringLiteral( "demoRound" ) );
	auto *pBar = new QHBoxLayout;
	pBar->addWidget( m_pCamera );
	pBar->addWidget( pFind );
	pBar->addWidget( pNames );
	pBar->addWidget( pThrough );
	pBar->addWidget( pHud );
	pBar->addWidget( pLook );
	pBar->addStretch( 1 );
	pBar->addWidget( m_pRound );
	auto *pView = new QWidget;
	auto *pViewLayout = new QVBoxLayout( pView );
	pViewLayout->setContentsMargins( 0, 0, 0, 0 );
	pViewLayout->addLayout( pBar );
	pViewLayout->addWidget( m_pWorld, 1 );

	connect( m_pWorld, &CDemoWorld::CameraModeChanged, this, &CDemoViewer::UpdateCameraControls );
	connect( m_pWorld, &CDemoWorld::FollowedChanged, this, &CDemoViewer::UpdateCameraControls );
	return pView;
}

//-----------------------------------------------------------------------------
// Purpose: The players at the playhead, a row each: a click on a row follows that player in
//          third person, a double click looks through its eyes; a click on a heading sorts by
//          it (the game's own scoreboard is Tab in the view).
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildPlayers()
{
	m_pPlayerList = ListTable( { tr( "Player" ), tr( "Team" ), tr( "Health" ), tr( "Armour" ), tr( "Money" ), tr( "Score" ), tr( "Deaths" ), tr( "Ping" ), tr( "Steam ID" ), tr( "On server" ) } );
	m_pPlayerList->setObjectName( QStringLiteral( "demoPlayers" ) );
	// Every column can be dragged wider or narrower; they start fitted to what they hold.
	m_pPlayerList->horizontalHeader()->setSectionResizeMode( QHeaderView::Interactive );
	m_pPlayerList->horizontalHeader()->setStretchLastSection( true );
	const std::vector<xv::demo::PlayerTrack_t> &tracks = m_Players.GetTracks();
	m_pPlayerList->setRowCount( static_cast<int>( tracks.size() ) );
	for ( int i = 0; i < static_cast<int>( tracks.size() ); i++ )
	{
		for ( int nColumn = 0; nColumn < PLAYER_COLUMN_COUNT; nColumn++ )
		{
			m_pPlayerList->setItem( i, nColumn, new CSortItem );
			if ( nColumn == PLAYER_COLUMN_NAME )
			{
				m_pPlayerList->item( i, nColumn )->setData( ROLE_TRACK, i );
			}
			if ( nColumn >= PLAYER_COLUMN_HEALTH && nColumn != PLAYER_COLUMN_STEAM_ID )
			{
				m_pPlayerList->item( i, nColumn )->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
			}
		}
	}
	// Unsorted until a heading is clicked: the slots' order.
	m_pPlayerList->horizontalHeader()->setSortIndicator( -1, Qt::AscendingOrder );
	m_pPlayerList->setSortingEnabled( true );
	m_pPlayerList->horizontalHeader()->setSortIndicatorShown( true );
	connect( m_pPlayerList, &QTableWidget::cellClicked, this, [ this ]( int nRow ) {
		m_pWorld->SetFollowed( m_Players.GetTracks()[ TrackOfRow( nRow ) ].m_nEntity );
		if ( m_pWorld->GetCameraMode() != DemoCamera_t::DEMO_CAMERA_FIRST_PERSON )
		{
			m_pWorld->SetCameraMode( DemoCamera_t::DEMO_CAMERA_CHASE );
		}
	} );
	connect( m_pPlayerList, &QTableWidget::cellDoubleClicked, this, [ this ]( int nRow ) {
		m_pWorld->SetFollowed( m_Players.GetTracks()[ TrackOfRow( nRow ) ].m_nEntity );
		m_pWorld->SetCameraMode( DemoCamera_t::DEMO_CAMERA_FIRST_PERSON );
	} );
	UpdatePlayerList();
	m_pPlayerList->resizeColumnsToContents();
	m_pPlayerList->setColumnWidth( PLAYER_COLUMN_NAME, std::max( m_pPlayerList->columnWidth( PLAYER_COLUMN_NAME ), 140 ) );
	m_pPlayerList->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pPlayerList, &QWidget::customContextMenuRequested, this, &CDemoViewer::ShowPlayerMenu );

	return m_pPlayerList;
}

//-----------------------------------------------------------------------------
// Purpose: Beside the pages: the summary, the players, the logs of what the demo put on
//          screens, the console's commands, the voices and the map's logic, a tab each.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildSide()
{
	m_pSide = new QSplitter( Qt::Vertical );
	m_pSide->setObjectName( QStringLiteral( "demoSide" ) );
	m_pSide->setChildrenCollapsible( false );
	m_pSide->installEventFilter( this );
	// The splitter over a stretch: with every group folded it shrinks to their tab bars and
	// they stay at the top.
	auto *pSideBox = new QWidget;
	pSideBox->setMinimumWidth( 320 );
	auto *pSideLayout = new QVBoxLayout( pSideBox );
	pSideLayout->setContentsMargins( 0, 0, 0, 0 );
	pSideLayout->setSpacing( 0 );
	pSideLayout->addWidget( m_pSide, 1 );
	pSideLayout->addStretch( 0 );
	for ( std::size_t nGroup = 0; nGroup < m_SideGroups.size(); nGroup++ )
	{
		auto *pGroup = new QTabWidget;
		pGroup->setObjectName( QStringLiteral( "demoSide%1" ).arg( nGroup ) );
		pGroup->setDocumentMode( true );
		pGroup->tabBar()->setContextMenuPolicy( Qt::CustomContextMenu );
		connect( pGroup->tabBar(), &QWidget::customContextMenuRequested, this, [ this, pGroup ]( const QPoint &point ) { ShowSideMenu( pGroup, point ); } );
		m_SideGroups[ nGroup ] = pGroup;
		m_pSide->addWidget( pGroup );
		auto *pFold = new QToolButton;
		pFold->setObjectName( QStringLiteral( "demoSideFold%1" ).arg( nGroup ) );
		pFold->setAutoRaise( true );
		pFold->setCheckable( true );
		pGroup->setCornerWidget( pFold );
		m_SideButtons[ nGroup ] = pFold;
		const QString szKey = QStringLiteral( "demoViewer/side/collapsed/%1" ).arg( nGroup );
		pFold->setChecked( QSettings().value( szKey, false ).toBool() );
		connect( pFold, &QToolButton::toggled, this, [ this, nGroup, szKey ]( bool bCollapsed ) {
			SetSideCollapsed( static_cast<int>( nGroup ), bCollapsed );
			QSettings().setValue( szKey, bCollapsed );
		} );
	}
	m_pSide->setStretchFactor( SIDE_TOP, 0 );
	m_pSide->setStretchFactor( SIDE_MIDDLE, 2 );
	m_pSide->setStretchFactor( SIDE_BOTTOM, 1 );
	const auto addLog = [ & ]( CDemoLog *&pLog, const char *pszName, const QString &szTip ) {
		pLog = new CDemoLog;
		pLog->setObjectName( QString::fromLatin1( pszName ) );
		pLog->setToolTip( szTip );
		connect( pLog, &CDemoLog::TickPicked, this, &CDemoViewer::SetTick );
		return pLog;
	};
	m_SidePages = {
		{ "summary", tr( "Summary" ), BuildSummary(), SIDE_TOP },
		{ "players", tr( "Players" ), BuildPlayers(), SIDE_MIDDLE },
		{ "kills", tr( "Kills" ), addLog( m_pKillLog, "demoKills", tr( "Who killed whom, with what, up to the playhead" ) ), SIDE_MIDDLE },
		{ "messages", tr( "Messages" ), addLog( m_pMessageLog, "demoMessages", tr( "HUD messages, hints, menus, the server's dialogs, centre and console text up to the playhead. A SourceTV demo has only what went to everyone; a player's own demo has what went to that player" ) ), SIDE_MIDDLE },
		{ "voice", tr( "Voice" ), BuildVoice(), SIDE_MIDDLE },
		{ "chat", tr( "Chat" ), addLog( m_pChatLog, "demoChat", tr( "Chat, radio and notices up to the playhead; click a time to go there" ) ), SIDE_BOTTOM },
		{ "commands", tr( "Console Commands (%1)" ).arg( m_Demo.GetCommands().size() ), BuildCommands(), SIDE_BOTTOM },
		{ "logic", tr( "Map Logic" ), addLog( m_pLogicLog, "demoLogic", tr( "Guesses: for each named map entity that moved, came, went or was shown or hidden, the map's outputs that could have done it, the likely first (what the demo shows backs them: a player in the trigger, by the button, a round starting, the source breaking or stopping). Outputs aren't recorded: these are inferences from the map's entity lump, which a server's Stripper config may have changed. Click a line to see it in the view" ) ), SIDE_BOTTOM },
	};
	// Where each page was put last time.
	const QSettings settings;
	for ( SidePage_t &page : m_SidePages )
	{
		const int nGroup = settings.value( QStringLiteral( "demoViewer/side/%1" ).arg( QLatin1String( page.m_pszKey ) ), page.m_nGroup ).toInt();
		page.m_nGroup = std::clamp( nGroup, 0, static_cast<int>( SIDE_GROUPS ) - 1 );
	}
	FillSideGroups();
	connect( m_pLogicLog, &CDemoLog::ItemPicked, this, [ this ]( int nItem ) { ShowGuess( static_cast<std::size_t>( nItem ) ); } );
	connect( m_pWorld, &CDemoWorld::LogicMapRead, this, &CDemoViewer::BuildLogic );
	UpdateCameraControls();
	UpdatePlayerList();
	return pSideBox;
}

//-----------------------------------------------------------------------------
// Purpose: Each part's tabs in the pages' order; a part without any hides.
//-----------------------------------------------------------------------------
void CDemoViewer::FillSideGroups()
{
	std::array<QWidget *, SIDE_GROUPS> current{};
	for ( std::size_t nGroup = 0; nGroup < m_SideGroups.size(); nGroup++ )
	{
		current[ nGroup ] = m_SideGroups[ nGroup ]->currentWidget();
		while ( m_SideGroups[ nGroup ]->count() > 0 )
			m_SideGroups[ nGroup ]->removeTab( 0 );
	}
	for ( const SidePage_t &page : m_SidePages )
		m_SideGroups[ static_cast<std::size_t>( page.m_nGroup ) ]->addTab( page.m_pWidget, page.m_szTitle );
	for ( std::size_t nGroup = 0; nGroup < m_SideGroups.size(); nGroup++ )
	{
		QTabWidget *pGroup = m_SideGroups[ nGroup ];
		if ( current[ nGroup ] && pGroup->indexOf( current[ nGroup ] ) >= 0 )
			pGroup->setCurrentWidget( current[ nGroup ] );
		pGroup->setVisible( pGroup->count() > 0 );
		SetSideCollapsed( static_cast<int>( nGroup ), m_SideButtons[ nGroup ]->isChecked() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Give the splitter a tab-bar-only height when folded and restore the page area on demand.
//-----------------------------------------------------------------------------
void CDemoViewer::SetSideCollapsed( int nGroup, bool bCollapsed )
{
	QTabWidget *pGroup = m_SideGroups[ nGroup ];
	QToolButton *pButton = m_SideButtons[ nGroup ];
	pButton->setArrowType( bCollapsed ? Qt::RightArrow : Qt::DownArrow );
	const QString szLabel = bCollapsed ? tr( "Expand Tab Group" ) : tr( "Collapse Tab Group" );
	pButton->setToolTip( szLabel );
	pButton->setAccessibleName( szLabel );

	const bool bWasCollapsed = pGroup->maximumHeight() != QWIDGETSIZE_MAX;
	const int nHeight = std::max( pGroup->tabBar()->sizeHint().height(), pButton->sizeHint().height() );
	if ( bCollapsed && !bWasCollapsed && pGroup->isVisible() && m_pSide->isVisible() )
		m_SideExpandedHeights[ nGroup ] = m_pSide->sizes()[ nGroup ];
	pGroup->setMaximumHeight( bCollapsed ? nHeight : QWIDGETSIZE_MAX );
	if ( auto *pPages = pGroup->findChild<QStackedWidget *>( QString{}, Qt::FindDirectChildrenOnly ) )
		pPages->setVisible( !bCollapsed );
	ApplySideSizes( !bCollapsed && bWasCollapsed ? nGroup : -1 );
}

//-----------------------------------------------------------------------------
// Purpose: Folded groups get their tab bars; the open ones share what is left in the
//          proportions they had (a group just unfolded first gets its old height back, at
//          most most of the room); with none open the splitter is held to the tab bars.
//-----------------------------------------------------------------------------
void CDemoViewer::ApplySideSizes( int nOpened )
{
	QList<int> sizes = m_pSide->sizes();
	if ( sizes.size() != SIDE_GROUPS )
		return;
	int nShown = 0, nFolded = 0;
	std::vector<int> open;
	for ( int i = 0; i < SIDE_GROUPS; i++ )
	{
		if ( m_SideGroups[ i ]->isHidden() )
		{
			sizes[ i ] = 0;
			continue;
		}
		nShown++;
		if ( m_SideButtons[ i ]->isChecked() )
		{
			sizes[ i ] = m_SideGroups[ i ]->maximumHeight();
			nFolded += sizes[ i ];
		}
		else
		{
			open.push_back( i );
		}
	}
	const int nHandles = std::max( 0, nShown - 1 ) * m_pSide->handleWidth();
	m_pSide->setMaximumHeight( open.empty() ? nFolded + nHandles : QWIDGETSIZE_MAX );
	// Before the side is shown it has no height to share: done again on its first show.
	if ( !m_pSide->isVisible() || open.empty() )
	{
		m_pSide->setSizes( sizes );
		return;
	}
	const int nRoom = std::max( 0, m_pSide->height() - nHandles - nFolded );
	int nLeft = nRoom;
	if ( nOpened >= 0 && std::ranges::find( open, nOpened ) != open.end() && open.size() > 1 )
	{
		const int nWanted = m_SideExpandedHeights[ nOpened ] > 0 ? m_SideExpandedHeights[ nOpened ] : m_SideGroups[ nOpened ]->sizeHint().height();
		sizes[ nOpened ] = std::clamp( nWanted, std::min( 120, nRoom / 2 ), nRoom * 2 / 3 );
		nLeft -= sizes[ nOpened ];
		std::erase( open, nOpened );
	}
	// Shares by the heights the open groups have now; ones squeezed to nothing (the room a
	// folded group had) count as the splitter's stretch would give them.
	std::vector<double> weights;
	double flTotal = 0.0;
	for ( const int i : open )
	{
		const int nNow = sizes[ i ] > m_SideGroups[ i ]->tabBar()->sizeHint().height() + 8 ? sizes[ i ] : 0;
		weights.push_back( nNow > 0 ? nNow : std::max( 1, m_SideGroups[ i ]->sizeHint().height() ) );
		flTotal += weights.back();
	}
	for ( std::size_t n = 0; n < open.size(); n++ )
	{
		const int nShare = n + 1 == open.size() ? nLeft : static_cast<int>( nLeft * weights[ n ] / flTotal );
		sizes[ open[ n ] ] = nShare;
		nLeft -= nShare;
	}
	m_pSide->setSizes( sizes );
}

//-----------------------------------------------------------------------------
// Purpose: Once the splitter has laid itself out (a show, a resize), the folded groups' room
//          is handed back to the open ones.
//-----------------------------------------------------------------------------
void CDemoViewer::QueueSideSizes()
{
	if ( m_bSideSizesQueued )
		return;
	m_bSideSizesQueued = true;
	QTimer::singleShot( 0, this, [ this ] {
		m_bSideSizesQueued = false;
		ApplySideSizes();
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CDemoViewer::eventFilter( QObject *pWatched, QEvent *pEvent )
{
	if ( pWatched == m_pSide && ( pEvent->type() == QEvent::Show || pEvent->type() == QEvent::Resize ) )
		QueueSideSizes();
	return QWidget::eventFilter( pWatched, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: A page moved to another part of the side, and remembered there.
//-----------------------------------------------------------------------------
void CDemoViewer::MoveSidePage( const QString &szKey, int nGroup )
{
	for ( SidePage_t &page : m_SidePages )
	{
		if ( szKey != QLatin1String( page.m_pszKey ) )
			continue;
		page.m_nGroup = std::clamp( nGroup, 0, static_cast<int>( SIDE_GROUPS ) - 1 );
		QSettings().setValue( QStringLiteral( "demoViewer/side/%1" ).arg( szKey ), page.m_nGroup );
		FillSideGroups();
		m_SideGroups[ static_cast<std::size_t>( page.m_nGroup ) ]->setCurrentWidget( page.m_pWidget );
		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Which part of the side a page is in (0 top, 1 middle, 2 bottom); -1 for none.
//-----------------------------------------------------------------------------
int CDemoViewer::GetSideGroup( const QString &szKey ) const
{
	for ( const SidePage_t &page : m_SidePages )
	{
		if ( szKey == QLatin1String( page.m_pszKey ) )
			return page.m_nGroup;
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: The page brought to the front of its part.
//-----------------------------------------------------------------------------
void CDemoViewer::ShowSidePage( const QString &szKey )
{
	for ( const SidePage_t &page : m_SidePages )
	{
		if ( szKey == QLatin1String( page.m_pszKey ) )
			m_SideGroups[ static_cast<std::size_t>( page.m_nGroup ) ]->setCurrentWidget( page.m_pWidget );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A tab's menu: move it to the top, middle or bottom part; or put every page back.
//-----------------------------------------------------------------------------
void CDemoViewer::ShowSideMenu( QTabWidget *pGroup, const QPoint &point )
{
	const int nTab = pGroup->tabBar()->tabAt( point );
	QWidget *pWidget = nTab >= 0 ? pGroup->widget( nTab ) : nullptr;
	QString szKey;
	for ( const SidePage_t &page : m_SidePages )
	{
		if ( page.m_pWidget == pWidget )
			szKey = QLatin1String( page.m_pszKey );
	}
	QMenu menu( this );
	if ( !szKey.isEmpty() )
	{
		const int nNow = GetSideGroup( szKey );
		const QString names[] = { tr( "Move to the Top" ), tr( "Move to the Middle" ), tr( "Move to the Bottom" ) };
		for ( int nGroup = 0; nGroup < static_cast<int>( SIDE_GROUPS ); nGroup++ )
		{
			QAction *pAction = menu.addAction( names[ nGroup ], this, [ this, szKey, nGroup ] { MoveSidePage( szKey, nGroup ); } );
			pAction->setEnabled( nGroup != nNow );
		}
		menu.addSeparator();
	}
	menu.addAction( tr( "Put Every Tab Back" ), this, [ this ] {
		const std::map<QString, int> defaults = { { QStringLiteral( "summary" ), SIDE_TOP }, { QStringLiteral( "chat" ), SIDE_BOTTOM }, { QStringLiteral( "commands" ), SIDE_BOTTOM }, { QStringLiteral( "logic" ), SIDE_BOTTOM } };
		QSettings settings;
		for ( SidePage_t &page : m_SidePages )
		{
			const auto found = defaults.find( QLatin1String( page.m_pszKey ) );
			page.m_nGroup = found == defaults.end() ? SIDE_MIDDLE : found->second;
			settings.remove( QStringLiteral( "demoViewer/side/%1" ).arg( QLatin1String( page.m_pszKey ) ) );
		}
		FillSideGroups();
	} );
	menu.exec( pGroup->tabBar()->mapToGlobal( point ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildVoice()
{
	m_pVoice = new CDemoVoice( *this );
	return m_pVoice;
}

//-----------------------------------------------------------------------------
// Purpose: The box and the list say what the view's camera is doing, however it came to.
//-----------------------------------------------------------------------------
void CDemoViewer::UpdateCameraControls()
{
	m_pCamera->setCurrentIndex( m_pCamera->findData( static_cast<int>( m_pWorld->GetCameraMode() ) ) );
	const std::vector<xv::demo::PlayerTrack_t> &tracks = m_Players.GetTracks();
	const auto followed = std::ranges::find( tracks, m_pWorld->GetFollowed(), &xv::demo::PlayerTrack_t::m_nEntity );
	const QSignalBlocker blocker( m_pPlayerList );
	if ( followed == tracks.end() )
	{
		m_pPlayerList->clearSelection();
	}
	else
	{
		m_pPlayerList->selectRow( RowOfTrack( static_cast<std::size_t>( followed - tracks.begin() ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Mute the player under the pointer, copy its Steam ID in any of its
//          forms, or its profile opened in the browser (only ever on a click: nothing about
//          the demo's players leaves the machine otherwise).
//-----------------------------------------------------------------------------
void CDemoViewer::ShowPlayerMenu( const QPoint &point )
{
	const int nRow = m_pPlayerList->rowAt( point.y() );
	const std::vector<xv::demo::PlayerTrack_t> &tracks = m_Players.GetTracks();
	if ( nRow < 0 || nRow >= static_cast<int>( tracks.size() ) )
	{
		return;
	}
	const int nEntity = tracks[ TrackOfRow( nRow ) ].m_nEntity;
	const int nTick = m_nTick;
	const xv::demo::PlayerIdentity_t *pIdentity = tracks[ TrackOfRow( nRow ) ].IdentityAt( nTick );
	if ( !pIdentity )
	{
		return;
	}
	QMenu menu( this );
	const bool bMuted = IsPlayerMuted( nEntity, nTick );
	menu.addAction( bMuted ? tr( "Unmute Voice" ) : tr( "Mute Voice" ), this, [ this, nEntity, nTick, bMuted ] { SetPlayerMuted( nEntity, nTick, !bMuted ); } );
	menu.addSeparator();
	const auto copy = [ &menu ]( const QString &szLabel, const QString &szText ) {
		menu.addAction( szLabel, [ szText ] { QGuiApplication::clipboard()->setText( szText ); } );
	};
	copy( tr( "Copy Name" ), QString::fromUtf8( pIdentity->m_szName ) );
	if ( const auto nId64 = xv::demo::SteamId64( *pIdentity ) )
	{
		const QString szUrl = QString::fromStdString( xv::demo::SteamProfileUrl( *nId64 ) );
		copy( tr( "Copy Steam ID (%1)" ).arg( QString::fromStdString( xv::demo::SteamId2( *nId64 ) ) ), QString::fromStdString( xv::demo::SteamId2( *nId64 ) ) );
		copy( tr( "Copy Steam ID3 (%1)" ).arg( QString::fromStdString( xv::demo::SteamId3( *nId64 ) ) ), QString::fromStdString( xv::demo::SteamId3( *nId64 ) ) );
		copy( tr( "Copy SteamID64 (%1)" ).arg( *nId64 ), QString::number( *nId64 ) );
		copy( tr( "Copy Profile Link" ), szUrl );
		menu.addSeparator();
		menu.addAction( tr( "Open Steam Profile" ), [ szUrl ] { QDesktopServices::openUrl( QUrl( szUrl ) ); } );
	}
	menu.exec( m_pPlayerList->viewport()->mapToGlobal( point ) );
}

//-----------------------------------------------------------------------------
// Purpose: Which of the players' tracks a row of the list is, however it is sorted.
//-----------------------------------------------------------------------------
std::size_t CDemoViewer::TrackOfRow( int nRow ) const
{
	const QTableWidgetItem *pName = m_pPlayerList->item( nRow, PLAYER_COLUMN_NAME );
	return pName ? static_cast<std::size_t>( pName->data( ROLE_TRACK ).toInt() ) : 0;
}

//-----------------------------------------------------------------------------
// Purpose: The row a track is on now.
//-----------------------------------------------------------------------------
int CDemoViewer::RowOfTrack( std::size_t nTrack ) const
{
	for ( int nRow = 0; nRow < m_pPlayerList->rowCount(); nRow++ )
	{
		if ( TrackOfRow( nRow ) == nTrack )
		{
			return nRow;
		}
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: Each slot's player at the playhead: its name in its team's colour, greyed when
//          the demo doesn't have it there (dead players keep their team and say so).
//-----------------------------------------------------------------------------
void CDemoViewer::UpdatePlayerList()
{
	m_ListClock.restart();
	const std::vector<xv::demo::PlayerTrack_t> &tracks = m_Players.GetTracks();
	const std::string &szGameDir = m_Demo.GetHeader().m_szGameDir;
	const std::vector<int> speakers = GetSpeakers();
	// Sorting as cells change would move rows under the loop: once, at the end.
	m_pPlayerList->setSortingEnabled( false );
	std::vector<int> rows( tracks.size(), -1 );
	for ( int nRow = 0; nRow < m_pPlayerList->rowCount(); nRow++ )
	{
		rows[ TrackOfRow( nRow ) ] = nRow;
	}
	for ( std::size_t nTrack = 0; nTrack < tracks.size(); nTrack++ )
	{
		const int i = rows[ nTrack ];
		const xv::demo::PlayerTrack_t &track = tracks[ nTrack ];
		const auto state = m_Players.At( track.m_nEntity, m_nTick );
		const xv::demo::PlayerIdentity_t *pIdentity = track.IdentityAt( m_nTick );
		QString szName = pIdentity ? QString::fromUtf8( pIdentity->m_szName ) : tr( "(slot %1)" ).arg( track.m_nEntity );
		if ( pIdentity && pIdentity->m_bSourceTv )
		{
			szName = tr( "%1 (SourceTV)" ).arg( szName );
		}
		else if ( pIdentity && pIdentity->m_bBot )
		{
			szName = tr( "%1 (bot)" ).arg( szName );
		}
		const QString szTeam = state ? QString::fromStdString( xv::demo::TeamName( szGameDir, state->m_nTeam ) ) : QString{};
		const QString szHealth = !state ? QString{} : !state->m_bAlive ? ( state->m_nTeam >= 2 ? tr( "dead" ) : QString{} )
																	   : QString::number( state->m_nHealth );
		const QBrush brush = state && state->m_bAlive && state->m_nTeam >= 2 ? QBrush( DemoTeamColor( state->m_nTeam ) ) : QBrush( palette().color( QPalette::Disabled, QPalette::Text ) );
		const auto set = [ & ]( int nColumn, const QString &szText, const QVariant &sortBy = {} ) {
			QTableWidgetItem *pItem = m_pPlayerList->item( i, nColumn );
			if ( pItem->text() != szText )
			{
				pItem->setText( szText );
			}
			if ( pItem->foreground() != brush )
			{
				pItem->setForeground( brush );
			}
			if ( pItem->data( ROLE_SORT ) != sortBy )
			{
				pItem->setData( ROLE_SORT, sortBy );
			}
		};
		const Score_t score = ScoreOf( track.m_nEntity, m_nTick );
		const auto number = []( const std::optional<int> &n ) { return n ? QString::number( *n ) : QString{}; };
		const auto sortBy = []( const std::optional<int> &n ) { return n ? QVariant( *n ) : QVariant{}; };
		const bool bAlive = state && state->m_bAlive;
		set( PLAYER_COLUMN_NAME, szName );
		set( PLAYER_COLUMN_TEAM, szTeam, state ? QVariant( state->m_nTeam ) : QVariant{} );
		// The dead after the living, however hurt.
		set( PLAYER_COLUMN_HEALTH, szHealth, bAlive ? QVariant( state->m_nHealth ) : state && state->m_nTeam >= 2 ? QVariant( -1 )
																												  : QVariant{} );
		set( PLAYER_COLUMN_ARMOR, bAlive ? number( score.m_nArmor ) : QString{}, bAlive ? sortBy( score.m_nArmor ) : QVariant{} );
		set( PLAYER_COLUMN_MONEY, score.m_nMoney ? QStringLiteral( "$%1" ).arg( *score.m_nMoney ) : QString{}, sortBy( score.m_nMoney ) );
		set( PLAYER_COLUMN_SCORE, number( score.m_nScore ), sortBy( score.m_nScore ) );
		set( PLAYER_COLUMN_DEATHS, number( score.m_nDeaths ), sortBy( score.m_nDeaths ) );
		set( PLAYER_COLUMN_PING, number( score.m_nPing ), sortBy( score.m_nPing ) );
		set( PLAYER_COLUMN_STEAM_ID, !pIdentity ? QString{} : pIdentity->m_bSourceTv ? tr( "SourceTV" )
																					 : QString::fromUtf8( pIdentity->m_szSteamId ) );
		const auto connected = ConnectionTime( track, m_nTick );
		set( PLAYER_COLUMN_CONNECTION, connected ? FormatConnection( *connected ) : QString{},
			connected ? QVariant( connected->first ) : QVariant{} );
		m_pPlayerList->item( i, PLAYER_COLUMN_CONNECTION )->setToolTip( connected && connected->second ? tr( "Connected before the demo began" ) : QString{} );
		QTableWidgetItem *pId = m_pPlayerList->item( i, PLAYER_COLUMN_STEAM_ID );
		const QString szIdTip = SteamIdTip( pIdentity );
		if ( pId->toolTip() != szIdTip )
		{
			pId->setToolTip( szIdTip );
		}
		// Talking: a speaker by the name.
		QTableWidgetItem *pName = m_pPlayerList->item( i, PLAYER_COLUMN_NAME );
		const bool bTalking = std::ranges::find( speakers, track.m_nEntity ) != speakers.end();
		const bool bMuted = IsPlayerMuted( track.m_nEntity, m_nTick );
		const int nVoice = ( bTalking ? 1 : 0 ) + ( bMuted ? 2 : 0 );
		if ( pName->data( Qt::UserRole ).toInt() != nVoice )
		{
			pName->setData( Qt::UserRole, nVoice );
			pName->setIcon( nVoice ? SpeakerIcon( bMuted, bTalking ) : QIcon{} );
			pName->setToolTip( bMuted ? ( bTalking ? tr( "Talking (muted)" ) : tr( "Voice muted" ) ) : ( bTalking ? tr( "Talking" ) : QString{} ) );
		}
	}
	m_pPlayerList->setSortingEnabled( true );
	if ( m_pRound )
	{
		m_pRound->setText( RoundText( m_nTick ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The commands the recorder's console ran, each at its time; a double click puts
//          the playhead there.
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildCommands()
{
	m_pCommands = ListTable( { tr( "Time" ), tr( "Tick" ), tr( "Command" ) } );
	m_pCommands->setObjectName( QStringLiteral( "demoCommands" ) );
	const std::vector<xv::demo::Command_t> &commands = m_Demo.GetCommands();
	m_pCommands->setRowCount( static_cast<int>( commands.size() ) );
	for ( int i = 0; i < static_cast<int>( commands.size() ); i++ )
	{
		const xv::demo::Command_t &command = commands[ static_cast<std::size_t>( i ) ];
		m_pCommands->setItem( i, COMMAND_COLUMN_TIME, new QTableWidgetItem( FormatTime( command.m_nTick ) ) );
		m_pCommands->setItem( i, COMMAND_COLUMN_TICK, new QTableWidgetItem( QString::number( command.m_nTick ) ) );
		m_pCommands->setItem( i, COMMAND_COLUMN_TEXT, new QTableWidgetItem( QString::fromUtf8( command.m_szText ) ) );
	}
	m_pCommands->resizeColumnToContents( COMMAND_COLUMN_TIME );
	m_pCommands->resizeColumnToContents( COMMAND_COLUMN_TICK );
	connect( m_pCommands, &QTableWidget::cellDoubleClicked, this, [ this ]( int nRow ) {
		SetTick( m_Demo.GetCommands()[ static_cast<std::size_t>( nRow ) ].m_nTick );
	} );
	return m_pCommands;
}

//-----------------------------------------------------------------------------
// Purpose: How many frames of each kind the file has, and how much of the file they are
//          (under the summary).
//-----------------------------------------------------------------------------
QWidget *CDemoViewer::BuildFrameKinds()
{
	QTableWidget *pTable = ListTable( { tr( "Kind" ), tr( "Frames" ), tr( "Size" ), tr( "Of the file" ) } );
	pTable->setObjectName( QStringLiteral( "demoFrameKinds" ) );
	for ( int nKind = 1; nKind <= xv::demo::FRAME_KIND_COUNT; nKind++ )
	{
		const auto nFrameKind = static_cast<xv::demo::FrameKind_t>( nKind );
		const int nCount = m_Demo.CountOf( nFrameKind );
		if ( nCount == 0 )
		{
			continue;
		}
		const std::uint64_t nBytes = m_Demo.BytesOf( nFrameKind );
		const int nRow = pTable->rowCount();
		pTable->insertRow( nRow );
		const std::string_view szKind = xv::demo::FrameKindName( nFrameKind );
		pTable->setItem( nRow, 0, new QTableWidgetItem( QString::fromUtf8( szKind.data(), static_cast<qsizetype>( szKind.size() ) ) ) );
		pTable->setItem( nRow, 1, new QTableWidgetItem( tr( "%L1" ).arg( nCount ) ) );
		pTable->setItem( nRow, 2, new QTableWidgetItem( QLocale{}.formattedDataSize( static_cast<qint64>( nBytes ) ) ) );
		pTable->setItem( nRow, 3, new QTableWidgetItem( tr( "%1%" ).arg( m_Demo.GetFileSize() ? 100.0 * static_cast<double>( nBytes ) / static_cast<double>( m_Demo.GetFileSize() ) : 0.0, 0, 'f', 1 ) ) );
	}
	pTable->resizeColumnsToContents();
	// Its few rows whole, in the summary's form, without a scroll bar of its own.
	pTable->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	pTable->setFixedHeight( pTable->horizontalHeader()->sizeHint().height() + pTable->verticalHeader()->length() + 2 * pTable->frameWidth() + 2 );
	return pTable;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::SetTick( int nTick )
{
	nTick = std::clamp( nTick, 0, m_Demo.GetLastTick() );
	if ( nTick == m_nTick )
	{
		return;
	}
	m_PacketStep.reset();
	m_nTick = nTick;
	// Moved by hand while playing: it plays on from there.
	if ( IsPlaying() && std::abs( m_flPlayTick - nTick ) >= 1.0 )
	{
		m_flPlayTick = nTick;
		m_PlayClock.restart();
	}
	UpdateTimeLabel();
	emit TickChanged( m_nTick );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CDemoViewer::GetPlayTick() const
{
	return IsPlaying() ? std::clamp( m_flPlayTick, static_cast<double>( m_nTick ), m_nTick + 1.0 ) : m_nTick;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CDemoViewer::IsPlaying() const
{
	return m_pPlayTimer->isActive();
}

//-----------------------------------------------------------------------------
// Purpose: Play at the end starts over from the beginning.
//-----------------------------------------------------------------------------
void CDemoViewer::SetPlaying( bool bPlaying )
{
	if ( bPlaying == IsPlaying() )
	{
		return;
	}
	if ( bPlaying )
	{
		if ( m_nTick >= m_Demo.GetLastTick() )
		{
			SetTick( 0 );
		}
		m_flPlayTick = m_nTick;
		m_PlayClock.start();
		m_pPlayTimer->start();
	}
	else
	{
		m_pPlayTimer->stop();
		UpdatePlayerList();
		m_pWorld->Redraw();
	}
	UpdateSound();
	m_pPlay->setChecked( bPlaying );
	m_pPlay->setText( bPlaying ? tr( "Pause" ) : tr( "Play" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CDemoViewer::GetSpeed() const
{
	return m_pSpeed->currentData().toDouble();
}

//-----------------------------------------------------------------------------
// Purpose: One of the box's speeds; any other is left as it was.
//-----------------------------------------------------------------------------
void CDemoViewer::SetSpeed( double flSpeed )
{
	if ( const int nIndex = m_pSpeed->findData( flSpeed ); nIndex >= 0 )
	{
		m_pSpeed->setCurrentIndex( nIndex );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The playhead moved by the time gone since the clock last read, not by a tick a
//          timer shot: a slow repaint doesn't slow the demo.
//-----------------------------------------------------------------------------
void CDemoViewer::Advance()
{
	const double flInterval = m_Demo.GetTickInterval() > 0.0 ? m_Demo.GetTickInterval() : FALLBACK_TICK_INTERVAL;
	m_flPlayTick += static_cast<double>( m_PlayClock.restart() ) / 1000.0 / flInterval * GetSpeed();
	const int nLast = m_Demo.GetLastTick();
	if ( m_flPlayTick >= nLast )
	{
		m_flPlayTick = nLast;
		SetTick( nLast );
		SetPlaying( false );
		return;
	}
	SetTick( static_cast<int>( m_flPlayTick ) );
	// Between ticks too: the players move by fractions of one.
	m_pWorld->Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CDemoViewer::FormatTime( int nTick ) const
{
	if ( m_Demo.GetTickInterval() <= 0.0 )
	{
		return tr( "tick %L1" ).arg( nTick );
	}
	const double flSeconds = m_Demo.SecondsAt( nTick );
	const int nMinutes = static_cast<int>( flSeconds / 60.0 );
	return QStringLiteral( "%1:%2" ).arg( nMinutes ).arg( flSeconds - nMinutes * 60.0, 4, 'f', 1, QLatin1Char( '0' ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::UpdateTimeLabel()
{
	QString szTime = tr( "%1 / %2   tick %L3" ).arg( FormatTime( m_nTick ), FormatTime( m_Demo.GetLastTick() ) ).arg( m_nTick );
	if ( m_PacketStep )
		szTime += tr( "   frame %L1" ).arg( *m_PacketStep + 1 );
	m_pTime->setText( szTime );
}

//-----------------------------------------------------------------------------
// Purpose: Step a tick, a second, or a recorded packet and leave playback paused.
//-----------------------------------------------------------------------------
bool CDemoViewer::StepKey( QKeyEvent *pEvent )
{
	const int nKey = pEvent->key();
	if ( nKey != Qt::Key_Left && nKey != Qt::Key_Right && nKey != Qt::Key_Comma && nKey != Qt::Key_Period )
		return false;
	if ( pEvent->modifiers() & ( Qt::AltModifier | Qt::MetaModifier ) )
		return false;
	SetPlaying( false );
	const bool bForward = nKey == Qt::Key_Right || nKey == Qt::Key_Period;
	if ( pEvent->modifiers() & Qt::ControlModifier )
	{
		if ( !m_PacketTicks.empty() )
		{
			const auto first = m_PacketTicks.begin();
			const auto last = m_PacketTicks.end();
			const auto after = std::upper_bound( first, last, m_nTick );
			const bool bAtPacket = after != first && *( after - 1 ) == m_nTick;
			const auto nNext = m_PacketStep ? static_cast<std::ptrdiff_t>( *m_PacketStep ) + ( bForward ? 1 : -1 ) : after - first - ( bForward ? 0 : bAtPacket ? 2
																																								: 1 );
			const auto nPacket = static_cast<std::size_t>( std::clamp<std::ptrdiff_t>( nNext, 0, last - first - 1 ) );
			SetTick( m_PacketTicks[ nPacket ] );
			m_PacketStep = nPacket;
			UpdateTimeLabel();
		}
	}
	else
	{
		const double flInterval = m_Demo.GetTickInterval() > 0.0 ? m_Demo.GetTickInterval() : FALLBACK_TICK_INTERVAL;
		const int nStep = pEvent->modifiers() & Qt::ShiftModifier ? std::max( 1, static_cast<int>( std::lround( 1.0 / flInterval ) ) ) : 1;
		m_PacketStep.reset();
		SetTick( static_cast<int>( std::clamp<qint64>( static_cast<qint64>( m_nTick ) + ( bForward ? nStep : -nStep ), 0, m_Demo.GetLastTick() ) ) );
		UpdateTimeLabel();
	}
	pEvent->accept();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Parse an absolute tick, clock time, or one-based packet number.
//-----------------------------------------------------------------------------
bool CDemoViewer::GoTo( const QString &szInput )
{
	const QString szPosition = szInput.trimmed();
	bool bOk = false;
	int nTick = 0;
	std::optional<std::size_t> packet;
	if ( szPosition.startsWith( u'f', Qt::CaseInsensitive ) )
	{
		const int nFrame = szPosition.mid( 1 ).trimmed().toInt( &bOk );
		if ( !bOk || nFrame <= 0 || static_cast<std::size_t>( nFrame ) > m_PacketTicks.size() )
			return false;
		packet = static_cast<std::size_t>( nFrame - 1 );
		nTick = m_PacketTicks[ *packet ];
	}
	else if ( szPosition.contains( u':' ) )
	{
		static const QRegularExpression clock( QStringLiteral( R"(^(?:(\d+):)?(\d+):([0-5]?\d(?:\.\d+)?)$)" ) );
		const auto match = clock.match( szPosition );
		if ( !match.hasMatch() )
			return false;
		const double flHours = match.captured( 1 ).toDouble();
		const double flMinutes = match.captured( 2 ).toDouble();
		if ( !match.captured( 1 ).isEmpty() && flMinutes >= 60.0 )
			return false;
		const double flInterval = m_Demo.GetTickInterval() > 0.0 ? m_Demo.GetTickInterval() : FALLBACK_TICK_INTERVAL;
		const double flTick = std::round( ( flHours * 3600 + flMinutes * 60 + match.captured( 3 ).toDouble() ) / flInterval );
		if ( !std::isfinite( flTick ) || flTick > m_Demo.GetLastTick() )
			return false;
		nTick = static_cast<int>( flTick );
	}
	else
	{
		nTick = szPosition.toInt( &bOk );
		if ( !bOk || nTick < 0 || nTick > m_Demo.GetLastTick() )
			return false;
	}
	SetPlaying( false );
	SetTick( nTick );
	m_PacketStep = packet;
	UpdateTimeLabel();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Prompt for an absolute position and allow invalid input to be corrected in place.
//-----------------------------------------------------------------------------
void CDemoViewer::AskGoTo()
{
	QDialog dialog( this );
	dialog.setWindowTitle( tr( "Go to" ) );
	auto *pLayout = new QVBoxLayout( &dialog );
	pLayout->addWidget( new QLabel( tr( "Tick, time (12:34 or 1:02:03.5), or packet frame (f 678)" ) ) );
	auto *pInput = new QLineEdit( QString::number( m_nTick ) );
	pInput->setObjectName( QStringLiteral( "demoGoToPosition" ) );
	pLayout->addWidget( pInput );
	auto *pError = new QLabel( tr( "Enter a position within the demo." ) );
	pError->hide();
	pLayout->addWidget( pError );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pLayout->addWidget( pButtons );
	connect( pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
	connect( pButtons, &QDialogButtonBox::accepted, &dialog, [ & ] {
		if ( GoTo( pInput->text() ) )
			dialog.accept();
		else
		{
			pError->show();
			pInput->setFocus();
			pInput->selectAll();
		}
	} );
	pInput->setFocus();
	pInput->selectAll();
	dialog.exec();
}

//-----------------------------------------------------------------------------
// Purpose: Space plays and pauses. Home and End go to the ends of the demo.
//-----------------------------------------------------------------------------
void CDemoViewer::keyPressEvent( QKeyEvent *pEvent )
{
	if ( StepKey( pEvent ) )
		return;
	switch ( pEvent->key() )
	{
		case Qt::Key_Space:
			SetPlaying( !IsPlaying() );
			break;
		case Qt::Key_Home:
			SetTick( 0 );
			break;
		case Qt::Key_End:
			SetTick( m_Demo.GetLastTick() );
			break;
		default:
			QWidget::keyPressEvent( pEvent );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CDemoViewer::GetTabTitle() const
{
	return m_Disk.empty() ? m_szName.section( u'/', -1 ) : QString::fromStdU16String( m_Disk.filename().u16string() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CDemoViewer::GetSessionEntry() const
{
	if ( m_bFromGame )
	{
		return QStringLiteral( "demo\tgame\t%1" ).arg( m_szName );
	}
	if ( m_Disk.empty() )
	{
		return {};
	}
	return QStringLiteral( "demo\tdisk\t%1" ).arg( QString::fromStdU16String( m_Disk.u16string() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::StopWork()
{
	SetPlaying( false );
	m_pSound->StopAll();
	m_pSound->StopSpeech();
	m_pWorld->Stop();
}

//-----------------------------------------------------------------------------
// Purpose: The HUD reads its files now; the feed is read again with the language.
//-----------------------------------------------------------------------------
void CDemoViewer::SetGameFiles( CHudEditor::ReadFile_t pfnRead, xv::audio::CEngine::Output_t nAudio )
{
	m_bGameFiles = true;
	m_nAudio = nAudio;
	if ( m_pWorld->GetMapFiles() )
	{
		UseMapFiles();
	}
	else
	{
		m_pSound->SetFiles( pfnRead, nAudio );
		m_Hud.SetFiles( std::move( pfnRead ) );
	}
	BuildFeed();
	m_pWorld->Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: The game's files as the map's tab has them, its packed files first, as the game
//          reads them with the map loaded: maps carry their own music and HUD pictures.
//-----------------------------------------------------------------------------
void CDemoViewer::UseMapFiles()
{
	const std::shared_ptr<const xv::vfs::CFileSystem> pFiles = m_pWorld->GetMapFiles();
	if ( !m_bGameFiles || !pFiles )
	{
		return;
	}
	const CHudEditor::ReadFile_t pfnRead = [ pFiles ]( const std::string &szFile ) { return pFiles->Read( szFile ); };
	m_pSound->SetFiles( pfnRead, m_nAudio );
	m_Hud.SetFiles( pfnRead );
	m_pWorld->Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::SetShowingHud( bool bShow )
{
	m_bHud = bShow;
	m_pWorld->Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CDemoViewer::PlayerName( int nEntity, int nTick ) const
{
	if ( const xv::demo::PlayerTrack_t *pTrack = m_Players.FindTrack( nEntity ) )
	{
		if ( const xv::demo::PlayerIdentity_t *pIdentity = pTrack->IdentityAt( nTick ) )
			return QString::fromUtf8( pIdentity->m_szName );
	}
	return nEntity ? tr( "(slot %1)" ).arg( nEntity ) : tr( "world" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemoViewer::PlayerTeam( int nEntity, int nTick ) const
{
	const auto state = m_Players.At( nEntity, nTick );
	return state ? state->m_nTeam : 0;
}

//-----------------------------------------------------------------------------
// Purpose: The player resource's arrays (by entity) and the player's own armour and money.
//-----------------------------------------------------------------------------
CDemoViewer::Score_t CDemoViewer::ScoreOf( int nEntity, int nTick ) const
{
	Score_t score;
	if ( !m_pContents )
		return score;
	const auto toInt = []( const std::optional<float> &fl ) { return fl ? std::optional<int>( static_cast<int>( *fl ) ) : std::nullopt; };
	const xv::demo::EntityLife_t *pResource = m_pContents->FindLife( "CCSPlayerResource", nTick );
	if ( !pResource )
		pResource = m_pContents->FindLife( "CPlayerResource", nTick );
	if ( pResource )
	{
		char szIndex[ 8 ];
		std::snprintf( szIndex, sizeof( szIndex ), ".%03d", nEntity );
		score.m_nScore = toInt( m_pContents->StatOf( *pResource, std::string( "m_iScore" ) + szIndex, nTick ) );
		score.m_nDeaths = toInt( m_pContents->StatOf( *pResource, std::string( "m_iDeaths" ) + szIndex, nTick ) );
		score.m_nPing = toInt( m_pContents->StatOf( *pResource, std::string( "m_iPing" ) + szIndex, nTick ) );
	}
	if ( const xv::demo::EntityLife_t *pPlayer = m_pContents->LifeAt( nEntity, nTick ) )
	{
		score.m_nArmor = toInt( m_pContents->StatOf( *pPlayer, "m_ArmorValue", nTick ) );
		score.m_nMoney = toInt( m_pContents->StatOf( *pPlayer, "m_iAccount", nTick ) );
	}
	return score;
}

//-----------------------------------------------------------------------------
// Purpose: In the freeze before a round, the time until it starts; then the round's time
//          less what has gone since it started.
//-----------------------------------------------------------------------------
std::optional<double> CDemoViewer::RoundLeft( double flTick ) const
{
	if ( !m_pContents )
		return std::nullopt;
	const int nTick = static_cast<int>( flTick );
	const xv::demo::EntityLife_t *pRules = m_pContents->FindLife( "CCSGameRulesProxy", nTick );
	if ( !pRules )
		return std::nullopt;
	const auto start = m_pContents->StatOf( *pRules, "m_fRoundStartTime", nTick );
	const auto length = m_pContents->StatOf( *pRules, "m_iRoundTime", nTick );
	if ( !start || !length )
		return std::nullopt;
	const double flNow = m_pContents->ServerTimeAt( flTick );
	if ( m_pContents->StatOf( *pRules, "m_bFreezePeriod", nTick ).value_or( 0.0f ) != 0.0f )
		return std::max( 0.0, *start - flNow );
	return std::max( 0.0, *length - ( flNow - *start ) );
}

//-----------------------------------------------------------------------------
// Purpose: "Round 2 · 28:12 · Terrorists 10, Counter-Terrorists 1", as far as the demo says.
//-----------------------------------------------------------------------------
QString CDemoViewer::RoundText( int nTick ) const
{
	QStringList parts;
	const std::vector<xv::demo::Round_t> &rounds = m_Feed.GetRounds();
	if ( const xv::demo::Round_t *pRound = m_Feed.RoundAt( nTick ) )
	{
		const int nIndex = static_cast<int>( pRound - rounds.data() ) + 1;
		parts << ( pRound->m_nEnd >= 0 && nTick >= pRound->m_nEnd && !pRound->m_szMessage.empty() ? tr( "Round %1 over: %2" ).arg( nIndex ).arg( QString::fromUtf8( xv::demo::StripColors( pRound->m_szMessage ) ) ) : tr( "Round %1" ).arg( nIndex ) );
	}
	if ( const auto left = RoundLeft( nTick ) )
	{
		const int nSeconds = static_cast<int>( std::ceil( *left ) );
		parts << QStringLiteral( "%1:%2" ).arg( nSeconds / 60 ).arg( nSeconds % 60, 2, 10, QLatin1Char( '0' ) );
	}
	if ( m_pContents )
	{
		const int nClass = std::max( m_pContents->FindClass( "CCSTeam" ), m_pContents->FindClass( "CTeam" ) );
		QStringList teams;
		for ( const xv::demo::EntityLife_t &life : m_pContents->GetLives() )
		{
			if ( life.m_nClass != nClass || nClass < 0 || !life.IsThere( nTick ) )
				continue;
			const auto team = m_pContents->StatOf( life, "m_iTeamNum", nTick );
			const auto score = m_pContents->StatOf( life, "m_iScore", nTick );
			if ( team && score && *team >= 2 )
				teams << QStringLiteral( "%1 %2" ).arg( QString::fromStdString( xv::demo::TeamName( m_Demo.GetHeader().m_szGameDir, static_cast<int>( *team ) ) ) ).arg( static_cast<int>( *score ) );
		}
		if ( !teams.isEmpty() )
			parts << teams.join( QStringLiteral( ", " ) );
	}
	return parts.join( QStringLiteral( " \u00b7 " ) );
}

//-----------------------------------------------------------------------------
// Purpose: Resolve a voice to the player who owned its slot at the recorded tick.
//-----------------------------------------------------------------------------
bool CDemoViewer::IsPlayerMuted( int nEntity, int nTick ) const
{
	const auto *pTrack = m_Players.FindTrack( nEntity );
	const auto *pIdentity = pTrack ? pTrack->IdentityAt( nTick ) : nullptr;
	if ( !pIdentity )
		return false;
	if ( const auto nSteamId = xv::demo::SteamId64( *pIdentity ) )
		return CPreferences::Instance().GetMutedDemoPlayers().contains( QString::number( *nSteamId ) );
	return m_MutedPlayers.contains( { nEntity, pIdentity->m_nUserId > 0 ? pIdentity->m_nUserId : -pIdentity->m_nTick } );
}

//-----------------------------------------------------------------------------
// Purpose: Keep account mutes across demos and identify other players within this demo.
//-----------------------------------------------------------------------------
void CDemoViewer::SetPlayerMuted( int nEntity, int nTick, bool bMuted )
{
	const auto *pTrack = m_Players.FindTrack( nEntity );
	const auto *pIdentity = pTrack ? pTrack->IdentityAt( nTick ) : nullptr;
	if ( !pIdentity )
		return;
	if ( const auto nSteamId = xv::demo::SteamId64( *pIdentity ) )
	{
		CPreferences::Instance().SetDemoPlayerMuted( QString::number( *nSteamId ), bMuted );
		return;
	}
	const auto id = std::pair{ nEntity, pIdentity->m_nUserId > 0 ? pIdentity->m_nUserId : -pIdentity->m_nTick };
	if ( bMuted )
		m_MutedPlayers.insert( id );
	else
		m_MutedPlayers.erase( id );
	UpdatePlayerMutes();
}

//-----------------------------------------------------------------------------
// Purpose: Stop voices already playing and refresh the indicators in this tab.
//-----------------------------------------------------------------------------
void CDemoViewer::UpdatePlayerMutes()
{
	m_pSound->ApplyPlayerMutes();
	UpdatePlayerList();
	m_pVoice->UpdateButtons();
	m_pWorld->Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Players speaking at the current playhead.
//-----------------------------------------------------------------------------
std::vector<int> CDemoViewer::GetSpeakers() const
{
	return m_pContents ? xv::demo::SpeakersAt( *m_pContents, m_nTick ) : std::vector<int>{};
}

//-----------------------------------------------------------------------------
// Purpose: The player's health from its track; armour, money, its weapon's clip and what it
//          carries for it from the entities, where the demo has them (a SourceTV demo hasn't
//          a player's ammunition: that goes to the player alone).
//-----------------------------------------------------------------------------
DemoHudMoment_t CDemoViewer::HudMoment( int nPlayer, double flTick ) const
{
	DemoHudMoment_t moment;
	moment.m_flTick = flTick;
	moment.m_nPlayer = nPlayer;
	const int nTick = static_cast<int>( flTick );
	moment.m_flRoundLeft = RoundLeft( flTick );
	moment.m_bOwnMessages = m_Demo.IsSourceTv() || ( nPlayer != 0 && nPlayer == m_Players.GetRecorderEntity() );
	moment.m_Speakers = m_pContents ? xv::demo::SpeakersAt( *m_pContents, nTick ) : std::vector<int>{};
	for ( const int nEntity : moment.m_Speakers )
	{
		if ( IsPlayerMuted( nEntity, nTick ) )
			moment.m_MutedSpeakers.push_back( nEntity );
	}
	if ( nPlayer == 0 )
		return moment;
	if ( const auto state = m_Players.At( nPlayer, flTick ) )
	{
		moment.m_nTeam = state->m_nTeam;
		moment.m_bAlive = state->m_bAlive;
		moment.m_nHealth = state->m_nHealth;
	}
	if ( !m_pContents )
		return moment;
	const xv::demo::EntityLife_t *pPlayer = m_pContents->LifeAt( nPlayer, nTick );
	if ( !pPlayer )
		return moment;
	const auto toInt = []( const std::optional<float> &fl ) { return fl ? std::optional<int>( static_cast<int>( *fl ) ) : std::nullopt; };
	moment.m_nArmor = toInt( m_pContents->StatOf( *pPlayer, "m_ArmorValue", nTick ) );
	moment.m_bHelmet = m_pContents->StatOf( *pPlayer, "m_bHasHelmet", nTick ).value_or( 0.0f ) != 0.0f;
	moment.m_nMoney = toInt( m_pContents->StatOf( *pPlayer, "m_iAccount", nTick ) );
	// The weapon in hand: the player's first EXTRA field is its handle.
	const std::uint32_t nHandle = static_cast<std::uint32_t>( pPlayer->ValueAt( xv::demo::ENTITY_FIELD_EXTRA_0, nTick )[ 0 ] );
	if ( nHandle == xv::demo::ENTITY_NO_HANDLE )
		return moment;
	const xv::demo::EntityLife_t *pWeapon = m_pContents->LifeAt( static_cast<int>( nHandle & xv::demo::ENTITY_HANDLE_INDEX_MASK ), nTick );
	if ( !pWeapon )
		return moment;
	const auto clip = toInt( m_pContents->StatOf( *pWeapon, "m_iClip1", nTick ) );
	const auto type = toInt( m_pContents->StatOf( *pWeapon, "m_iPrimaryAmmoType", nTick ) );
	// What takes no ammunition (a knife: no type) and grenades (the types past the guns')
	// show no ammunition panel, as in the game.
	if ( clip && *clip >= 0 && type && *type > 0 && static_cast<std::size_t>( *type ) < std::size( AMMO_ICONS ) )
	{
		moment.m_nClip = clip;
		moment.m_szAmmoIcon = AMMO_ICONS[ *type ];
		char szName[ 24 ];
		std::snprintf( szName, sizeof( szName ), "m_iAmmo.%03d", *type );
		moment.m_nReserve = toInt( m_pContents->StatOf( *pPlayer, szName, nTick ) );
	}
	return moment;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::PaintHud( QPainter &painter, const QRect &rect, int nPlayer, bool bEyes )
{
	if ( !m_bHud )
		return;
	const double flTick = GetPlayTick();
	const int nTick = static_cast<int>( flTick );
	DemoHudMoment_t moment = HudMoment( nPlayer, flTick );
	moment.m_bCrosshair = bEyes;
	m_Hud.Paint( painter, rect, m_Feed, moment, [ this, nTick ]( int nEntity ) { return PlayerName( nEntity, nTick ); }, [ this, nTick ]( int nEntity ) { return PlayerTeam( nEntity, nTick ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Keep the join and leave boundaries the demo records for each user ID.
//-----------------------------------------------------------------------------
void CDemoViewer::IndexConnections()
{
	if ( !m_pContents )
		return;
	const int nConnect = m_pContents->FindEventKind( "player_connect" );
	const int nClient = m_pContents->FindEventKind( "player_connect_client" );
	const int nLeave = m_pContents->FindEventKind( "player_disconnect" );
	for ( const auto &event : m_pContents->GetEvents() )
	{
		const bool bJoined = event.m_nKind == nConnect || event.m_nKind == nClient;
		if ( event.m_nKind < 0 || ( !bJoined && event.m_nKind != nLeave ) )
			continue;
		const auto *pUser = m_pContents->EventValue( event, "userid" );
		if ( !pUser || pUser->m_flNumber <= 0 )
			continue;
		auto &changes = m_Connections[ static_cast<int>( pUser->m_flNumber ) ];
		if ( changes.empty() || changes.back().second != bJoined )
			changes.emplace_back( event.m_nTick, bJoined );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Use the latest recorded join, or the first userinfo for this continuous occupant.
//          Players present at tick zero have a lower bound on their connection time.
//-----------------------------------------------------------------------------
std::optional<std::pair<int, bool>> CDemoViewer::ConnectionTime( const xv::demo::PlayerTrack_t &track, int nTick ) const
{
	const auto *pIdentity = track.IdentityAt( nTick );
	if ( !pIdentity || pIdentity->m_bSourceTv )
		return std::nullopt;
	const auto *pFirst = pIdentity;
	while ( pFirst != track.m_Identities.data() && pFirst->m_nUserId > 0 &&
		( pFirst - 1 )->m_nUserId == pIdentity->m_nUserId && !( pFirst - 1 )->m_szName.empty() )
		--pFirst;
	int nFrom = pFirst->m_nTick;
	if ( const auto found = m_Connections.find( pIdentity->m_nUserId ); found != m_Connections.end() )
	{
		const auto &changes = found->second;
		const auto after = std::upper_bound( changes.begin(), changes.end(), nTick, []( int nAt, const auto &change ) { return nAt < change.first; } );
		if ( after != changes.begin() )
		{
			const auto [ nEvent, bJoined ] = *( after - 1 );
			if ( bJoined )
				nFrom = nEvent;
			else if ( nEvent >= nFrom )
				return std::nullopt;
		}
	}
	const auto flInterval = m_Demo.GetTickInterval() > 0.0 ? m_Demo.GetTickInterval() : FALLBACK_TICK_INTERVAL;
	return std::pair{ static_cast<int>( std::max( 0, nTick - std::max( 0, nFrom ) ) * flInterval ), nFrom <= 0 };
}

//-----------------------------------------------------------------------------
// Purpose: Show elapsed connection time with a lower-bound mark at sign-on.
//-----------------------------------------------------------------------------
QString CDemoViewer::FormatConnection( const std::pair<int, bool> &time ) const
{
	const int nSeconds = time.first;
	QString szTime = QStringLiteral( "%1:%2" ).arg( nSeconds / 60 % 60, nSeconds >= 3600 ? 2 : 1, 10, QLatin1Char( '0' ) ).arg( nSeconds % 60, 2, 10, QLatin1Char( '0' ) );
	if ( nSeconds >= 3600 )
		szTime.prepend( QString::number( nSeconds / 3600 ) + u':' );
	if ( time.second )
		szTime.prepend( u'≥' );
	return szTime;
}

//-----------------------------------------------------------------------------
// Purpose: Connection times for the players in the scoreboard at the playhead.
//-----------------------------------------------------------------------------
QString CDemoViewer::ScoreboardConnections() const
{
	QStringList lines;
	for ( const auto &track : m_Players.GetTracks() )
	{
		if ( const auto time = ConnectionTime( track, m_nTick ) )
		{
			const QString szName = QString::fromUtf8( track.IdentityAt( m_nTick )->m_szName );
			lines << tr( "%1 — %2" ).arg( szName, FormatConnection( *time ) );
		}
	}
	return lines.join( u'\n' );
}

//-----------------------------------------------------------------------------
// Purpose: Everyone the demo has there at the tick (SourceTV left out), with the scoreboard's
//          numbers, and the teams' names and scores.
//-----------------------------------------------------------------------------
DemoScoreboard_t CDemoViewer::ScoreboardAt( int nTick, int nFollowed ) const
{
	DemoScoreboard_t board;
	const xv::demo::Header_t &header = m_Demo.GetHeader();
	board.m_szServer = QString::fromUtf8( header.m_szServer );
	board.m_szMap = QString::fromUtf8( header.m_szMap );
	for ( int nTeam = 0; nTeam < 4; nTeam++ )
		board.m_TeamNames[ static_cast<std::size_t>( nTeam ) ] = QString::fromStdString( xv::demo::TeamName( header.m_szGameDir, nTeam ) );
	board.m_flRoundLeft = RoundLeft( nTick );
	if ( m_pContents )
	{
		const int nClass = std::max( m_pContents->FindClass( "CCSTeam" ), m_pContents->FindClass( "CTeam" ) );
		for ( const xv::demo::EntityLife_t &life : m_pContents->GetLives() )
		{
			if ( nClass < 0 || life.m_nClass != nClass || !life.IsThere( nTick ) )
				continue;
			const auto team = m_pContents->StatOf( life, "m_iTeamNum", nTick );
			const auto score = m_pContents->StatOf( life, "m_iScore", nTick );
			if ( team && score && *team >= 0 && *team < 4 )
				board.m_TeamScores[ static_cast<std::size_t>( *team ) ] = static_cast<int>( *score );
		}
	}
	const std::vector<int> speakers = GetSpeakers();
	for ( const xv::demo::PlayerTrack_t &track : m_Players.GetTracks() )
	{
		const xv::demo::PlayerIdentity_t *pIdentity = track.IdentityAt( nTick );
		const auto state = m_Players.At( track.m_nEntity, nTick );
		if ( !pIdentity || pIdentity->m_bSourceTv || !state )
			continue;
		DemoScoreRow_t row;
		row.m_nEntity = track.m_nEntity;
		row.m_szName = QString::fromUtf8( pIdentity->m_szName );
		if ( pIdentity->m_bBot )
			row.m_szName = tr( "%1 (bot)" ).arg( row.m_szName );
		row.m_nTeam = state->m_nTeam;
		row.m_bAlive = state->m_bAlive;
		const Score_t score = ScoreOf( track.m_nEntity, nTick );
		row.m_nScore = score.m_nScore;
		row.m_nDeaths = score.m_nDeaths;
		row.m_nPing = score.m_nPing;
		row.m_bTalking = std::ranges::find( speakers, track.m_nEntity ) != speakers.end();
		row.m_bMuted = IsPlayerMuted( track.m_nEntity, nTick );
		row.m_bFollowed = track.m_nEntity == nFollowed;
		board.m_Rows.push_back( std::move( row ) );
	}
	return board;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::PaintScoreboard( QPainter &painter, const QRect &rect, int nFollowed )
{
	m_Hud.PaintScoreboard( painter, rect, ScoreboardAt( m_nTick, nFollowed ) );
}

//-----------------------------------------------------------------------------
// Purpose: The feed in the language the HUD's files have (tokens as written without them),
//          and the three logs' lines: who said what in their team's colour, the kills, and
//          the rest by kind (a dialog repeated as it was is shown once).
//-----------------------------------------------------------------------------
void CDemoViewer::BuildFeed()
{
	if ( !m_pContents )
		return;
	m_Feed = xv::demo::CFeed::Build( *m_pContents, m_Players, [ this ]( std::string_view szToken ) { return m_Hud.Localize( szToken ); }, m_Demo.GetCommands() );
	BuildLogs();
	if ( m_pTimeline )
		m_pTimeline->update();
}

//-----------------------------------------------------------------------------
// Purpose: The three logs' lines from the feed, in the palette's colours (again when the
//          palette changes, so pale text gets a backing on a light scheme and dark text on
//          a dark one).
//-----------------------------------------------------------------------------
void CDemoViewer::BuildLogs()
{
	if ( !m_pContents || !m_pChatLog )
		return;
	const QColor normal = palette().color( QPalette::Text );
	const QColor grey = palette().color( QPalette::PlaceholderText );
	// The logs' background, which every coloured run is checked against.
	const QColor base = m_pChatLog ? m_pChatLog->palette().color( QPalette::Base ) : palette().color( QPalette::Base );
	const auto teamColor = [ & ]( int nEntity, int nTick ) {
		const int nTeam = nEntity ? PlayerTeam( nEntity, nTick ) : 0;
		return nTeam >= 2 ? DemoTeamColor( nTeam ) : normal;
	};
	const auto kindHtml = [ & ]( const QString &szKind ) { return QStringLiteral( "<span style=\"color:%1\">%2</span> " ).arg( grey.name(), szKind.toHtmlEscaped() ); };
	std::vector<CDemoLog::Entry_t> chat, kills, messages;
	std::string szLastDialog;
	for ( const xv::demo::FeedLine_t &line : m_Feed.GetLines() )
	{
		const QColor team = teamColor( line.m_nEntity, line.m_nTick );
		switch ( line.m_nKind )
		{
			case xv::demo::FeedKind_t::FEED_KIND_CHAT:
				chat.push_back( { line.m_nTick, DemoColoredHtml( line.m_szText, team, normal, base ) } );
				break;
			case xv::demo::FeedKind_t::FEED_KIND_RADIO:
				chat.push_back( { line.m_nTick, kindHtml( tr( "radio" ) ) + DemoColoredHtml( line.m_szText, team, QColor( 90, 190, 90 ), base ) } );
				break;
			case xv::demo::FeedKind_t::FEED_KIND_NOTICE:
				chat.push_back( { line.m_nTick, DemoColoredHtml( line.m_szText, team, grey, base ) } );
				break;
			case xv::demo::FeedKind_t::FEED_KIND_CONSOLE:
				messages.push_back( { line.m_nTick, kindHtml( tr( "console" ) ) + DemoColoredHtml( line.m_szText, team, normal, base ) } );
				break;
			case xv::demo::FeedKind_t::FEED_KIND_CENTER:
				messages.push_back( { line.m_nTick, kindHtml( tr( "centre" ) ) + DemoColoredHtml( line.m_szText, team, normal, base ) } );
				break;
			case xv::demo::FeedKind_t::FEED_KIND_HINT:
				messages.push_back( { line.m_nTick, kindHtml( tr( "hint" ) ) + DemoColoredHtml( line.m_szText, team, normal, base ) } );
				break;
			case xv::demo::FeedKind_t::FEED_KIND_KEY_HINT:
				messages.push_back( { line.m_nTick, kindHtml( tr( "key hint" ) ) + DemoColoredHtml( line.m_szText, team, normal, base ) } );
				break;
			case xv::demo::FeedKind_t::FEED_KIND_DIALOG:
				if ( line.m_szText != szLastDialog )
				{
					const QColor own( static_cast<int>( line.m_nRgba >> 24 ), static_cast<int>( ( line.m_nRgba >> 16 ) & 0xff ), static_cast<int>( ( line.m_nRgba >> 8 ) & 0xff ) );
					messages.push_back( { line.m_nTick, kindHtml( tr( "dialog" ) ) + DemoColoredHtml( line.m_szText, team, own == QColor( 255, 255, 255 ) ? normal : own, base ) } );
				}
				szLastDialog = line.m_szText;
				break;
		}
	}
	for ( const xv::demo::HudText_t &text : m_Feed.GetHudTexts() )
	{
		const QColor color( static_cast<int>( text.m_nColor1 >> 24 ), static_cast<int>( ( text.m_nColor1 >> 16 ) & 0xff ), static_cast<int>( ( text.m_nColor1 >> 8 ) & 0xff ) );
		messages.push_back( { text.m_nTick, kindHtml( tr( "HUD %1" ).arg( text.m_nChannel ) ) + DemoColoredHtml( text.m_szText, color, color, base ) } );
	}
	for ( const xv::demo::MenuShown_t &menu : m_Feed.GetMenus() )
		messages.push_back( { menu.m_nTick, kindHtml( tr( "menu" ) ) + DemoColoredHtml( menu.m_szText, normal, normal, base ) } );
	for ( const xv::demo::PanelShown_t &panel : m_Feed.GetPanels() )
		messages.push_back( { panel.m_nTick, kindHtml( tr( "panel" ) ) + ( panel.m_bShow ? tr( "%1 opened" ) : tr( "%1 closed" ) ).arg( QString::fromStdString( panel.m_szName ).toHtmlEscaped() ) } );
	for ( const xv::demo::Round_t &round : m_Feed.GetRounds() )
	{
		if ( round.m_nEnd >= 0 )
			messages.push_back( { round.m_nEnd, kindHtml( tr( "round" ) ) + DemoColoredHtml( round.m_szMessage, normal, normal, base ) } );
	}
	std::ranges::stable_sort( messages, {}, &CDemoLog::Entry_t::m_nTick );
	for ( const xv::demo::Kill_t &kill : m_Feed.GetKills() )
	{
		QString szHtml;
		if ( kill.m_nAttacker && kill.m_nAttacker != kill.m_nVictim )
			szHtml += QStringLiteral( "<span style=\"%1\">%2</span> " ).arg( DemoReadableStyle( teamColor( kill.m_nAttacker, kill.m_nTick ), base ), PlayerName( kill.m_nAttacker, kill.m_nTick ).toHtmlEscaped() );
		szHtml += kindHtml( QStringLiteral( "[%1%2]" ).arg( QString::fromStdString( kill.m_szWeapon ), kill.m_bHeadshot ? tr( ", headshot" ) : QString{} ) );
		szHtml += QStringLiteral( "<span style=\"%1\">%2</span>" ).arg( DemoReadableStyle( teamColor( kill.m_nVictim, kill.m_nTick ), base ), PlayerName( kill.m_nVictim, kill.m_nTick ).toHtmlEscaped() );
		kills.push_back( { kill.m_nTick, szHtml } );
	}
	const auto time = [ this ]( int nTick ) { return FormatTime( nTick ); };
	m_pChatLog->SetEntries( std::move( chat ), time );
	m_pKillLog->SetEntries( std::move( kills ), time );
	m_pMessageLog->SetEntries( std::move( messages ), time );
	UpdateLogs();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::changeEvent( QEvent *pEvent )
{
	QWidget::changeEvent( pEvent );
	if ( pEvent->type() == QEvent::PaletteChange )
		BuildLogs();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::UpdateLogs()
{
	for ( CDemoLog *pLog : { m_pChatLog, m_pKillLog, m_pMessageLog, m_pLogicLog } )
	{
		if ( pLog )
			pLog->ShowUpTo( m_nTick );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The guesses, and their log: the change, then each cause with how much the demo
//          backs it.
//-----------------------------------------------------------------------------
void CDemoViewer::BuildLogic()
{
	if ( !m_pContents )
		return;
	m_Guesses = xv::demo::GuessLogic( *m_pContents, m_Players, m_Feed, m_pWorld->GetLogicMap() );
	const QColor grey = palette().color( QPalette::PlaceholderText );
	std::vector<CDemoLog::Entry_t> entries;
	for ( std::size_t i = 0; i < m_Guesses.size(); i++ )
	{
		const xv::demo::LogicGuess_t &guess = m_Guesses[ i ];
		QString szHtml = QStringLiteral( "<a href=\"item:%1\"><b>%2 %3</b></a> %4" ).arg( i ).arg( QString::fromStdString( guess.m_szClass ).toHtmlEscaped(), QString::fromStdString( guess.m_szName ).toHtmlEscaped(), QString::fromUtf8( xv::demo::LogicChangeName( guess.m_nChange ) ) );
		for ( const xv::demo::LogicCause_t &cause : guess.m_Causes )
		{
			const QColor trust = cause.m_nTrust == xv::demo::LogicTrust_t::LOGIC_TRUST_LIKELY ? QColor( 90, 190, 90 ) : cause.m_nTrust == xv::demo::LogicTrust_t::LOGIC_TRUST_UNLIKELY ? QColor( 200, 90, 90 )
																																													   : grey;
			const QString szTrust = cause.m_nTrust == xv::demo::LogicTrust_t::LOGIC_TRUST_LIKELY ? tr( "likely" ) : cause.m_nTrust == xv::demo::LogicTrust_t::LOGIC_TRUST_UNLIKELY ? tr( "unlikely" )
																																												   : tr( "possible" );
			QString szCause = cause.m_nSource == SIZE_MAX ? QString::fromStdString( cause.m_szSource ) : tr( "%1 %2 \u2192 %3%4 after %5 s" ).arg( QString::fromStdString( cause.m_szSource ), QString::fromStdString( cause.m_szOutput ), QString::fromStdString( cause.m_szInput ), cause.m_szParameter.empty() ? QString{} : QStringLiteral( " (%1)" ).arg( QString::fromStdString( cause.m_szParameter ) ) ).arg( static_cast<double>( cause.m_flDelay ), 0, 'g', 3 );
			if ( !cause.m_szWhy.empty() )
				szCause += QStringLiteral( ": " ) + QString::fromStdString( cause.m_szWhy );
			szHtml += QStringLiteral( "<br>&nbsp;&nbsp;&nbsp;<span style=\"color:%1\">%2</span> %3" ).arg( trust.name(), szTrust, szCause.toHtmlEscaped() );
		}
		entries.push_back( { guess.m_nTick, szHtml } );
	}
	m_pLogicLog->SetEntries( std::move( entries ), [ this ]( int nTick ) { return FormatTime( nTick ); } );
	m_pLogicLog->ShowUpTo( m_nTick );
	if ( m_pTimeline )
		m_pTimeline->update();
}

//-----------------------------------------------------------------------------
// Purpose: The playhead goes to the change; the entity is marked yellow, the sources of the
//          likely causes (or the first cause) blue: networked ones where they are, triggers
//          and such by their brush's box.
//-----------------------------------------------------------------------------
void CDemoViewer::ShowGuess( std::size_t nGuess )
{
	if ( nGuess >= m_Guesses.size() )
		return;
	const xv::demo::LogicGuess_t &guess = m_Guesses[ nGuess ];
	SetTick( guess.m_nTick );
	std::vector<CDemoWorld::Highlight_t> highlights;
	highlights.push_back( { guess.m_nEntity, std::nullopt, QColor( 255, 220, 0 ), QStringLiteral( "%1 %2" ).arg( QString::fromStdString( guess.m_szName ), QString::fromUtf8( xv::demo::LogicChangeName( guess.m_nChange ) ) ) } );
	const xv::demo::LogicMap_t &map = m_pWorld->GetLogicMap();
	for ( std::size_t i = 0; i < guess.m_Causes.size(); i++ )
	{
		const xv::demo::LogicCause_t &cause = guess.m_Causes[ i ];
		if ( i > 0 && cause.m_nTrust != xv::demo::LogicTrust_t::LOGIC_TRUST_LIKELY )
			break;
		if ( cause.m_nSource >= map.m_Entities.size() )
			continue;
		CDemoWorld::Highlight_t source;
		source.m_nEntity = cause.m_nSourceEntity;
		source.m_Color = QColor( 80, 200, 255 );
		source.m_szLabel = QStringLiteral( "%1 %2" ).arg( QString::fromStdString( cause.m_szSource ), QString::fromStdString( cause.m_szOutput ) );
		const xv::bsp::Entity_t &entity = map.m_Entities[ cause.m_nSource ];
		const std::string_view szModel = entity.Get( "model" );
		if ( szModel.starts_with( '*' ) )
		{
			const std::size_t nModel = static_cast<std::size_t>( std::atoi( std::string( szModel.substr( 1 ) ).c_str() ) );
			if ( nModel < map.m_ModelBounds.size() )
			{
				xv::map::Box_t box = map.m_ModelBounds[ nModel ];
				double flOrigin[ 3 ] = {};
				if ( std::sscanf( std::string( entity.Get( "origin" ) ).c_str(), "%lf %lf %lf", &flOrigin[ 0 ], &flOrigin[ 1 ], &flOrigin[ 2 ] ) == 3 && !box.IsEmpty() )
				{
					box.m_Mins = xv::map::Add( box.m_Mins, xv::map::Point_t{ flOrigin[ 0 ], flOrigin[ 1 ], flOrigin[ 2 ] } );
					box.m_Maxs = xv::map::Add( box.m_Maxs, xv::map::Point_t{ flOrigin[ 0 ], flOrigin[ 1 ], flOrigin[ 2 ] } );
				}
				source.m_Box = box;
			}
		}
		if ( source.m_nEntity || source.m_Box )
			highlights.push_back( std::move( source ) );
	}
	m_pWorld->SetHighlights( std::move( highlights ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoViewer::AddWarning( const QString &szText )
{
	QLabel *pWarning = ValueLabel( szText, true );
	pWarning->setObjectName( QStringLiteral( "demoMapWarning" ) );
	m_pSummaryForm->addRow( pWarning );
}

//-----------------------------------------------------------------------------
// Purpose: The sound follows the playhead and the view's camera.
//-----------------------------------------------------------------------------
void CDemoViewer::UpdateSound()
{
	m_pSound->Update( GetPlayTick(), IsPlaying(), GetSpeed(), m_pWorld->GetCamera(), m_pWorld->GetEarPlayer() );
}
