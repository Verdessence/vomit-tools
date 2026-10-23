//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SkyTestPattern.h.
//
//===========================================================================//
#include "SkyTestPattern.h"

#include <QFont>
#include <QImage>
#include <QPainter>

#include "ImageFiles.h"

using xv::sky::Edge_t;
using xv::sky::Face_t;

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QColor Background( Face_t nFace )
{
	switch ( nFace )
	{
		case Face_t::FACE_RT: return { 196, 92, 84 };
		case Face_t::FACE_BK: return { 92, 160, 96 };
		case Face_t::FACE_LF: return { 84, 116, 196 };
		case Face_t::FACE_FT: return { 200, 172, 72 };
		case Face_t::FACE_UP: return { 150, 196, 220 };
		case Face_t::FACE_DN: return { 120, 96, 80 };
	}
	return Qt::gray;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Heading( Face_t nFace )
{
	switch ( nFace )
	{
		case Face_t::FACE_RT: return QStringLiteral( "+X · yaw 0 · east" );
		case Face_t::FACE_BK: return QStringLiteral( "+Y · yaw 90 · north" );
		case Face_t::FACE_LF: return QStringLiteral( "−X · yaw 180 · west" );
		case Face_t::FACE_FT: return QStringLiteral( "−Y · yaw 270 · south" );
		case Face_t::FACE_UP: return QStringLiteral( "+Z · up" );
		case Face_t::FACE_DN: return QStringLiteral( "−Z · down" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Name( Face_t nFace )
{
	return QString::fromUtf8( xv::sky::Suffix( nFace ) ).toUpper();
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A labelled face for checking a sky in game: the face's name and direction in the
//          middle, and at
//          each edge the name of the face that should meet it there. If the table in sky/Sky.cpp
//          is right,
//          every seam in game shows two labels naming each other, and "setang 0 0 0" faces RT.
//-----------------------------------------------------------------------------
xv::texture::CImage SkyTestPattern( Face_t nFace, int nSize )
{
	QImage image( nSize, nSize, QImage::Format_RGBA8888 );
	image.fill( Background( nFace ) );
	QPainter painter( &image );
	painter.setRenderHint( QPainter::Antialiasing );
	painter.setRenderHint( QPainter::TextAntialiasing );
	const double s = nSize;

	// A grid, so stretching and seams are easy to see.
	painter.setPen( QPen( QColor( 255, 255, 255, 70 ), std::max( 1.0, s / 512 ) ) );
	for ( int i = 1; i < 8; i++ )
	{
		painter.drawLine( QPointF( s * i / 8, 0 ), QPointF( s * i / 8, s ) );
		painter.drawLine( QPointF( 0, s * i / 8 ), QPointF( s, s * i / 8 ) );
	}
	painter.setPen( QPen( Qt::white, std::max( 2.0, s / 128 ) ) );
	painter.drawRect( QRectF( 0, 0, s, s ) );

	QFont font;
	font.setBold( true );
	font.setPixelSize( static_cast<int>( s / 4 ) );
	painter.setFont( font );
	painter.setPen( Qt::white );
	painter.drawText( QRectF( 0, s * 0.22, s, s * 0.35 ), Qt::AlignCenter, Name( nFace ) );
	font.setPixelSize( static_cast<int>( s / 18 ) );
	painter.setFont( font );
	painter.drawText( QRectF( 0, s * 0.55, s, s * 0.1 ), Qt::AlignCenter, Heading( nFace ) );

	// Each edge names the face that should be on the other side of it, with an arrow pointing there.
	font.setPixelSize( static_cast<int>( s / 14 ) );
	painter.setFont( font );
	const double flBand = s * 0.12;
	painter.drawText( QRectF( 0, s * 0.02, s, flBand ), Qt::AlignCenter, QStringLiteral( "▲ %1" ).arg( Name( xv::sky::Neighbor( nFace, Edge_t::EDGE_TOP ) ) ) );
	painter.drawText( QRectF( 0, s * 0.98 - flBand, s, flBand ), Qt::AlignCenter, QStringLiteral( "▼ %1" ).arg( Name( xv::sky::Neighbor( nFace, Edge_t::EDGE_BOTTOM ) ) ) );
	painter.save();
	painter.translate( s * 0.02 + flBand / 2, s / 2 );
	painter.rotate( -90 );
	painter.drawText( QRectF( -s / 2, -flBand / 2, s, flBand ), Qt::AlignCenter, QStringLiteral( "▲ %1" ).arg( Name( xv::sky::Neighbor( nFace, Edge_t::EDGE_LEFT ) ) ) );
	painter.restore();
	painter.save();
	painter.translate( s * 0.98 - flBand / 2, s / 2 );
	painter.rotate( 90 );
	painter.drawText( QRectF( -s / 2, -flBand / 2, s, flBand ), Qt::AlignCenter, QStringLiteral( "▲ %1" ).arg( Name( xv::sky::Neighbor( nFace, Edge_t::EDGE_RIGHT ) ) ) );
	painter.restore();

	// The top-left corner is marked, so a turned or mirrored face shows at a glance.
	painter.setBrush( Qt::white );
	painter.setPen( Qt::NoPen );
	painter.drawPolygon( QPolygonF{ { QPointF( 0, 0 ), QPointF( s * 0.1, 0 ), QPointF( 0, s * 0.1 ) } } );
	painter.end();
	return FromQImage( image );
}
