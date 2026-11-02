//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapToolBars.h.
//
//===========================================================================//
#include "MapToolBars.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

#include <QAction>
#include <QActionGroup>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{

// Each bar's buttons ("[bar]" then its entries), every tab's.
constexpr char LAYOUT_SETTING[] = "maps/toolBars/layout";
// Where the bars are: QMainWindow::saveState.
constexpr char STATE_SETTING[] = "maps/toolBars/state";
constexpr char STYLE_SETTING[] = "maps/toolBars/style";
constexpr int STATE_VERSION = 1;
constexpr char GAP[] = "-";

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Our own pictures for the tool bar (Hammer's are Valve's to keep).
//-----------------------------------------------------------------------------
QIcon MakeMapIcon( MapIcon_t nIcon, const QColor &color )
{
	QPixmap pixmap( 32, 32 );
	pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap );
	painter.setRenderHint( QPainter::Antialiasing );
	painter.setPen( QPen{ color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin } );
	switch ( nIcon )
	{
		case MapIcon_t::MAP_ICON_SELECT:
		{
			QPainterPath arrow;
			arrow.moveTo( 9, 5 );
			arrow.lineTo( 9, 25 );
			arrow.lineTo( 14, 20 );
			arrow.lineTo( 18, 28 );
			arrow.lineTo( 21, 27 );
			arrow.lineTo( 17, 19 );
			arrow.lineTo( 24, 19 );
			arrow.closeSubpath();
			painter.drawPath( arrow );
			break;
		}
		case MapIcon_t::MAP_ICON_MAGNIFY:
			painter.drawEllipse( QPointF{ 13, 13 }, 7.5, 7.5 );
			painter.drawLine( QPointF{ 18.5, 18.5 }, QPointF{ 27, 27 } );
			break;
		case MapIcon_t::MAP_ICON_CAMERA:
			painter.drawRoundedRect( QRectF{ 4, 10, 18, 13 }, 2, 2 );
			painter.drawPolygon( QPolygonF{ { QPointF{ 22, 14 }, QPointF{ 29, 10 }, QPointF{ 29, 23 }, QPointF{ 22, 19 } } } );
			break;
		case MapIcon_t::MAP_ICON_ENTITY:
			// A marker where the entity goes.
			painter.drawEllipse( QPointF{ 16, 16 }, 8.5, 8.5 );
			painter.drawLine( QPointF{ 16, 3 }, QPointF{ 16, 29 } );
			painter.drawLine( QPointF{ 3, 16 }, QPointF{ 29, 16 } );
			break;
		case MapIcon_t::MAP_ICON_BLOCK:
		{
			// A box seen from a corner.
			const QPointF front[ 4 ] = { { 5, 12 }, { 20, 12 }, { 20, 27 }, { 5, 27 } };
			const QPointF back{ 7, -7 };
			painter.drawPolygon( front, 4 );
			painter.drawLine( front[ 0 ], front[ 0 ] + back );
			painter.drawLine( front[ 1 ], front[ 1 ] + back );
			painter.drawLine( front[ 2 ], front[ 2 ] + back );
			painter.drawLine( front[ 0 ] + back, front[ 1 ] + back );
			painter.drawLine( front[ 1 ] + back, front[ 2 ] + back );
			break;
		}
		case MapIcon_t::MAP_ICON_VERTEX:
		{
			// A box with its corners marked.
			painter.drawRect( QRectF{ 7, 7, 18, 18 } );
			painter.setBrush( color );
			for ( const QPointF corner : { QPointF{ 7, 7 }, QPointF{ 25, 7 }, QPointF{ 7, 25 }, QPointF{ 25, 25 } } )
			{
				painter.drawRect( QRectF{ corner.x() - 3, corner.y() - 3, 6, 6 } );
			}
			break;
		}
		case MapIcon_t::MAP_ICON_CORDON:
			// A dashed fence round a small box.
			painter.setPen( QPen{ color, 2.0, Qt::DashLine } );
			painter.drawRect( QRectF{ 4, 4, 24, 24 } );
			painter.setPen( QPen{ color, 2.0 } );
			painter.drawRect( QRectF{ 11, 11, 10, 10 } );
			break;
		case MapIcon_t::MAP_ICON_CLIP:
			// A box with a cut through it.
			painter.drawRect( QRectF{ 6, 8, 20, 16 } );
			painter.setPen( QPen{ color, 2.5, Qt::DashLine, Qt::RoundCap } );
			painter.drawLine( QPointF{ 3, 28 }, QPointF{ 29, 4 } );
			break;
		case MapIcon_t::MAP_ICON_OVERLAY:
			painter.drawPolygon( QPolygonF{ { 5, 9 }, { 23, 4 }, { 28, 24 }, { 8, 28 } } );
			painter.setBrush( color );
			for ( const QPointF &at : { QPointF{ 5, 9 }, QPointF{ 23, 4 }, QPointF{ 28, 24 }, QPointF{ 8, 28 } } )
				painter.drawRect( QRectF{ at.x() - 2, at.y() - 2, 4, 4 } );
			break;
		case MapIcon_t::MAP_ICON_DECAL:
			// A stamp above a marked surface.
			painter.drawLine( QPointF{ 4, 26 }, QPointF{ 28, 26 } );
			painter.drawRect( QRectF{ 9, 15, 14, 6 } );
			painter.drawLine( QPointF{ 16, 6 }, QPointF{ 16, 15 } );
			painter.drawEllipse( QPointF{ 16, 6 }, 3, 3 );
			break;
		case MapIcon_t::MAP_ICON_PROPERTIES:
			// A sheet of keys and their values.
			painter.drawRoundedRect( QRectF{ 5, 4, 22, 24 }, 2, 2 );
			for ( const double y : { 11.0, 16.5, 22.0 } )
			{
				painter.drawLine( QPointF{ 9, y }, QPointF{ 13, y } );
				painter.drawLine( QPointF{ 16, y }, QPointF{ 23, y } );
			}
			break;
		case MapIcon_t::MAP_ICON_MATERIAL:
		{
			// A ball with a shine on it, as a material is shown.
			painter.drawEllipse( QPointF{ 16, 16 }, 11, 11 );
			painter.setPen( Qt::NoPen );
			painter.setBrush( color );
			painter.drawEllipse( QPointF{ 12, 12 }, 3.5, 3.5 );
			break;
		}
		case MapIcon_t::MAP_ICON_SPRINKLE:
		{
			// A can's spray of dots onto a surface.
			painter.drawLine( QPointF{ 4, 27 }, QPointF{ 28, 27 } );
			painter.drawRoundedRect( QRectF{ 5, 4, 7, 11 }, 1.5, 1.5 );
			painter.setPen( Qt::NoPen );
			painter.setBrush( color );
			for ( const QPointF &at : { QPointF{ 16, 10 }, QPointF{ 21, 14 }, QPointF{ 17, 18 }, QPointF{ 25, 20 }, QPointF{ 13, 22 }, QPointF{ 21, 24 } } )
				painter.drawEllipse( at, 1.6, 1.6 );
			break;
		}
		case MapIcon_t::MAP_ICON_LIGHT:
			// A sun: a disc and its rays.
			painter.drawEllipse( QPointF{ 16, 16 }, 6, 6 );
			for ( int i = 0; i < 8; i++ )
			{
				const double flAngle = i * std::numbers::pi / 4.0;
				painter.drawLine( QPointF{ 16 + 9.5 * std::cos( flAngle ), 16 + 9.5 * std::sin( flAngle ) }, QPointF{ 16 + 13.5 * std::cos( flAngle ), 16 + 13.5 * std::sin( flAngle ) } );
			}
			break;
		case MapIcon_t::MAP_ICON_TEXTURE:
			// A face with a texture's checker on it.
			painter.drawRect( QRectF{ 5, 5, 22, 22 } );
			painter.setBrush( color );
			painter.setPen( Qt::NoPen );
			painter.drawRect( QRectF{ 5, 5, 11, 11 } );
			painter.drawRect( QRectF{ 16, 16, 11, 11 } );
			break;
		case MapIcon_t::MAP_ICON_SELECT_GROUPS:
			// Two boxes held in one bracket.
			painter.drawRect( QRectF{ 7, 9, 8, 8 } );
			painter.drawRect( QRectF{ 17, 15, 8, 8 } );
			painter.setPen( QPen{ color, 2.0, Qt::DashLine } );
			painter.drawRoundedRect( QRectF{ 3, 5, 26, 22 }, 3, 3 );
			break;
		case MapIcon_t::MAP_ICON_SELECT_OBJECTS:
			// A box with its entity's marker.
			painter.drawRect( QRectF{ 5, 11, 15, 15 } );
			painter.drawEllipse( QPointF{ 23, 9 }, 4.5, 4.5 );
			painter.drawLine( QPointF{ 20, 12 }, QPointF{ 17, 15 } );
			break;
		case MapIcon_t::MAP_ICON_SELECT_SOLIDS:
			// One box picked out of two.
			painter.drawRect( QRectF{ 4, 13, 11, 11 } );
			painter.setBrush( color );
			painter.drawRect( QRectF{ 17, 8, 11, 11 } );
			break;
		case MapIcon_t::MAP_ICON_GIZMO_MOVE:
			// Arrows out four ways.
			painter.drawLine( QPointF{ 16, 4 }, QPointF{ 16, 28 } );
			painter.drawLine( QPointF{ 4, 16 }, QPointF{ 28, 16 } );
			for ( const auto &[ tip, a, b ] : { std::array<QPointF, 3>{ QPointF{ 16, 4 }, QPointF{ 12, 8 }, QPointF{ 20, 8 } }, { QPointF{ 16, 28 }, QPointF{ 12, 24 }, QPointF{ 20, 24 } }, { QPointF{ 4, 16 }, QPointF{ 8, 12 }, QPointF{ 8, 20 } }, { QPointF{ 28, 16 }, QPointF{ 24, 12 }, QPointF{ 24, 20 } } } )
			{
				painter.drawLine( tip, a );
				painter.drawLine( tip, b );
			}
			break;
		case MapIcon_t::MAP_ICON_GIZMO_ROTATE:
			// Most of a circle with an arrow head.
			painter.drawArc( QRectF{ 6, 6, 20, 20 }, 30 * 16, 300 * 16 );
			painter.drawLine( QPointF{ 24.7, 11 }, QPointF{ 26, 4.5 } );
			painter.drawLine( QPointF{ 24.7, 11 }, QPointF{ 18.5, 9.5 } );
			break;
		case MapIcon_t::MAP_ICON_GIZMO_SCALE:
			// A small box grown into a bigger one.
			painter.drawRect( QRectF{ 4, 14, 14, 14 } );
			painter.drawLine( QPointF{ 14, 18 }, QPointF{ 27, 5 } );
			painter.drawLine( QPointF{ 27, 5 }, QPointF{ 20, 5 } );
			painter.drawLine( QPointF{ 27, 5 }, QPointF{ 27, 12 } );
			break;
		case MapIcon_t::MAP_ICON_GIZMO_SHEAR:
			painter.drawPolygon( QPolygonF{ { QPointF{ 10, 7 }, QPointF{ 28, 7 }, QPointF{ 22, 25 }, QPointF{ 4, 25 } } } );
			break;
		case MapIcon_t::MAP_ICON_GIZMO_ALL:
			// The arrows and the ring together.
			painter.drawEllipse( QPointF{ 16, 16 }, 10, 10 );
			painter.drawLine( QPointF{ 16, 3 }, QPointF{ 16, 29 } );
			painter.drawLine( QPointF{ 3, 16 }, QPointF{ 29, 16 } );
			break;
		case MapIcon_t::MAP_ICON_GRID:
		case MapIcon_t::MAP_ICON_GRID_SMALLER:
		case MapIcon_t::MAP_ICON_GRID_LARGER:
		{
			// The grid; smaller and larger with a minus or plus in the corner.
			const bool bSign = nIcon != MapIcon_t::MAP_ICON_GRID;
			const double flSize = bSign ? 18 : 24;
			painter.setPen( QPen{ color, 1.6 } );
			for ( int i = 0; i <= 3; i++ )
			{
				const double flAt = 4 + i * flSize / 3;
				painter.drawLine( QPointF{ flAt, 4 }, QPointF{ flAt, 4 + flSize } );
				painter.drawLine( QPointF{ 4, flAt }, QPointF{ 4 + flSize, flAt } );
			}
			if ( bSign )
			{
				painter.setPen( QPen{ color, 2.5, Qt::SolidLine, Qt::RoundCap } );
				painter.drawLine( QPointF{ 20, 25 }, QPointF{ 29, 25 } );
				if ( nIcon == MapIcon_t::MAP_ICON_GRID_LARGER )
					painter.drawLine( QPointF{ 24.5, 20.5 }, QPointF{ 24.5, 29.5 } );
			}
			break;
		}
		case MapIcon_t::MAP_ICON_SNAP:
			// A magnet.
			painter.setPen( QPen{ color, 4.0, Qt::SolidLine, Qt::FlatCap } );
			painter.drawArc( QRectF{ 7, 9, 18, 18 }, 180 * 16, 180 * 16 );
			painter.drawLine( QPointF{ 9, 18 }, QPointF{ 9, 6 } );
			painter.drawLine( QPointF{ 23, 18 }, QPointF{ 23, 6 } );
			break;
		case MapIcon_t::MAP_ICON_TEXTURE_LOCK:
		case MapIcon_t::MAP_ICON_SCALE_LOCK:
			// A padlock, with a checker (texture) or an arrow (scale) on it.
			painter.drawArc( QRectF{ 10, 4, 12, 14 }, 0, 180 * 16 );
			painter.drawLine( QPointF{ 10, 11 }, QPointF{ 10, 14 } );
			painter.drawLine( QPointF{ 22, 11 }, QPointF{ 22, 14 } );
			painter.drawRoundedRect( QRectF{ 6, 14, 20, 14 }, 2, 2 );
			if ( nIcon == MapIcon_t::MAP_ICON_TEXTURE_LOCK )
			{
				painter.setPen( Qt::NoPen );
				painter.setBrush( color );
				painter.drawRect( QRectF{ 11, 18, 5, 4 } );
				painter.drawRect( QRectF{ 16, 22, 5, 4 } );
			}
			else
			{
				painter.setPen( QPen{ color, 1.8, Qt::SolidLine, Qt::RoundCap } );
				painter.drawLine( QPointF{ 11, 21 }, QPointF{ 21, 21 } );
				painter.drawLine( QPointF{ 21, 21 }, QPointF{ 18, 18.5 } );
				painter.drawLine( QPointF{ 21, 21 }, QPointF{ 18, 23.5 } );
			}
			break;
		case MapIcon_t::MAP_ICON_PIVOT:
			// A pin a turn goes about.
			painter.drawArc( QRectF{ 5, 5, 22, 22 }, 200 * 16, 250 * 16 );
			painter.setBrush( color );
			painter.drawEllipse( QPointF{ 16, 16 }, 3.5, 3.5 );
			break;
		case MapIcon_t::MAP_ICON_MODELS_2D:
			// A crate drawn flat.
			painter.drawRect( QRectF{ 6, 6, 20, 20 } );
			painter.drawLine( QPointF{ 6, 6 }, QPointF{ 26, 26 } );
			painter.drawLine( QPointF{ 26, 6 }, QPointF{ 6, 26 } );
			break;
		case MapIcon_t::MAP_ICON_NAMES:
		{
			// A tag.
			painter.drawPolygon( QPolygonF{ { QPointF{ 4, 10 }, QPointF{ 20, 10 }, QPointF{ 28, 16 }, QPointF{ 20, 22 }, QPointF{ 4, 22 } } } );
			painter.drawLine( QPointF{ 8, 16 }, QPointF{ 16, 16 } );
			break;
		}
		case MapIcon_t::MAP_ICON_HELPERS:
			// A cone of light from a point, as a spotlight's helper.
			painter.drawLine( QPointF{ 8, 6 }, QPointF{ 4, 27 } );
			painter.drawLine( QPointF{ 8, 6 }, QPointF{ 28, 22 } );
			painter.setPen( QPen{ color, 2.0, Qt::DashLine } );
			painter.drawLine( QPointF{ 4, 27 }, QPointF{ 28, 22 } );
			painter.setBrush( color );
			painter.drawEllipse( QPointF{ 8, 6 }, 2.5, 2.5 );
			break;
		case MapIcon_t::MAP_ICON_TOOL_TEXTURES:
			// A face in stripes, as the tool textures are.
			painter.drawRect( QRectF{ 5, 5, 22, 22 } );
			painter.setClipRect( QRectF{ 5, 5, 22, 22 } );
			for ( int i = -2; i < 6; i++ )
				painter.drawLine( QPointF{ 5.0 + i * 7, 27 }, QPointF{ 27.0 + i * 7, 5 } );
			painter.setClipping( false );
			break;
		case MapIcon_t::MAP_ICON_NODRAW:
			// A face that isn't drawn: dashed, crossed out.
			painter.setPen( QPen{ color, 2.0, Qt::DashLine } );
			painter.drawRect( QRectF{ 5, 5, 22, 22 } );
			painter.setPen( QPen{ color, 2.0 } );
			painter.drawLine( QPointF{ 9, 9 }, QPointF{ 23, 23 } );
			painter.drawLine( QPointF{ 23, 9 }, QPointF{ 9, 23 } );
			break;
		case MapIcon_t::MAP_ICON_SKYBOX:
			// Hills under a sky, in a box.
			painter.drawRect( QRectF{ 4, 6, 24, 20 } );
			painter.drawPolyline( QPolygonF{ { QPointF{ 4, 22 }, QPointF{ 12, 14 }, QPointF{ 17, 19 }, QPointF{ 21, 15 }, QPointF{ 28, 21 } } } );
			painter.drawEllipse( QPointF{ 22, 10.5 }, 1.8, 1.8 );
			break;
		case MapIcon_t::MAP_ICON_FOG:
			// Wavy bands of mist, fading off.
			for ( int i = 0; i < 4; i++ )
			{
				QPainterPath path;
				const double y = 8.0 + i * 6.0;
				path.moveTo( 4, y );
				path.cubicTo( 10, y - 3, 14, y + 3, 20, y );
				path.cubicTo( 23, y - 1.5, 26, y - 1.5, 28, y );
				QColor faded = color;
				faded.setAlphaF( 1.0 - i * 0.2 );
				painter.setPen( QPen{ faded, 2.0, Qt::SolidLine, Qt::RoundCap } );
				painter.drawPath( path );
			}
			break;
		case MapIcon_t::MAP_ICON_WALKABLE:
			// Steps a player can walk up.
			painter.drawPolyline( QPolygonF{ { QPointF{ 4, 27 }, QPointF{ 4, 22 }, QPointF{ 12, 22 }, QPointF{ 12, 16 }, QPointF{ 20, 16 }, QPointF{ 20, 10 }, QPointF{ 28, 10 }, QPointF{ 28, 27 } } } );
			painter.setBrush( color );
			painter.drawEllipse( QPointF{ 8, 15 }, 2.5, 2.5 );
			break;
		case MapIcon_t::MAP_ICON_CENTER:
			// A box in a view's crosshair.
			painter.drawRect( QRectF{ 10, 10, 12, 12 } );
			for ( const auto &[ a, b ] : { std::pair{ QPointF{ 16, 3 }, QPointF{ 16, 7 } }, { QPointF{ 16, 25 }, QPointF{ 16, 29 } }, { QPointF{ 3, 16 }, QPointF{ 7, 16 } }, { QPointF{ 25, 16 }, QPointF{ 29, 16 } } } )
				painter.drawLine( a, b );
			break;
		case MapIcon_t::MAP_ICON_GO_TO_BRUSH:
		{
			// A number sign over a box.
			painter.drawRect( QRectF{ 4, 14, 14, 14 } );
			painter.setPen( QPen{ color, 2.0, Qt::SolidLine, Qt::RoundCap } );
			painter.drawLine( QPointF{ 20, 4 }, QPointF{ 18, 16 } );
			painter.drawLine( QPointF{ 26, 4 }, QPointF{ 24, 16 } );
			painter.drawLine( QPointF{ 16, 8 }, QPointF{ 29, 8 } );
			painter.drawLine( QPointF{ 15, 12 }, QPointF{ 28, 12 } );
			break;
		}
		case MapIcon_t::MAP_ICON_AUTOSIZE:
			// The four panes made even.
			painter.drawRect( QRectF{ 4, 5, 24, 22 } );
			painter.drawLine( QPointF{ 16, 5 }, QPointF{ 16, 27 } );
			painter.drawLine( QPointF{ 4, 16 }, QPointF{ 28, 16 } );
			break;
		case MapIcon_t::MAP_ICON_RUN_MAP:
			// Play.
			painter.setBrush( color );
			painter.drawPolygon( QPolygonF{ { QPointF{ 9, 5 }, QPointF{ 26, 16 }, QPointF{ 9, 27 } } } );
			break;
		case MapIcon_t::MAP_ICON_CARVE:
			// A box with a bite out of it.
			painter.drawPolyline( QPolygonF{ { QPointF{ 14, 6 }, QPointF{ 4, 6 }, QPointF{ 4, 26 }, QPointF{ 28, 26 }, QPointF{ 28, 18 } } } );
			painter.setPen( QPen{ color, 2.0, Qt::DashLine } );
			painter.drawRect( QRectF{ 14, 4, 14, 14 } );
			break;
		case MapIcon_t::MAP_ICON_HOLLOW:
			painter.drawRect( QRectF{ 4, 4, 24, 24 } );
			painter.drawRect( QRectF{ 10, 10, 12, 12 } );
			break;
		case MapIcon_t::MAP_ICON_GROUP:
		case MapIcon_t::MAP_ICON_UNGROUP:
			// Two boxes, joined or pulled apart.
			if ( nIcon == MapIcon_t::MAP_ICON_GROUP )
			{
				painter.drawRect( QRectF{ 5, 9, 10, 10 } );
				painter.drawRect( QRectF{ 17, 13, 10, 10 } );
				painter.drawLine( QPointF{ 15, 14 }, QPointF{ 17, 18 } );
				painter.drawLine( QPointF{ 4, 27 }, QPointF{ 28, 27 } );
			}
			else
			{
				painter.drawRect( QRectF{ 3, 6, 10, 10 } );
				painter.drawRect( QRectF{ 19, 16, 10, 10 } );
				painter.setPen( QPen{ color, 2.0, Qt::DashLine } );
				painter.drawLine( QPointF{ 4, 27 }, QPointF{ 28, 27 } );
			}
			break;
		case MapIcon_t::MAP_ICON_TIE:
			// A box tied to an entity's marker.
			painter.drawRect( QRectF{ 4, 14, 14, 14 } );
			painter.drawEllipse( QPointF{ 24, 8 }, 4.5, 4.5 );
			painter.setPen( QPen{ color, 2.0, Qt::DotLine } );
			painter.drawLine( QPointF{ 18, 14 }, QPointF{ 21, 11 } );
			break;
		case MapIcon_t::MAP_ICON_MOVE_TO_WORLD:
			// A box set down on the ground.
			painter.drawRect( QRectF{ 9, 6, 14, 14 } );
			painter.drawLine( QPointF{ 3, 26 }, QPointF{ 29, 26 } );
			painter.drawLine( QPointF{ 16, 20 }, QPointF{ 16, 24 } );
			break;
		case MapIcon_t::MAP_ICON_HIDE:
		case MapIcon_t::MAP_ICON_HIDE_OTHERS:
		case MapIcon_t::MAP_ICON_UNHIDE:
		{
			// An eye, struck out to hide; others: a box beside it.
			QPainterPath eye;
			eye.moveTo( 3, 16 );
			eye.quadTo( 16, 4, 29, 16 );
			eye.quadTo( 16, 28, 3, 16 );
			painter.drawPath( eye );
			painter.drawEllipse( QPointF{ 16, 16 }, 3.5, 3.5 );
			if ( nIcon != MapIcon_t::MAP_ICON_UNHIDE )
				painter.drawLine( QPointF{ 6, 27 }, QPointF{ 26, 5 } );
			if ( nIcon == MapIcon_t::MAP_ICON_HIDE_OTHERS )
			{
				painter.setBrush( color );
				painter.drawRect( QRectF{ 22, 22, 7, 7 } );
			}
			break;
		}
		case MapIcon_t::MAP_ICON_PARTICLES:
		case MapIcon_t::MAP_ICON_PARTICLES_SELECTED:
		{
			// Sparks thrown up from a point, smaller as they go; for the selected ones alone, inside
			// a dashed selection box, fewer of them.
			const bool bSelected = nIcon == MapIcon_t::MAP_ICON_PARTICLES_SELECTED;
			if ( bSelected )
			{
				QPen dashed = painter.pen();
				dashed.setStyle( Qt::DashLine );
				painter.setPen( dashed );
				painter.drawRect( QRectF{ 2, 2, 28, 28 } );
			}
			painter.setPen( Qt::NoPen );
			painter.setBrush( color );
			painter.drawEllipse( QPointF{ 16, 27 }, 2.5, 2.5 );
			const struct
			{
				double x, y, r;
			} sparks[] = { { 9, 19, 2.6 }, { 16, 17, 2.8 }, { 23, 19, 2.6 }, { 6, 11, 2.0 }, { 13, 9, 2.1 }, { 20, 8, 2.1 }, { 27, 11, 2.0 }, { 10, 3.5, 1.4 }, { 17, 2.5, 1.4 }, { 24, 4, 1.4 } };
			for ( const auto &spark : sparks )
				if ( !bSelected || spark.r > 2.5 || spark.y > 6 )
					painter.drawEllipse( QPointF{ spark.x, spark.y }, spark.r, spark.r );
			break;
		}
		case MapIcon_t::MAP_ICON_STRIPPER:
			// A config's page with a minus and a plus: what it takes out and puts in.
			painter.drawRoundedRect( QRectF{ 6, 3, 20, 26 }, 2, 2 );
			painter.drawLine( QPointF{ 10, 11 }, QPointF{ 17, 11 } );
			painter.drawLine( QPointF{ 10, 21 }, QPointF{ 17, 21 } );
			painter.drawLine( QPointF{ 13.5, 17.5 }, QPointF{ 13.5, 24.5 } );
			break;
	}
	return QIcon{ pixmap };
}

//-----------------------------------------------------------------------------
// Purpose: The bars are made empty here and filled by SetLayout, from the saved layout when
//          there is one; then they go where they were last left.
//-----------------------------------------------------------------------------
CMapToolHost::CMapToolHost( std::vector<MapToolBarSpec_t> specs, QList<QAction *> actions, QWidget *pParent )
	: QMainWindow( pParent ),
	  m_Specs( std::move( specs ) ),
	  m_Actions( std::move( actions ) )
{
	setObjectName( QStringLiteral( "mapToolHost" ) );
	setWindowFlags( Qt::Widget );
	setDockNestingEnabled( false );
	for ( const MapToolBarSpec_t &spec : m_Specs )
	{
		auto *pBar = new QToolBar( spec.m_szTitle, this );
		pBar->setObjectName( spec.m_szName );
		pBar->setIconSize( QSize{ 24, 24 } );
		// Some styles pin bars by default; these are meant to be moved.
		pBar->setMovable( true );
		pBar->setFloatable( true );
		addToolBar( spec.m_nArea, pBar );
		m_Bars.push_back( pBar );
	}
	const MapToolBarLayout_t saved = SavedLayout();
	SetLayout( saved.empty() ? DefaultLayout() : saved, false );
	SetButtonStyle( static_cast<Qt::ToolButtonStyle>( QSettings{}.value( QLatin1String( STYLE_SETTING ), static_cast<int>( Qt::ToolButtonIconOnly ) ).toInt() ), false );
	restoreState( QSettings{}.value( QLatin1String( STATE_SETTING ) ).toByteArray(), STATE_VERSION );
	m_StartState = saveState( STATE_VERSION );
	Hosts().append( this );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapToolHost::~CMapToolHost()
{
	// Only a tab whose bars were moved has news: another closing later leaves that alone.
	if ( saveState( STATE_VERSION ) != m_StartState )
	{
		SaveState();
	}
	Hosts().removeAll( this );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QList<CMapToolHost *> &CMapToolHost::Hosts()
{
	static QList<CMapToolHost *> s_Hosts;
	return s_Hosts;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapToolHost::SaveState() const
{
	QSettings{}.setValue( QLatin1String( STATE_SETTING ), saveState( STATE_VERSION ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QToolBar *CMapToolHost::GetBar( const QString &szName ) const
{
	for ( QToolBar *pBar : m_Bars )
	{
		if ( pBar->objectName() == szName )
			return pBar;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QAction *CMapToolHost::FindAction( const QString &szName ) const
{
	for ( QAction *pAction : m_Actions )
	{
		if ( pAction->objectName() == szName )
			return pAction;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapToolBarLayout_t CMapToolHost::DefaultLayout() const
{
	MapToolBarLayout_t layout;
	for ( const MapToolBarSpec_t &spec : m_Specs )
	{
		layout.emplace_back( spec.m_szName, spec.m_Actions );
	}
	return layout;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapToolBarLayout_t CMapToolHost::SavedLayout()
{
	MapToolBarLayout_t layout;
	for ( const QString &szEntry : QSettings{}.value( QLatin1String( LAYOUT_SETTING ) ).toStringList() )
	{
		if ( szEntry.startsWith( QLatin1Char( '[' ) ) && szEntry.endsWith( QLatin1Char( ']' ) ) )
		{
			layout.emplace_back( szEntry.mid( 1, szEntry.size() - 2 ), QStringList{} );
		}
		else if ( !layout.empty() )
		{
			layout.back().second.append( szEntry );
		}
	}
	return layout;
}

//-----------------------------------------------------------------------------
// Purpose: Hidden buttons are kept in the layout (so Customize can show them again) but not on
//          the bar; the bar's own widgets (its separators) are made again each time.
//-----------------------------------------------------------------------------
MapToolBarLayout_t CMapToolHost::GetLayout() const
{
	return m_Layout;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapToolHost::SetLayout( const MapToolBarLayout_t &layout, bool bSave )
{
	MapToolBarLayout_t placed;
	QSet<QString> used;
	for ( QToolBar *pBar : m_Bars )
	{
		pBar->clear();
		QStringList entries;
		for ( const auto &[ szBar, list ] : layout )
		{
			if ( szBar == pBar->objectName() )
				entries = list;
		}
		// Commands no saved bar holds (new since the layout was saved) join the bar they come in.
		for ( const MapToolBarSpec_t &spec : m_Specs )
		{
			if ( spec.m_szName != pBar->objectName() )
				continue;
			for ( const QString &szName : spec.m_Actions )
			{
				if ( szName == QLatin1String( GAP ) )
					continue;
				const bool bSomewhere = std::ranges::any_of( layout, [ & ]( const auto &bar ) { return bar.second.contains( szName ) || bar.second.contains( QLatin1Char( '!' ) + szName ); } );
				if ( !bSomewhere )
					entries.append( szName );
			}
		}
		QStringList kept;
		bool bGapWanted = false;
		for ( const QString &szEntry : entries )
		{
			if ( szEntry == QLatin1String( GAP ) )
			{
				kept.append( szEntry );
				bGapWanted = !pBar->actions().isEmpty();
				continue;
			}
			const bool bHidden = szEntry.startsWith( QLatin1Char( '!' ) );
			const QString szName = bHidden ? szEntry.mid( 1 ) : szEntry;
			QAction *pAction = FindAction( szName );
			if ( !pAction || used.contains( szName ) )
				continue;
			used.insert( szName );
			kept.append( szEntry );
			if ( bHidden )
				continue;
			if ( bGapWanted )
				pBar->addSeparator();
			bGapWanted = false;
			pBar->addAction( pAction );
		}
		placed.emplace_back( pBar->objectName(), kept );
	}
	m_Layout = placed;
	if ( bSave )
	{
		QStringList entries;
		for ( const auto &[ szBar, list ] : placed )
		{
			entries.append( QLatin1Char( '[' ) + szBar + QLatin1Char( ']' ) );
			entries.append( list );
		}
		QSettings{}.setValue( QLatin1String( LAYOUT_SETTING ), entries );
		for ( CMapToolHost *pHost : Hosts() )
		{
			if ( pHost != this )
				pHost->SetLayout( placed, false );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapToolHost::SetButtonStyle( Qt::ToolButtonStyle nStyle, bool bSave )
{
	setToolButtonStyle( nStyle );
	for ( QToolBar *pBar : m_Bars )
		pBar->setToolButtonStyle( nStyle );
	if ( bSave )
	{
		QSettings{}.setValue( QLatin1String( STYLE_SETTING ), static_cast<int>( nStyle ) );
		for ( CMapToolHost *pHost : Hosts() )
		{
			if ( pHost != this )
				pHost->SetButtonStyle( nStyle, false );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapToolHost::ResetLayout()
{
	QSettings{}.remove( QLatin1String( LAYOUT_SETTING ) );
	QSettings{}.remove( QLatin1String( STATE_SETTING ) );
	SetLayout( DefaultLayout(), false );
	for ( CMapToolHost *pHost : Hosts() )
	{
		if ( pHost != this )
			pHost->SetLayout( DefaultLayout(), false );
		for ( std::size_t i = 0; i < pHost->m_Bars.size(); i++ )
		{
			pHost->removeToolBar( pHost->m_Bars[ i ] );
			pHost->addToolBar( pHost->m_Specs[ i ].m_nArea, pHost->m_Bars[ i ] );
			pHost->m_Bars[ i ]->show();
		}
		pHost->m_StartState = pHost->saveState( STATE_VERSION );
	}
	SetButtonStyle( Qt::ToolButtonIconOnly, true );
}

//-----------------------------------------------------------------------------
// Purpose: Qt's menu of the bars, then how buttons look, Customize and Reset.
//-----------------------------------------------------------------------------
QMenu *CMapToolHost::createPopupMenu()
{
	auto *pMenu = new QMenu( this );
	for ( QToolBar *pBar : m_Bars )
	{
		QAction *pToggle = pBar->toggleViewAction();
		pMenu->addAction( pToggle );
	}
	pMenu->addSeparator();
	auto *pStyles = new QActionGroup( pMenu );
	for ( const auto &[ szName, nStyle ] : { std::pair{ tr( "Icons" ), Qt::ToolButtonIconOnly }, { tr( "Words" ), Qt::ToolButtonTextOnly }, { tr( "Icons and Words" ), Qt::ToolButtonTextBesideIcon } } )
	{
		QAction *pStyle = pMenu->addAction( szName );
		pStyle->setCheckable( true );
		pStyle->setChecked( toolButtonStyle() == nStyle );
		pStyles->addAction( pStyle );
		connect( pStyle, &QAction::triggered, this, [ this, nStyle ] { SetButtonStyle( nStyle, true ); } );
	}
	pMenu->addSeparator();
	connect( pMenu->addAction( tr( "Customize Tool Bars…" ) ), &QAction::triggered, this, &CMapToolHost::Customize );
	connect( pMenu->addAction( tr( "Reset Tool Bars" ) ), &QAction::triggered, this, &CMapToolHost::ResetLayout );
	return pMenu;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapToolHost::Customize()
{
	CMapToolBarDialog dialog( *this, this );
	if ( dialog.exec() == QDialog::Accepted )
	{
		SetLayout( dialog.GetLayout(), true );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapToolBarDialog::CMapToolBarDialog( const CMapToolHost &host, QWidget *pParent )
	: QDialog( pParent ),
	  m_Host( host )
{
	setWindowTitle( tr( "Customize Tool Bars" ) );
	setObjectName( QStringLiteral( "mapToolBarDialog" ) );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( new QLabel( tr( "Drag buttons into order or onto another bar; untick to hide one." ) ) );
	m_pTree = new QTreeWidget;
	m_pTree->setObjectName( QStringLiteral( "mapToolBarTree" ) );
	m_pTree->setHeaderHidden( true );
	m_pTree->setDragDropMode( QAbstractItemView::InternalMove );
	m_pTree->setDefaultDropAction( Qt::MoveAction );
	m_pTree->setSelectionMode( QAbstractItemView::SingleSelection );
	// Buttons drop onto bars, never beside them at the top level.
	m_pTree->invisibleRootItem()->setFlags( m_pTree->invisibleRootItem()->flags() & ~Qt::ItemIsDropEnabled );
	pLayout->addWidget( m_pTree, 1 );
	auto *pButtons = new QHBoxLayout;
	auto *pGap = new QPushButton( tr( "Add Gap" ) );
	auto *pRemoveGap = new QPushButton( tr( "Remove Gap" ) );
	auto *pDefaults = new QPushButton( tr( "Defaults" ) );
	pButtons->addWidget( pGap );
	pButtons->addWidget( pRemoveGap );
	pButtons->addStretch();
	pButtons->addWidget( pDefaults );
	pLayout->addLayout( pButtons );
	auto *pBox = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	connect( pBox, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pBox, &QDialogButtonBox::rejected, this, &QDialog::reject );
	pLayout->addWidget( pBox );
	connect( pGap, &QPushButton::clicked, this, [ this ] {
		QTreeWidgetItem *pAt = m_pTree->currentItem();
		if ( !pAt )
			return;
		QTreeWidgetItem *pBar = pAt->parent() ? pAt->parent() : pAt;
		auto *pItem = new QTreeWidgetItem;
		pItem->setText( 0, tr( "— gap —" ) );
		pItem->setData( 0, Qt::UserRole, QLatin1String( GAP ) );
		pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled );
		pBar->insertChild( pAt->parent() ? pBar->indexOfChild( pAt ) + 1 : pBar->childCount(), pItem );
		m_pTree->setCurrentItem( pItem );
	} );
	connect( pRemoveGap, &QPushButton::clicked, this, [ this ] {
		QTreeWidgetItem *pAt = m_pTree->currentItem();
		if ( pAt && pAt->data( 0, Qt::UserRole ).toString() == QLatin1String( GAP ) )
			delete pAt;
	} );
	connect( pDefaults, &QPushButton::clicked, this, [ this ] {
		MapToolBarLayout_t layout;
		for ( const MapToolBarSpec_t &spec : m_Host.GetSpecs() )
			layout.emplace_back( spec.m_szName, spec.m_Actions );
		Fill( layout );
	} );
	Fill( host.GetLayout() );
	resize( 380, 560 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapToolBarDialog::Fill( const MapToolBarLayout_t &layout )
{
	m_pTree->clear();
	for ( const MapToolBarSpec_t &spec : m_Host.GetSpecs() )
	{
		auto *pBar = new QTreeWidgetItem( m_pTree );
		pBar->setText( 0, spec.m_szTitle );
		pBar->setData( 0, Qt::UserRole, spec.m_szName );
		pBar->setFlags( Qt::ItemIsEnabled | Qt::ItemIsDropEnabled );
		QFont font = pBar->font( 0 );
		font.setBold( true );
		pBar->setFont( 0, font );
		for ( const auto &[ szBar, entries ] : layout )
		{
			if ( szBar != spec.m_szName )
				continue;
			for ( const QString &szEntry : entries )
			{
				auto *pItem = new QTreeWidgetItem( pBar );
				pItem->setData( 0, Qt::UserRole, szEntry.startsWith( QLatin1Char( '!' ) ) ? szEntry.mid( 1 ) : szEntry );
				if ( szEntry == QLatin1String( GAP ) )
				{
					pItem->setText( 0, tr( "— gap —" ) );
					pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled );
					continue;
				}
				const QAction *pAction = m_Host.FindAction( pItem->data( 0, Qt::UserRole ).toString() );
				pItem->setText( 0, pAction ? pAction->text() : szEntry );
				if ( pAction )
					pItem->setIcon( 0, pAction->icon() );
				pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable );
				pItem->setCheckState( 0, szEntry.startsWith( QLatin1Char( '!' ) ) ? Qt::Unchecked : Qt::Checked );
			}
		}
	}
	m_pTree->expandAll();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapToolBarLayout_t CMapToolBarDialog::GetLayout() const
{
	MapToolBarLayout_t layout;
	for ( int i = 0; i < m_pTree->topLevelItemCount(); i++ )
	{
		const QTreeWidgetItem *pBar = m_pTree->topLevelItem( i );
		QStringList entries;
		for ( int j = 0; j < pBar->childCount(); j++ )
		{
			const QTreeWidgetItem *pItem = pBar->child( j );
			const QString szName = pItem->data( 0, Qt::UserRole ).toString();
			const bool bHidden = szName != QLatin1String( GAP ) && pItem->checkState( 0 ) == Qt::Unchecked;
			entries.append( bHidden ? QLatin1Char( '!' ) + szName : szName );
		}
		layout.emplace_back( pBar->data( 0, Qt::UserRole ).toString(), entries );
	}
	return layout;
}
