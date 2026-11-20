//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Splash.h.
//
//===========================================================================//
#include "Splash.h"

#include <algorithm>

#include <QApplication>
#include <QPainter>
#include <QScreen>

namespace
{

// The splash's width on screen; the logo keeps its shape.
constexpr int WIDTH = 640;

//-----------------------------------------------------------------------------
// Purpose: Reserve room for the progress bar even with a large desktop font.
//-----------------------------------------------------------------------------
int FooterHeight()
{
	return std::max( 58, QFontMetrics( QApplication::font() ).height() + 35 );
}

//-----------------------------------------------------------------------------
// Purpose: The wide logo at the splash's size, sharp on high-density screens.
//-----------------------------------------------------------------------------
QPixmap SplashPicture()
{
	const qreal flRatio = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
	QPixmap logo( QStringLiteral( ":/assets/logo_wide.png" ) );
	if ( logo.isNull() )
	{
		logo = QPixmap( 2 * WIDTH, WIDTH );
		logo.fill( Qt::transparent );
	}
	logo = logo.scaledToWidth( qRound( WIDTH * flRatio ), Qt::SmoothTransformation );
	logo.setDevicePixelRatio( flRatio );
	QPixmap picture( logo.width(), logo.height() + qRound( FooterHeight() * flRatio ) );
	picture.setDevicePixelRatio( flRatio );
	picture.fill( Qt::transparent );
	{
		QPainter painter( &picture );
		painter.drawPixmap( 0, 0, logo );
	}
	return picture;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSplash::CSplash( int nSteps )
	: QSplashScreen( SplashPicture() ),
	  m_nSteps( std::max( 1, nSteps ) )
{
	setObjectName( QStringLiteral( "splash" ) );
	// The logo and progress panel float over the desktop.
	setAttribute( Qt::WA_TranslucentBackground );
	setCursor( Qt::WaitCursor );
}

//-----------------------------------------------------------------------------
// Purpose: Shows the next step's name, and draws it at once.
//-----------------------------------------------------------------------------
void CSplash::SetStep( const QString &szWhat )
{
	m_nStep = std::min( m_nStep + 1, m_nSteps );
	m_szText = tr( "Loading %1 [%2/%3]" ).arg( szWhat ).arg( m_nStep ).arg( m_nSteps );
	setAccessibleName( m_szText );
	repaint();
	QApplication::processEvents();
}

//-----------------------------------------------------------------------------
// Purpose: Fill the bar once the last startup step has finished.
//-----------------------------------------------------------------------------
void CSplash::Complete()
{
	m_bComplete = true;
	m_szText = tr( "Ready" );
	setAccessibleName( m_szText );
	repaint();
}

//-----------------------------------------------------------------------------
// Purpose: The status and progress have their own space below the bottom of the image.
//-----------------------------------------------------------------------------
void CSplash::drawContents( QPainter *pPainter )
{
	if ( m_szText.isEmpty() )
	{
		return;
	}
	pPainter->setRenderHint( QPainter::Antialiasing );
	const QPalette colors = palette();
	const int nFooterHeight = FooterHeight();
	const QRect panel( 8, height() - nFooterHeight + 4, width() - 16, nFooterHeight - 10 );
	pPainter->setPen( colors.color( QPalette::Mid ) );
	pPainter->setBrush( colors.brush( QPalette::Window ) );
	pPainter->drawRoundedRect( QRectF( panel ), 8, 8 );

	const QRect text = panel.adjusted( 16, 5, -16, -18 );
	pPainter->setPen( colors.color( QPalette::WindowText ) );
	pPainter->drawText( text, Qt::AlignCenter, fontMetrics().elidedText( m_szText, Qt::ElideMiddle, text.width() ) );

	const QRectF track( text.left(), panel.bottom() - 12, text.width(), 5 );
	pPainter->setPen( Qt::NoPen );
	pPainter->setBrush( colors.brush( QPalette::Mid ) );
	pPainter->drawRoundedRect( track, 2.5, 2.5 );
	const int nDone = m_bComplete ? m_nSteps : m_nStep - 1;
	pPainter->setBrush( colors.brush( QPalette::Highlight ) );
	pPainter->drawRoundedRect( QRectF( track.topLeft(), QSizeF( track.width() * nDone / m_nSteps, track.height() ) ), 2.5, 2.5 );
}
