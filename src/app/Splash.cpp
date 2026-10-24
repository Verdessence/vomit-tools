//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Splash.h.
//
//===========================================================================//
#include "Splash.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

namespace
{

// The splash's width on screen; the logo keeps its shape.
constexpr int WIDTH = 640;

//-----------------------------------------------------------------------------
// Purpose: The wide logo at the splash's size, sharp on high-density screens.
//-----------------------------------------------------------------------------
QPixmap Logo()
{
	const qreal flRatio = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
	QPixmap logo( QStringLiteral( ":/assets/logo_wide.png" ) );
	if ( logo.isNull() )
	{
		logo = QPixmap( 2 * WIDTH, WIDTH );
		logo.fill( Qt::transparent );
	}
	logo = logo.scaledToWidth( static_cast<int>( WIDTH * flRatio ), Qt::SmoothTransformation );
	logo.setDevicePixelRatio( flRatio );
	return logo;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSplash::CSplash( int nSteps )
	: QSplashScreen( Logo() ),
	  m_nSteps( std::max( 1, nSteps ) )
{
	setObjectName( QStringLiteral( "splash" ) );
	// Only the logo shows; around it, whatever is behind.
	setAttribute( Qt::WA_TranslucentBackground );
}

//-----------------------------------------------------------------------------
// Purpose: Shows the next step's name, and draws it at once.
//-----------------------------------------------------------------------------
void CSplash::SetStep( const QString &szWhat )
{
	m_nStep = std::min( m_nStep + 1, m_nSteps );
	m_szText = tr( "Loading %1 [%2/%3]" ).arg( szWhat ).arg( m_nStep ).arg( m_nSteps );
	repaint();
	QApplication::processEvents();
}

//-----------------------------------------------------------------------------
// Purpose: The step, centred under the logo: white with a dark edge, readable on any desktop.
//-----------------------------------------------------------------------------
void CSplash::drawContents( QPainter *pPainter )
{
	if ( m_szText.isEmpty() )
	{
		return;
	}
	QFont font = QApplication::font();
	font.setPixelSize( 17 );
	font.setBold( true );
	QPainterPath path;
	path.addText( 0, 0, font, m_szText );
	const QRectF bounds = path.boundingRect();
	const QSizeF size = QSizeF( pixmap().size() ) / pixmap().devicePixelRatio();
	// Below the lettering, which ends about two thirds of the way down.
	path.translate( ( size.width() - bounds.width() ) / 2.0 - bounds.left(), size.height() * 0.8 - bounds.top() );
	pPainter->setRenderHint( QPainter::Antialiasing );
	pPainter->strokePath( path, QPen( QColor( 20, 30, 20, 220 ), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
	pPainter->fillPath( path, Qt::white );
}
