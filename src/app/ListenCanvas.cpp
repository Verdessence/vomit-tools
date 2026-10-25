//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ListenCanvas.h.
//
//===========================================================================//
#include "ListenCanvas.h"

#include <cmath>
#include <numbers>

#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include "SoundPlayer.h"

namespace
{

constexpr int LISTENER = 8;
constexpr double MARKER_RADIUS = 11.0;
constexpr qint64 FADE_MS = 700;
constexpr auto SCENE_KEY = "listenScene";

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The scene is the player's (one for the app), kept between runs; the first time, the
//          positions sit in a ring around the listener.
//-----------------------------------------------------------------------------
CListenCanvas::CListenCanvas( CSoundPlayer *pPlayer, QWidget *pParent )
	: QWidget( pParent ),
	  m_pPlayer( pPlayer )
{
	setObjectName( QStringLiteral( "listenCanvas" ) );
	setMinimumSize( 220, 220 );
	setMouseTracking( true );
	setToolTip( tr( "Drag the listener and positions 0–7 (env_soundscape's position targets). The wheel zooms." ) );
	m_Clock.start();
	m_pFade = new QTimer( this );
	m_pFade->setInterval( 30 );
	connect( m_pFade, &QTimer::timeout, this, [ this ] {
		const bool bFading = std::ranges::any_of( m_Heard, [ this ]( const auto &at ) { return at && m_Clock.elapsed() - *at < FADE_MS; } );
		if ( !bFading )
		{
			m_pFade->stop();
		}
		update();
	} );

	CSoundPlayer::ListenScene_t scene = m_pPlayer->GetListenScene();
	const QList<QVariant> saved = QSettings{}.value( QLatin1String( SCENE_KEY ) ).toList();
	if ( saved.size() == 9 )
	{
		for ( std::size_t i = 0; i < 8; i++ )
		{
			scene.m_Positions[ i ] = saved[ static_cast<qsizetype>( i ) ].toPointF();
		}
		scene.m_Listener = saved[ 8 ].toPointF();
	}
	else if ( std::ranges::all_of( scene.m_Positions, []( const QPointF &at ) { return at.isNull(); } ) )
	{
		for ( std::size_t i = 0; i < 8; i++ )
		{
			const double flAngle = std::numbers::pi / 2.0 - static_cast<double>( i ) * std::numbers::pi / 4.0;
			scene.m_Positions[ i ] = QPointF( std::cos( flAngle ), std::sin( flAngle ) ) * 400.0;
		}
	}
	m_pPlayer->SetListenScene( scene );
	connect( m_pPlayer, &CSoundPlayer::PositionHeard, this, [ this ]( int nPosition ) {
		m_Heard[ static_cast<std::size_t>( nPosition ) ] = m_Clock.elapsed();
		m_pFade->start();
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CListenCanvas::SetUsed( std::set<int> used )
{
	m_Used = std::move( used );
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CListenCanvas::SetListening( bool bOn )
{
	CSoundPlayer::ListenScene_t scene = m_pPlayer->GetListenScene();
	scene.m_bOn = bOn;
	m_pPlayer->SetListenScene( scene );
	update();
}

//-----------------------------------------------------------------------------
// Purpose: The listener stays in the middle of the view; world y (ahead) is up.
//-----------------------------------------------------------------------------
QPointF CListenCanvas::ToWidget( const QPointF &world ) const
{
	const QPointF offset = world - m_pPlayer->GetListenScene().m_Listener;
	return QPointF( width() / 2.0 + offset.x() / m_flUnitsPerPixel, height() / 2.0 - offset.y() / m_flUnitsPerPixel );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QPointF CListenCanvas::ToWorld( const QPointF &at ) const
{
	return m_pPlayer->GetListenScene().m_Listener + QPointF( ( at.x() - width() / 2.0 ) * m_flUnitsPerPixel, ( height() / 2.0 - at.y() ) * m_flUnitsPerPixel );
}

//-----------------------------------------------------------------------------
// Purpose: A grid every 128 units (256 when zoomed out), then the positions, then the listener.
//-----------------------------------------------------------------------------
void CListenCanvas::paintEvent( QPaintEvent * )
{
	const CSoundPlayer::ListenScene_t &scene = m_pPlayer->GetListenScene();
	QPainter painter( this );
	painter.setRenderHint( QPainter::Antialiasing );
	painter.fillRect( rect(), palette().base() );

	const double flStep = m_flUnitsPerPixel > 6.0 ? 256.0 : 128.0;
	QColor grid = palette().text().color();
	grid.setAlphaF( 0.08f );
	painter.setPen( grid );
	const QPointF topLeft = ToWorld( QPointF( 0, 0 ) );
	const QPointF bottomRight = ToWorld( QPointF( width(), height() ) );
	for ( double x = std::floor( topLeft.x() / flStep ) * flStep; x <= bottomRight.x(); x += flStep )
	{
		painter.drawLine( QPointF( ToWidget( { x, 0 } ).x(), 0 ), QPointF( ToWidget( { x, 0 } ).x(), height() ) );
	}
	for ( double y = std::floor( bottomRight.y() / flStep ) * flStep; y <= topLeft.y(); y += flStep )
	{
		painter.drawLine( QPointF( 0, ToWidget( { 0, y } ).y() ), QPointF( width(), ToWidget( { 0, y } ).y() ) );
	}

	const QColor accent = palette().highlight().color();
	for ( int i = 0; i < 8; i++ )
	{
		const QPointF at = ToWidget( scene.m_Positions[ static_cast<std::size_t>( i ) ] );
		const bool bUsed = m_Used.contains( i );
		if ( const auto heard = m_Heard[ static_cast<std::size_t>( i ) ]; heard && m_Clock.elapsed() - *heard < FADE_MS )
		{
			const double flAge = static_cast<double>( m_Clock.elapsed() - *heard ) / FADE_MS;
			QColor ring = accent;
			ring.setAlphaF( static_cast<float>( 1.0 - flAge ) );
			painter.setPen( QPen( ring, 2.0 ) );
			painter.setBrush( Qt::NoBrush );
			painter.drawEllipse( at, MARKER_RADIUS + 18.0 * flAge, MARKER_RADIUS + 18.0 * flAge );
		}
		QColor fill = bUsed ? accent : palette().mid().color();
		fill.setAlphaF( bUsed ? 0.9f : 0.5f );
		painter.setPen( Qt::NoPen );
		painter.setBrush( fill );
		painter.drawEllipse( at, MARKER_RADIUS, MARKER_RADIUS );
		painter.setPen( bUsed ? palette().highlightedText().color() : palette().text().color() );
		painter.drawText( QRectF( at.x() - MARKER_RADIUS, at.y() - MARKER_RADIUS, 2 * MARKER_RADIUS, 2 * MARKER_RADIUS ), Qt::AlignCenter, QString::number( i ) );
	}

	// The listener, facing up (ahead).
	const QPointF centre = ToWidget( scene.m_Listener );
	painter.setPen( QPen( palette().text().color(), 1.5 ) );
	painter.setBrush( palette().window() );
	painter.drawEllipse( centre, 9.0, 9.0 );
	painter.drawLine( centre, centre + QPointF( 0, -15 ) );
	painter.drawLine( centre + QPointF( 0, -15 ), centre + QPointF( -4, -10 ) );
	painter.drawLine( centre + QPointF( 0, -15 ), centre + QPointF( 4, -10 ) );

	painter.setPen( palette().placeholderText().color() );
	painter.drawText( rect().adjusted( 6, 4, -6, -4 ), Qt::AlignLeft | Qt::AlignBottom,
		scene.m_bOn ? tr( "grid: %1 units · stereo, so behind sounds like ahead" ).arg( flStep ) : tr( "listening off: positions aren't simulated" ) );
}

//-----------------------------------------------------------------------------
// Purpose: Positions win over the listener when they overlap, so a stacked one can be pulled out.
//-----------------------------------------------------------------------------
std::optional<int> CListenCanvas::HitAt( const QPointF &at ) const
{
	const CSoundPlayer::ListenScene_t &scene = m_pPlayer->GetListenScene();
	for ( int i = 7; i >= 0; i-- )
	{
		if ( QLineF( at, ToWidget( scene.m_Positions[ static_cast<std::size_t>( i ) ] ) ).length() <= MARKER_RADIUS + 2.0 )
		{
			return i;
		}
	}
	if ( QLineF( at, ToWidget( scene.m_Listener ) ).length() <= 12.0 )
	{
		return LISTENER;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CListenCanvas::mousePressEvent( QMouseEvent *pEvent )
{
	m_Dragging = pEvent->button() == Qt::LeftButton ? HitAt( pEvent->position() ) : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: A dragged position follows the mouse; the dragged listener moves the world under the
//          view, so the view keeps it centred while the positions slide the other way.
//-----------------------------------------------------------------------------
void CListenCanvas::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( !m_Dragging )
	{
		setCursor( HitAt( pEvent->position() ) ? Qt::OpenHandCursor : Qt::ArrowCursor );
		return;
	}
	CSoundPlayer::ListenScene_t scene = m_pPlayer->GetListenScene();
	const QPointF world = ToWorld( pEvent->position() );
	QPointF *pMoved = *m_Dragging == LISTENER ? &scene.m_Listener : &scene.m_Positions[ static_cast<std::size_t>( *m_Dragging ) ];
	*pMoved = QPointF( std::round( world.x() / 8.0 ) * 8.0, std::round( world.y() / 8.0 ) * 8.0 );
	m_pPlayer->SetListenScene( scene );
	if ( *m_Dragging != LISTENER )
	{
		const QPointF offset = *pMoved - scene.m_Listener;
		QToolTip::showText( pEvent->globalPosition().toPoint(), tr( "position %1: %2 units away" ).arg( *m_Dragging ).arg( std::lround( std::hypot( offset.x(), offset.y() ) ) ), this );
	}
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CListenCanvas::mouseReleaseEvent( QMouseEvent * )
{
	if ( m_Dragging )
	{
		m_Dragging.reset();
		SaveScene();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CListenCanvas::wheelEvent( QWheelEvent *pEvent )
{
	m_flUnitsPerPixel = std::clamp( m_flUnitsPerPixel * std::pow( 0.9, pEvent->angleDelta().y() / 120.0 ), 0.5, 32.0 );
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CListenCanvas::SaveScene() const
{
	const CSoundPlayer::ListenScene_t &scene = m_pPlayer->GetListenScene();
	QList<QVariant> saved;
	for ( const QPointF &at : scene.m_Positions )
	{
		saved.push_back( at );
	}
	saved.push_back( scene.m_Listener );
	QSettings{}.setValue( QLatin1String( SCENE_KEY ), saved );
}
