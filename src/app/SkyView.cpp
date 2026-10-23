//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SkyView.h.
//
//===========================================================================//
#include "SkyView.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

using xv::sky::Face_t;

namespace
{

constexpr float MIN_FOV = 20.0f;
constexpr float MAX_FOV = 130.0f;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float WrapYaw( float flYaw )
{
	flYaw = std::fmod( flYaw, 360.0f );
	return flYaw < 0.0f ? flYaw + 360.0f : flYaw;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void OutlinedText( QPainter &painter, QPointF centre, const QString &szText, const QFont &font, const QColor &colour )
{
	QPainterPath path;
	const QFontMetricsF metrics{ font };
	path.addText( centre.x() - metrics.horizontalAdvance( szText ) / 2, centre.y() + metrics.capHeight() / 2, font, szText );
	painter.setPen( QPen( QColor( 0, 0, 0, 170 ), 3 ) );
	painter.setBrush( Qt::NoBrush );
	painter.drawPath( path );
	painter.fillPath( path, colour );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSkyView::CSkyView( QWidget *pParent )
	: QWidget( pParent )
{
	setMinimumSize( 240, 180 );
	setFocusPolicy( Qt::StrongFocus );
	setCursor( Qt::OpenHandCursor );
	setToolTip( tr( "Drag to look around, scroll to zoom, double-click to look east (yaw 0) again" ) );
}

//-----------------------------------------------------------------------------
// Purpose: The cube is not owned; call Refresh() after it changes. nullptr shows nothing.
//-----------------------------------------------------------------------------
void CSkyView::SetCube( const xv::sky::CCube *pCube )
{
	m_pCube = pCube;
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::Refresh()
{
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::SetAngles( xv::sky::Angles_t angles )
{
	m_Angles = { std::clamp( angles.m_flPitch, -89.0f, 89.0f ), WrapYaw( angles.m_flYaw ) };
	update();
	emit ViewChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::SetFov( float flFov )
{
	m_flFov = std::clamp( flFov, MIN_FOV, MAX_FOV );
	update();
	emit ViewChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::SetShowLabels( bool bShow )
{
	m_bLabels = bShow;
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::SetShowEdges( bool bShow )
{
	m_bEdges = bShow;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: "setang 0 90 0": the console command (sv_cheats 1) for the same view in game.
//-----------------------------------------------------------------------------
QString CSkyView::GetSetangCommand() const
{
	return QStringLiteral( "setang %1 %2 0" ).arg( std::lround( m_Angles.m_flPitch ) ).arg( std::lround( m_Angles.m_flYaw ) % 360 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::Look( float flYawDelta, float flPitchDelta )
{
	SetAngles( { m_Angles.m_flPitch + flPitchDelta, m_Angles.m_flYaw + flYawDelta } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	painter.fillRect( rect(), QColor( 24, 24, 28 ) );
	if ( !m_pCube || std::ranges::all_of( m_pCube->m_Faces, &xv::texture::CImage::Empty ) )
	{
		painter.setPen( palette().color( QPalette::PlaceholderText ) );
		painter.drawText( rect(), Qt::AlignCenter, tr( "Add face images to see the sky here." ) );
		return;
	}
	const xv::sky::CView view{ m_Angles, m_flFov, width(), height() };
	xv::sky::Render( *m_pCube, view, m_Frame, m_bEdges );
	const QImage image( m_Frame.m_Pixels.data(), m_Frame.m_nWidth, m_Frame.m_nHeight, m_Frame.m_nWidth * 4, QImage::Format_RGBA8888 );
	painter.drawImage( 0, 0, image );

	painter.setRenderHint( QPainter::Antialiasing );
	if ( m_bLabels )
	{
		QFont font = this->font();
		font.setBold( true );
		font.setPixelSize( std::clamp( height() / 14, 12, 40 ) );
		for ( Face_t nFace : xv::sky::FACES )
		{
			if ( const auto point = view.ToScreen( xv::sky::Basis( nFace ).m_vecNormal ) )
			{
				OutlinedText( painter, { ( *point )[ 0 ], ( *point )[ 1 ] }, QString::fromUtf8( xv::sky::Suffix( nFace ) ).toUpper(), font, QColor( 255, 255, 255, 220 ) );
			}
		}
	}
	QFont small = font();
	small.setPixelSize( 12 );
	painter.setFont( small );
	const QString szStatus = tr( "%1 · fov %2" ).arg( GetSetangCommand() ).arg( std::lround( m_flFov ) );
	const QRect box = painter.fontMetrics().boundingRect( szStatus ).adjusted( -6, -3, 6, 3 );
	const QRect placed = box.translated( 8 - box.left(), height() - 8 - box.bottom() );
	painter.fillRect( placed, QColor( 0, 0, 0, 150 ) );
	painter.setPen( Qt::white );
	painter.drawText( placed, Qt::AlignCenter, szStatus );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::mousePressEvent( QMouseEvent *pEvent )
{
	m_LastMouse = pEvent->position().toPoint();
	if ( pEvent->button() == Qt::LeftButton )
	{
		setCursor( Qt::ClosedHandCursor );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( !( pEvent->buttons() & Qt::LeftButton ) )
	{
		setCursor( Qt::OpenHandCursor );
		return;
	}
	const QPoint position = pEvent->position().toPoint();
	const QPoint delta = position - m_LastMouse;
	m_LastMouse = position;
	// Grab the sky: it follows the mouse. Degrees per pixel from the vertical field of view.
	const float flDegrees = 2.0f * std::atan( std::tan( m_flFov * 0.5f * 3.14159265f / 180.0f ) * 0.75f ) * 180.0f / 3.14159265f / static_cast<float>( std::max( height(), 1 ) );
	Look( static_cast<float>( delta.x() ) * flDegrees, -static_cast<float>( delta.y() ) * flDegrees );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::mouseDoubleClickEvent( QMouseEvent * )
{
	SetFov( 90.0f );
	SetAngles( {} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::wheelEvent( QWheelEvent *pEvent )
{
	const float flSteps = static_cast<float>( pEvent->angleDelta().y() ) / 120.0f;
	SetFov( m_flFov * std::pow( 0.9f, flSteps ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::keyPressEvent( QKeyEvent *pEvent )
{
	switch ( pEvent->key() )
	{
		case Qt::Key_Left: Look( 5.0f, 0.0f ); break;
		case Qt::Key_Right: Look( -5.0f, 0.0f ); break;
		case Qt::Key_Up: Look( 0.0f, -5.0f ); break;
		case Qt::Key_Down: Look( 0.0f, 5.0f ); break;
		case Qt::Key_Home: SetAngles( {} ); break;
		default: QWidget::keyPressEvent( pEvent );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyView::contextMenuEvent( QContextMenuEvent *pEvent )
{
	QMenu menu( this );
	menu.addAction( tr( "Copy \"%1\"" ).arg( GetSetangCommand() ), this, [ this ] { QApplication::clipboard()->setText( GetSetangCommand() ); } );
	menu.addSeparator();
	const std::array<std::pair<QString, xv::sky::Angles_t>, 6> looks{ {
		{ tr( "Look at rt (east, yaw 0)" ), { 0, 0 } },
		{ tr( "Look at bk (north, yaw 90)" ), { 0, 90 } },
		{ tr( "Look at lf (west, yaw 180)" ), { 0, 180 } },
		{ tr( "Look at ft (south, yaw 270)" ), { 0, 270 } },
		{ tr( "Look up" ), { -89, 0 } },
		{ tr( "Look down" ), { 89, 0 } },
	} };
	for ( const auto &[ label, angles ] : looks )
	{
		menu.addAction( label, this, [ this, angles ] { SetAngles( angles ); } );
	}
	menu.exec( pEvent->globalPos() );
}
