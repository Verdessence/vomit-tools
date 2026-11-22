//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoGraphs.h.
//
//===========================================================================//
#include "DemoGraphs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "DemoViewer.h"

namespace
{

// The spacing between samples of the team counts and scores, in seconds.
constexpr double SAMPLE_SECONDS = 0.5;
// How many players' lines are named at the plot's right edge (the highest at the end).
constexpr int NAMED_PLAYERS = 5;
// How near the pointer a line has to be to be picked out, in pixels.
constexpr double HOVER_REACH = 8.0;

constexpr double NOT_THERE = std::numeric_limits<double>::quiet_NaN();

//-----------------------------------------------------------------------------
// Purpose: A round step for about nLines grid lines up to flMost: 1, 2 or 5 times a power of
//          ten.
//-----------------------------------------------------------------------------
double NiceStep( double flMost, int nLines )
{
	const double flRough = std::max( flMost, 1.0 ) / nLines;
	const double flPower = std::pow( 10.0, std::floor( std::log10( flRough ) ) );
	for ( const double flTimes : { 1.0, 2.0, 5.0, 10.0 } )
	{
		if ( flRough <= flTimes * flPower )
			return std::max( 1.0, flTimes * flPower );
	}
	return std::max( 1.0, 10.0 * flPower );
}

//-----------------------------------------------------------------------------
// Purpose: The point a line adds when its value changes (or breaks), so a line is as few
//          points as it has steps.
//-----------------------------------------------------------------------------
void AddStep( DemoGraphSeries_t &series, int nTick, double flValue, int nTeam )
{
	if ( !series.m_Values.empty() )
	{
		const double flLast = series.m_Values.back();
		const bool bSame = ( std::isnan( flLast ) && std::isnan( flValue ) ) || flLast == flValue;
		if ( bSame && series.m_Teams.back() == nTeam )
			return;
	}
	series.m_Ticks.push_back( nTick );
	series.m_Values.push_back( flValue );
	series.m_Teams.push_back( nTeam );
}

//-----------------------------------------------------------------------------
// Purpose: The key a player's lines are joined by across slots and reconnects: the Steam id,
//          or for bots and players without one, the name.
//-----------------------------------------------------------------------------
QString PlayerKey( const xv::demo::PlayerIdentity_t &identity )
{
	if ( !identity.m_bBot && !identity.m_szSteamId.empty() && identity.m_szSteamId != "BOT" )
		return QString::fromStdString( identity.m_szSteamId );
	return QStringLiteral( "name:" ) + QString::fromStdString( identity.m_szName );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The last point at or before the tick holds there.
//-----------------------------------------------------------------------------
double DemoGraphSeries_t::ValueAt( int nTick ) const
{
	const auto it = std::upper_bound( m_Ticks.begin(), m_Ticks.end(), nTick );
	if ( it == m_Ticks.begin() )
		return NOT_THERE;
	return m_Values[ static_cast<std::size_t>( it - m_Ticks.begin() - 1 ) ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int DemoGraphSeries_t::TeamAt( int nTick ) const
{
	const auto it = std::upper_bound( m_Ticks.begin(), m_Ticks.end(), nTick );
	if ( it == m_Ticks.begin() )
		return 0;
	return m_Teams[ static_cast<std::size_t>( it - m_Ticks.begin() - 1 ) ];
}

//-----------------------------------------------------------------------------
// Purpose: The last value it had (a break at the end doesn't count).
//-----------------------------------------------------------------------------
double DemoGraphSeries_t::GetLast() const
{
	for ( auto it = m_Values.rbegin(); it != m_Values.rend(); ++it )
	{
		if ( !std::isnan( *it ) )
			return *it;
	}
	return NOT_THERE;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoGraphView::CDemoGraphView( CDemoViewer &viewer, QWidget *pParent )
	: QWidget( pParent ),
	  m_Viewer( viewer )
{
	setObjectName( QStringLiteral( "demoGraph" ) );
	setMouseTracking( true );
	setMinimumSize( 240, 160 );
	setToolTip( tr( "Hover for the values at a time; click to go there (and keep a player's line picked out); right click to copy the numbers" ) );
	connect( &viewer, &CDemoViewer::TickChanged, this, [ this ] {
		if ( isVisible() )
			update();
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoGraphView::SetGraph( DemoGraph_t nGraph )
{
	if ( nGraph == m_nGraph && m_bBuilt )
		return;
	m_nGraph = nGraph;
	m_bBuilt = false;
	m_nHover = m_nPicked = -1;
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoGraphView::SetTeam( int nTeam )
{
	m_nTeam = nTeam;
	m_nHover = -1;
	if ( m_bBuilt )
	{
		// The top follows the lines shown.
		double flMost = 0.0;
		for ( const DemoGraphSeries_t &series : m_Series )
		{
			if ( !IsShown( series ) )
				continue;
			for ( const double flValue : series.m_Values )
			{
				if ( !std::isnan( flValue ) )
					flMost = std::max( flMost, flValue );
			}
		}
		m_flStep = NiceStep( flMost, 4 );
		m_flTop = std::max( m_flStep, std::ceil( flMost / m_flStep ) * m_flStep );
	}
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::vector<DemoGraphSeries_t> &CDemoGraphView::GetSeries()
{
	if ( !m_bBuilt )
		Build();
	return m_Series;
}

//-----------------------------------------------------------------------------
// Purpose: The graph's lines and the scale that fits them.
//-----------------------------------------------------------------------------
void CDemoGraphView::Build()
{
	m_Series.clear();
	m_szEmpty.clear();
	m_bBuilt = true;
	if ( m_Viewer.GetPlayers().IsEmpty() )
	{
		m_szEmpty = tr( "This demo's players weren't followed, so there is nothing to graph." );
		return;
	}
	switch ( m_nGraph )
	{
		case DemoGraph_t::DEMO_GRAPH_TEAMS:
			BuildTeams();
			break;
		case DemoGraph_t::DEMO_GRAPH_SCORE:
			BuildScores();
			break;
		case DemoGraph_t::DEMO_GRAPH_DAMAGE:
			BuildDamage();
			break;
	}
	SetTeam( m_nTeam );
}

//-----------------------------------------------------------------------------
// Purpose: For each of the two teams, the players on it and those of them alive, counted
//          every SAMPLE_SECONDS.
//-----------------------------------------------------------------------------
void CDemoGraphView::BuildTeams()
{
	const xv::demo::CDemo &demo = m_Viewer.GetDemo();
	const double flInterval = demo.GetTickInterval() > 0.0 ? demo.GetTickInterval() : 1.0 / 66.0;
	const int nStep = std::max( 1, static_cast<int>( std::lround( SAMPLE_SECONDS / flInterval ) ) );
	const std::string szGame = demo.GetHeader().m_szGameDir;
	for ( const int nTeam : { 2, 3 } )
	{
		const QString szTeam = QString::fromStdString( xv::demo::TeamName( szGame, nTeam ) );
		DemoGraphSeries_t players, alive;
		players.m_szName = tr( "%1 players" ).arg( szTeam );
		alive.m_szName = tr( "%1 alive" ).arg( szTeam );
		alive.m_bDashed = true;
		m_Series.push_back( std::move( players ) );
		m_Series.push_back( std::move( alive ) );
	}
	const int nLast = demo.GetLastTick();
	for ( int nTick = 0;; nTick = std::min( nTick + nStep, nLast ) )
	{
		int nCounts[ 4 ] = {};
		for ( const xv::demo::PlayerState_t &state : m_Viewer.GetPlayers().At( static_cast<double>( nTick ) ) )
		{
			if ( state.m_nTeam != 2 && state.m_nTeam != 3 )
				continue;
			const int nSlot = ( state.m_nTeam - 2 ) * 2;
			nCounts[ nSlot ]++;
			if ( state.m_bAlive )
				nCounts[ nSlot + 1 ]++;
		}
		for ( int i = 0; i < 4; i++ )
			AddStep( m_Series[ static_cast<std::size_t>( i ) ], nTick, nCounts[ i ], 2 + i / 2 );
		if ( nTick >= nLast )
			break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Each player's score as the scoreboard had it (the player resource's), every
//          SAMPLE_SECONDS while the player was on the server (a break where it left); a
//          player's lines in other slots (a reconnect) are one line.
//-----------------------------------------------------------------------------
void CDemoGraphView::BuildScores()
{
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	if ( !pContents )
	{
		m_szEmpty = tr( "The demo's packets weren't read, so there are no scores." );
		return;
	}
	const xv::demo::CDemo &demo = m_Viewer.GetDemo();
	const double flInterval = demo.GetTickInterval() > 0.0 ? demo.GetTickInterval() : 1.0 / 66.0;
	const int nStep = std::max( 1, static_cast<int>( std::lround( SAMPLE_SECONDS / flInterval ) ) );
	const int nLast = demo.GetLastTick();
	std::map<QString, std::size_t> byKey;
	bool bAnyScore = false;
	for ( int nTick = 0;; nTick = std::min( nTick + nStep, nLast ) )
	{
		const xv::demo::EntityLife_t *pResource = pContents->FindLife( "CCSPlayerResource", nTick );
		if ( !pResource )
			pResource = pContents->FindLife( "CPlayerResource", nTick );
		for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
		{
			const xv::demo::PlayerIdentity_t *pIdentity = track.IdentityAt( nTick );
			if ( !pIdentity || pIdentity->m_szName.empty() || pIdentity->m_bSourceTv )
				continue;
			const QString szKey = PlayerKey( *pIdentity );
			auto [ it, bNew ] = byKey.try_emplace( szKey, m_Series.size() );
			if ( bNew )
			{
				DemoGraphSeries_t series;
				series.m_szName = QString::fromStdString( pIdentity->m_szName );
				series.m_bPlayer = true;
				m_Series.push_back( std::move( series ) );
			}
			std::optional<float> score;
			if ( pResource )
			{
				char szName[ 24 ];
				std::snprintf( szName, sizeof( szName ), "m_iScore.%03d", track.m_nEntity );
				score = pContents->StatOf( *pResource, szName, nTick );
			}
			bAnyScore = bAnyScore || score.has_value();
			// Out of the demo's sight for a moment, a player keeps its score and its team.
			DemoGraphSeries_t &series = m_Series[ it->second ];
			const auto state = m_Viewer.GetPlayers().At( track.m_nEntity, nTick );
			const int nTeam = state ? state->m_nTeam : series.m_Teams.empty() ? 0
																			  : series.m_Teams.back();
			AddStep( series, nTick, score ? static_cast<double>( *score ) : NOT_THERE, nTeam );
		}
		if ( nTick >= nLast )
			break;
	}
	if ( !bAnyScore )
	{
		m_Series.clear();
		m_szEmpty = tr( "The demo has no scoreboard (no player resource), so there are no scores." );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The health each player lost while alive, added up: a drop between two of its
//          samples, and what it had left when it died. Demos don't record who did the
//          damage, only the health, so this is damage taken; a rise (healing, a respawn,
//          becoming a zombie) adds nothing, nor does a big drop several players had at once.
//-----------------------------------------------------------------------------
void CDemoGraphView::BuildDamage()
{
	// The health a sample shows lost since the one before it the player was seen in (what it
	// had left, when it died); 0 for none.
	const auto lost = []( const xv::demo::PlayerTrack_t &track, const xv::demo::PlayerSample_t *pBefore, const xv::demo::PlayerSample_t &sample ) {
		if ( !pBefore || track.IdentityAt( pBefore->m_nTick ) != track.IdentityAt( sample.m_nTick ) || !pBefore->m_bAlive || pBefore->m_nTeam != sample.m_nTeam )
			return 0;
		if ( sample.m_bAlive )
			return std::max( 0, pBefore->m_nHealth - sample.m_nHealth );
		return std::max<int>( 0, pBefore->m_nHealth );
	};
	// A big drop that several players had at once is the map setting their health (a boss
	// fight's extra health taken back, a round's end slaying everyone), not damage: left out.
	constexpr int BIG_DROP = 1000;
	constexpr int SHARED_BY = 3;
	const xv::demo::CDemo &demo = m_Viewer.GetDemo();
	const int nShareTicks = std::max( 1, static_cast<int>( std::lround( 0.25 / ( demo.GetTickInterval() > 0.0 ? demo.GetTickInterval() : 1.0 / 66.0 ) ) ) );
	std::vector<int> bigDrops;
	for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
	{
		const xv::demo::PlayerSample_t *pBefore = nullptr;
		for ( const xv::demo::PlayerSample_t &sample : track.m_Samples )
		{
			if ( !sample.m_bThere )
				continue;
			if ( lost( track, pBefore, sample ) >= BIG_DROP )
				bigDrops.push_back( sample.m_nTick );
			pBefore = &sample;
		}
	}
	std::ranges::sort( bigDrops );
	const auto isShared = [ & ]( int nTick ) {
		const auto from = std::ranges::lower_bound( bigDrops, nTick - nShareTicks );
		const auto to = std::ranges::upper_bound( bigDrops, nTick + nShareTicks );
		return to - from >= SHARED_BY;
	};
	std::map<QString, std::size_t> byKey;
	std::map<QString, double> totals;
	for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
	{
		// The last sample the player was in sight in, to measure from.
		const xv::demo::PlayerSample_t *pBefore = nullptr;
		for ( const xv::demo::PlayerSample_t &sample : track.m_Samples )
		{
			const xv::demo::PlayerIdentity_t *pIdentity = track.IdentityAt( sample.m_nTick );
			if ( !pIdentity || pIdentity->m_szName.empty() || pIdentity->m_bSourceTv )
				continue;
			const QString szKey = PlayerKey( *pIdentity );
			auto [ it, bNew ] = byKey.try_emplace( szKey, m_Series.size() );
			if ( bNew )
			{
				DemoGraphSeries_t series;
				series.m_szName = QString::fromStdString( pIdentity->m_szName );
				series.m_bPlayer = true;
				m_Series.push_back( std::move( series ) );
			}
			DemoGraphSeries_t &series = m_Series[ it->second ];
			double &flTotal = totals[ szKey ];
			// Out of the demo's sight the total holds until the player is seen again.
			if ( !sample.m_bThere )
				continue;
			const int nLost = lost( track, pBefore, sample );
			if ( nLost < BIG_DROP || !isShared( sample.m_nTick ) )
				flTotal += nLost;
			pBefore = &sample;
			AddStep( series, sample.m_nTick, flTotal, sample.m_nTeam );
		}
	}
	// A player's points from several slots, in tick order.
	for ( DemoGraphSeries_t &series : m_Series )
	{
		std::vector<std::size_t> order( series.m_Ticks.size() );
		for ( std::size_t i = 0; i < order.size(); i++ )
			order[ i ] = i;
		std::ranges::stable_sort( order, {}, [ &series ]( std::size_t i ) { return series.m_Ticks[ i ]; } );
		DemoGraphSeries_t sorted;
		sorted.m_szName = series.m_szName;
		sorted.m_bPlayer = true;
		for ( const std::size_t i : order )
			AddStep( sorted, series.m_Ticks[ i ], series.m_Values[ i ], series.m_Teams[ i ] );
		series = std::move( sorted );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A player's line is shown while it was ever on the team picked.
//-----------------------------------------------------------------------------
bool CDemoGraphView::IsShown( const DemoGraphSeries_t &series ) const
{
	if ( !series.m_bPlayer || m_nTeam == 0 )
		return true;
	return std::ranges::find( series.m_Teams, m_nTeam ) != series.m_Teams.end();
}

//-----------------------------------------------------------------------------
// Purpose: Room on the left for the values, under for the times, on the right for the
//          lines' names, above for the key.
//-----------------------------------------------------------------------------
QRectF CDemoGraphView::PlotRect() const
{
	const QFontMetrics metrics = fontMetrics();
	const double flLeft = metrics.horizontalAdvance( QStringLiteral( "00000" ) ) + 12.0;
	const double flRight = std::min( 180.0, width() * 0.28 );
	const double flTop = metrics.height() * 2.0 + 8.0;
	const double flBottom = metrics.height() + 12.0;
	return QRectF( flLeft, flTop, std::max( 10.0, width() - flLeft - flRight ), std::max( 10.0, height() - flTop - flBottom ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CDemoGraphView::XAt( int nTick ) const
{
	const QRectF plot = PlotRect();
	const int nLast = std::max( 1, m_Viewer.GetDemo().GetLastTick() );
	return plot.left() + plot.width() * std::clamp( nTick, 0, nLast ) / nLast;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemoGraphView::TickAt( double x ) const
{
	const QRectF plot = PlotRect();
	const int nLast = std::max( 1, m_Viewer.GetDemo().GetLastTick() );
	return std::clamp( static_cast<int>( std::lround( ( x - plot.left() ) / plot.width() * nLast ) ), 0, nLast );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CDemoGraphView::YAt( double flValue ) const
{
	const QRectF plot = PlotRect();
	return plot.bottom() - plot.height() * flValue / m_flTop;
}

//-----------------------------------------------------------------------------
// Purpose: The palette is dark when its background is.
//-----------------------------------------------------------------------------
bool CDemoGraphView::IsDark() const
{
	return palette().color( QPalette::Base ).lightnessF() < 0.5;
}

//-----------------------------------------------------------------------------
// Purpose: The teams' colours, each stepped for the light or the dark background so both
//          read against it and stay apart for colour-blind eyes (checked with the dataviz
//          palette validator); grey for no team.
//-----------------------------------------------------------------------------
QColor CDemoGraphView::TeamColor( int nTeam ) const
{
	const bool bDark = IsDark();
	switch ( nTeam )
	{
		case 2:
			return bDark ? QColor( 0xe8, 0x68, 0x40 ) : QColor( 0xe0, 0x60, 0x3a );
		case 3:
			return bDark ? QColor( 0x4f, 0x8e, 0xe6 ) : QColor( 0x3f, 0x7f, 0xd9 );
		default:
			return palette().color( QPalette::PlaceholderText );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The shown line whose value at the pointer's time is nearest it, within reach.
//-----------------------------------------------------------------------------
int CDemoGraphView::SeriesNear( QPointF at ) const
{
	const int nTick = TickAt( at.x() );
	int nBest = -1;
	double flBest = HOVER_REACH;
	for ( std::size_t i = 0; i < m_Series.size(); i++ )
	{
		if ( !IsShown( m_Series[ i ] ) )
			continue;
		const double flValue = m_Series[ i ].ValueAt( nTick );
		if ( std::isnan( flValue ) )
			continue;
		const double flAway = std::abs( YAt( flValue ) - at.y() );
		if ( flAway <= flBest )
		{
			flBest = flAway;
			nBest = static_cast<int>( i );
		}
	}
	return nBest;
}

//-----------------------------------------------------------------------------
// Purpose: A recessive grid and axes; the lines as steps (a player's in its team's colour as
//          it goes, faint unless hovered or picked); the key above; names at the right for
//          the teams' lines or the players highest at the end and the one picked out; the
//          playhead; under the pointer, a crosshair with the values at that time.
//-----------------------------------------------------------------------------
void CDemoGraphView::paintEvent( QPaintEvent * )
{
	if ( !m_bBuilt )
		Build();
	QPainter painter( this );
	painter.fillRect( rect(), palette().color( QPalette::Base ) );
	const QColor ink = palette().color( QPalette::Text );
	const QColor muted = palette().color( QPalette::PlaceholderText );
	QColor grid = ink;
	grid.setAlphaF( 0.10f );
	if ( !m_szEmpty.isEmpty() || m_Series.empty() )
	{
		painter.setPen( muted );
		painter.drawText( rect().adjusted( 16, 16, -16, -16 ), Qt::AlignCenter | Qt::TextWordWrap, m_szEmpty.isEmpty() ? tr( "Nothing to graph." ) : m_szEmpty );
		return;
	}
	const QRectF plot = PlotRect();
	const QFontMetrics metrics = fontMetrics();
	painter.setRenderHint( QPainter::Antialiasing );

	// The values up the left, with their grid lines.
	for ( double flValue = 0.0; flValue <= m_flTop + 0.5; flValue += m_flStep )
	{
		const double y = YAt( flValue );
		painter.setPen( QPen( grid, 1 ) );
		painter.drawLine( QPointF( plot.left(), y ), QPointF( plot.right(), y ) );
		painter.setPen( muted );
		painter.drawText( QRectF( 0, y - metrics.height() / 2.0, plot.left() - 6, metrics.height() ), Qt::AlignRight | Qt::AlignVCenter, QLocale().toString( flValue, 'f', 0 ) );
	}
	// The times along the bottom, a round number of seconds apart.
	const xv::demo::CDemo &demo = m_Viewer.GetDemo();
	const int nLast = std::max( 1, demo.GetLastTick() );
	const double flInterval = demo.GetTickInterval() > 0.0 ? demo.GetTickInterval() : 1.0 / 66.0;
	const double flSeconds = nLast * flInterval;
	const int nLabels = std::max( 2, static_cast<int>( plot.width() / ( metrics.horizontalAdvance( QStringLiteral( "00:00.0" ) ) * 2.0 ) ) );
	double flEvery = 1.0;
	for ( const double fl : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0 } )
	{
		flEvery = fl;
		if ( flSeconds / fl <= nLabels )
			break;
	}
	painter.setPen( muted );
	for ( double fl = 0.0; fl <= flSeconds; fl += flEvery )
	{
		const int nTick = static_cast<int>( std::lround( fl / flInterval ) );
		const double x = XAt( nTick );
		painter.setPen( QPen( grid, 1 ) );
		painter.drawLine( QPointF( x, plot.bottom() ), QPointF( x, plot.bottom() + 4 ) );
		painter.setPen( muted );
		painter.drawText( QRectF( x - 40, plot.bottom() + 4, 80, metrics.height() + 4 ), Qt::AlignHCenter | Qt::AlignTop, m_Viewer.FormatTime( nTick ) );
	}
	painter.setPen( QPen( muted, 1 ) );
	painter.drawLine( plot.bottomLeft(), plot.bottomRight() );

	// The lines: faint players first, then the teams' and the picked-out ones over them.
	const bool bPlayers = m_nGraph != DemoGraph_t::DEMO_GRAPH_TEAMS;
	const auto drawSeries = [ & ]( std::size_t nIndex, bool bStrong ) {
		const DemoGraphSeries_t &series = m_Series[ nIndex ];
		for ( std::size_t i = 0; i < series.m_Ticks.size(); i++ )
		{
			const double flValue = series.m_Values[ i ];
			if ( std::isnan( flValue ) )
				continue;
			const int nEnd = i + 1 < series.m_Ticks.size() ? series.m_Ticks[ i + 1 ] : nLast;
			QColor color = TeamColor( series.m_Teams[ i ] );
			if ( !bStrong )
				color.setAlphaF( 0.35f );
			QPen pen( color, bStrong ? 2.0 : 1.0 );
			pen.setCapStyle( Qt::FlatCap );
			if ( series.m_bDashed )
				pen.setDashPattern( { 3.0, 2.0 } );
			painter.setPen( pen );
			const double y = YAt( flValue );
			painter.drawLine( QPointF( XAt( series.m_Ticks[ i ] ), y ), QPointF( XAt( nEnd ), y ) );
			// The rise or fall to the next step, unless the line breaks there.
			if ( i + 1 < series.m_Ticks.size() && !std::isnan( series.m_Values[ i + 1 ] ) )
				painter.drawLine( QPointF( XAt( nEnd ), y ), QPointF( XAt( nEnd ), YAt( series.m_Values[ i + 1 ] ) ) );
		}
	};
	painter.save();
	painter.setClipRect( plot.adjusted( -2, -2, 2, 2 ) );
	const int nStrong = m_nHover >= 0 ? m_nHover : m_nPicked;
	for ( std::size_t i = 0; i < m_Series.size(); i++ )
	{
		if ( IsShown( m_Series[ i ] ) && ( !bPlayers || static_cast<int>( i ) != nStrong ) )
			drawSeries( i, !bPlayers );
	}
	if ( bPlayers && m_nPicked >= 0 && m_nPicked != nStrong )
		drawSeries( static_cast<std::size_t>( m_nPicked ), true );
	if ( bPlayers && nStrong >= 0 )
		drawSeries( static_cast<std::size_t>( nStrong ), true );
	painter.restore();

	// The key: what the colours (and the dashes) mean.
	{
		double x = plot.left();
		const double y = metrics.height() * 0.5 + 4.0;
		const auto keyItem = [ & ]( const QColor &color, bool bDashed, const QString &szText ) {
			QPen pen( color, 2.0 );
			if ( bDashed )
				pen.setDashPattern( { 3.0, 2.0 } );
			painter.setPen( pen );
			painter.drawLine( QPointF( x, y + metrics.height() / 2.0 ), QPointF( x + 18, y + metrics.height() / 2.0 ) );
			painter.setPen( ink );
			painter.drawText( QPointF( x + 24, y + metrics.ascent() ), szText );
			x += 24 + metrics.horizontalAdvance( szText ) + 18;
		};
		const std::string szGame = demo.GetHeader().m_szGameDir;
		for ( const int nTeam : { 2, 3 } )
		{
			if ( bPlayers && m_nTeam != 0 && m_nTeam != nTeam )
				continue;
			keyItem( TeamColor( nTeam ), false, QString::fromStdString( xv::demo::TeamName( szGame, nTeam ) ) );
		}
		if ( !bPlayers )
			keyItem( muted, true, tr( "alive" ) );
		else
			keyItem( muted, false, m_nGraph == DemoGraph_t::DEMO_GRAPH_SCORE ? tr( "a line each player, its team's colour as it goes" ) : tr( "health each player lost, added up" ) );
	}

	// Names at the right: the teams' lines, or the players highest at the end (and the one
	// hovered or picked), each by its line's end, pushed apart so none overlap.
	struct Label_t
	{
		double m_flY;
		QString m_szText;
		QColor m_Color;
	};
	std::vector<Label_t> labels;
	std::vector<std::size_t> named;
	if ( !bPlayers )
	{
		for ( std::size_t i = 0; i < m_Series.size(); i++ )
			named.push_back( i );
	}
	else
	{
		std::vector<std::size_t> ranked;
		for ( std::size_t i = 0; i < m_Series.size(); i++ )
		{
			if ( IsShown( m_Series[ i ] ) && !std::isnan( m_Series[ i ].GetLast() ) )
				ranked.push_back( i );
		}
		std::ranges::sort( ranked, std::greater<>{}, [ this ]( std::size_t i ) { return m_Series[ i ].GetLast(); } );
		ranked.resize( std::min<std::size_t>( ranked.size(), NAMED_PLAYERS ) );
		named = ranked;
		for ( const int nExtra : { m_nPicked, m_nHover } )
		{
			if ( nExtra >= 0 && std::ranges::find( named, static_cast<std::size_t>( nExtra ) ) == named.end() )
				named.push_back( static_cast<std::size_t>( nExtra ) );
		}
	}
	for ( const std::size_t i : named )
	{
		const DemoGraphSeries_t &series = m_Series[ i ];
		const double flLast = series.GetLast();
		if ( std::isnan( flLast ) )
			continue;
		const QString szValue = QLocale().toString( flLast, 'f', 0 );
		labels.push_back( { YAt( flLast ), metrics.elidedText( series.m_szName, Qt::ElideRight, static_cast<int>( width() - plot.right() - 30 - metrics.horizontalAdvance( szValue ) ) ) + QStringLiteral( "  " ) + szValue, TeamColor( series.m_Teams.empty() ? 0 : series.m_Teams.back() ) } );
	}
	std::ranges::sort( labels, {}, &Label_t::m_flY );
	const double flLine = metrics.height();
	for ( std::size_t i = 1; i < labels.size(); i++ )
		labels[ i ].m_flY = std::max( labels[ i ].m_flY, labels[ i - 1 ].m_flY + flLine );
	if ( !labels.empty() && labels.back().m_flY > plot.bottom() )
	{
		labels.back().m_flY = plot.bottom();
		for ( std::size_t i = labels.size() - 1; i > 0; i-- )
			labels[ i - 1 ].m_flY = std::min( labels[ i - 1 ].m_flY, labels[ i ].m_flY - flLine );
	}
	for ( const Label_t &label : labels )
	{
		painter.setPen( Qt::NoPen );
		painter.setBrush( label.m_Color );
		painter.drawEllipse( QPointF( plot.right() + 10, label.m_flY ), 3.5, 3.5 );
		painter.setPen( ink );
		painter.drawText( QPointF( plot.right() + 18, label.m_flY - metrics.height() / 2.0 + metrics.ascent() ), label.m_szText );
	}

	// The playhead.
	{
		QColor playhead = palette().color( QPalette::Highlight );
		painter.setPen( QPen( playhead, 1.5 ) );
		const double x = XAt( m_Viewer.GetTick() );
		painter.drawLine( QPointF( x, plot.top() ), QPointF( x, plot.bottom() ) );
	}

	// The crosshair and the values at the pointer's time.
	if ( m_bMouseIn && plot.adjusted( -4, -4, 4, 4 ).contains( m_Mouse ) )
	{
		const int nTick = TickAt( m_Mouse.x() );
		const double x = XAt( nTick );
		QColor cross = ink;
		cross.setAlphaF( 0.35f );
		painter.setPen( QPen( cross, 1, Qt::DashLine ) );
		painter.drawLine( QPointF( x, plot.top() ), QPointF( x, plot.bottom() ) );
		struct Row_t
		{
			QColor m_Color;
			bool m_bDashed;
			QString m_szText;
		};
		std::vector<Row_t> rows;
		const auto valueText = [ & ]( double fl ) { return std::isnan( fl ) ? tr( "not in the game" ) : QLocale().toString( fl, 'f', 0 ); };
		if ( !bPlayers )
		{
			for ( const DemoGraphSeries_t &series : m_Series )
				rows.push_back( { TeamColor( series.TeamAt( nTick ) ), series.m_bDashed, QStringLiteral( "%1: %2" ).arg( series.m_szName, valueText( series.ValueAt( nTick ) ) ) } );
		}
		else
		{
			for ( const int nIndex : { m_nHover, m_nPicked } )
			{
				if ( nIndex < 0 || ( nIndex == m_nPicked && m_nPicked == m_nHover && !rows.empty() ) )
					continue;
				const DemoGraphSeries_t &series = m_Series[ static_cast<std::size_t>( nIndex ) ];
				rows.push_back( { TeamColor( series.TeamAt( nTick ) ), false, QStringLiteral( "%1: %2" ).arg( series.m_szName, valueText( series.ValueAt( nTick ) ) ) } );
			}
			if ( rows.empty() )
				rows.push_back( { muted, false, tr( "Point at a line for its player" ) } );
		}
		const QString szTime = m_Viewer.FormatTime( nTick );
		double flWide = metrics.horizontalAdvance( szTime );
		for ( const Row_t &row : rows )
			flWide = std::max( flWide, metrics.horizontalAdvance( row.m_szText ) + 20.0 );
		const double flTall = metrics.height() * ( rows.size() + 1 ) + 10.0;
		QRectF box( m_Mouse.x() + 14, m_Mouse.y() + 14, flWide + 16, flTall );
		if ( box.right() > width() - 4 )
			box.moveRight( m_Mouse.x() - 14 );
		if ( box.bottom() > height() - 4 )
			box.moveBottom( m_Mouse.y() - 14 );
		painter.setPen( QPen( palette().color( QPalette::Mid ), 1 ) );
		painter.setBrush( palette().color( QPalette::ToolTipBase ) );
		painter.drawRoundedRect( box, 4, 4 );
		const QColor tipInk = palette().color( QPalette::ToolTipText );
		double y = box.top() + 5;
		painter.setPen( tipInk );
		painter.drawText( QPointF( box.left() + 8, y + metrics.ascent() ), szTime );
		y += metrics.height();
		for ( const Row_t &row : rows )
		{
			QPen pen( row.m_Color, 2.0 );
			if ( row.m_bDashed )
				pen.setDashPattern( { 3.0, 2.0 } );
			painter.setPen( pen );
			painter.drawLine( QPointF( box.left() + 8, y + metrics.height() / 2.0 ), QPointF( box.left() + 20, y + metrics.height() / 2.0 ) );
			painter.setPen( tipInk );
			painter.drawText( QPointF( box.left() + 28, y + metrics.ascent() ), row.m_szText );
			y += metrics.height();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoGraphView::mouseMoveEvent( QMouseEvent *pEvent )
{
	m_Mouse = pEvent->position();
	m_bMouseIn = true;
	if ( m_nGraph != DemoGraph_t::DEMO_GRAPH_TEAMS && m_bBuilt )
		m_nHover = SeriesNear( m_Mouse );
	update();
}

//-----------------------------------------------------------------------------
// Purpose: The playhead to the time clicked; on a player's line, that line picked out (again
//          to let it go).
//-----------------------------------------------------------------------------
void CDemoGraphView::mousePressEvent( QMouseEvent *pEvent )
{
	if ( pEvent->button() != Qt::LeftButton )
		return;
	const QRectF plot = PlotRect();
	if ( !plot.adjusted( -4, -4, 4, 4 ).contains( pEvent->position() ) )
		return;
	if ( m_nGraph != DemoGraph_t::DEMO_GRAPH_TEAMS )
	{
		const int nNear = SeriesNear( pEvent->position() );
		if ( nNear >= 0 )
			m_nPicked = nNear == m_nPicked ? -1 : nNear;
	}
	m_Viewer.SetTick( TickAt( pEvent->position().x() ) );
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoGraphView::leaveEvent( QEvent * )
{
	m_bMouseIn = false;
	m_nHover = -1;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: The numbers behind the graph, for a spreadsheet.
//-----------------------------------------------------------------------------
void CDemoGraphView::contextMenuEvent( QContextMenuEvent *pEvent )
{
	QMenu menu( this );
	menu.addAction( tr( "Copy the Numbers (CSV)" ), this, [ this ] { QApplication::clipboard()->setText( ToCsv() ); } );
	menu.exec( pEvent->globalPos() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoGraphView::changeEvent( QEvent *pEvent )
{
	QWidget::changeEvent( pEvent );
	if ( pEvent->type() == QEvent::PaletteChange )
		update();
}

//-----------------------------------------------------------------------------
// Purpose: A header of the shown lines' names, then a row at every tick a shown line changed:
//          the time, the tick and each line's value (empty where it wasn't in the game).
//-----------------------------------------------------------------------------
QString CDemoGraphView::ToCsv()
{
	const std::vector<DemoGraphSeries_t> &all = GetSeries();
	std::vector<const DemoGraphSeries_t *> shown;
	std::vector<int> ticks;
	for ( const DemoGraphSeries_t &series : all )
	{
		if ( !IsShown( series ) )
			continue;
		shown.push_back( &series );
		ticks.insert( ticks.end(), series.m_Ticks.begin(), series.m_Ticks.end() );
	}
	std::ranges::sort( ticks );
	ticks.erase( std::unique( ticks.begin(), ticks.end() ), ticks.end() );
	const auto quoted = []( QString sz ) { return QLatin1Char( '"' ) + sz.replace( QLatin1Char( '"' ), QStringLiteral( "\"\"" ) ) + QLatin1Char( '"' ); };
	QString szCsv = QStringLiteral( "time,tick" );
	for ( const DemoGraphSeries_t *pSeries : shown )
		szCsv += QLatin1Char( ',' ) + quoted( pSeries->m_szName );
	szCsv += QLatin1Char( '\n' );
	for ( const int nTick : ticks )
	{
		szCsv += quoted( m_Viewer.FormatTime( nTick ) ) + QLatin1Char( ',' ) + QString::number( nTick );
		for ( const DemoGraphSeries_t *pSeries : shown )
		{
			const double flValue = pSeries->ValueAt( nTick );
			szCsv += QLatin1Char( ',' ) + ( std::isnan( flValue ) ? QString{} : QString::number( flValue, 'f', 0 ) );
		}
		szCsv += QLatin1Char( '\n' );
	}
	return szCsv;
}
