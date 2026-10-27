//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapView2D.h. Colours and grid rules are Hammer++'s (MapConstants.h).
//
//===========================================================================//
#include "MapView2D.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include "map/MapConstants.h"
#include "map/MapDocument.h"
#include "map/MapEditor.h"

namespace
{

constexpr std::uint32_t NO_OBJECT = ~0u;
constexpr int SCROLLBAR_SIZE = 14;
constexpr int DRAG_THRESHOLD = 3;

//-----------------------------------------------------------------------------
// Purpose: Whether two rectangles overlap, counting lines and points (zero width or height),
//          which QRectF::intersects leaves out.
//-----------------------------------------------------------------------------
bool Overlaps( const QRectF &a, const QRectF &b )
{
	return a.left() <= b.right() && b.left() <= a.right() && a.top() <= b.bottom() && b.top() <= a.bottom();
}

//-----------------------------------------------------------------------------
// Purpose: Distance in pixels from p to the segment a-b.
//-----------------------------------------------------------------------------
double DistanceToSegment( QPointF p, QPointF a, QPointF b )
{
	const QPointF ab = b - a;
	const double flLength = QPointF::dotProduct( ab, ab );
	const double t = flLength > 0.0 ? std::clamp( QPointF::dotProduct( p - a, ab ) / flLength, 0.0, 1.0 ) : 0.0;
	const QPointF d = p - ( a + ab * t );
	return std::sqrt( QPointF::dotProduct( d, d ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Each kind of line is left out once it would come closer than Hammer++'s smallest
//          spacing in pixels.
//-----------------------------------------------------------------------------
GridPlan_t PlanGrid( double flPixelsPerUnit, int nGridSize )
{
	auto shown = [ flPixelsPerUnit ]( double flSpacing ) {
		return flSpacing * flPixelsPerUnit >= mapstyle::HIDE_GRID_BELOW_PIXELS ? flSpacing : 0.0;
	};
	GridPlan_t plan;
	plan.m_flBase = shown( nGridSize );
	plan.m_flHighlight = shown( static_cast<double>( nGridSize ) * mapstyle::GRID_HIGHLIGHT_EVERY );
	// "Highlight every 64 units" only adds lines while the grid is finer than 64.
	plan.m_flSixtyFour = nGridSize < 64 ? shown( 64.0 ) : 0.0;
	plan.m_fl1024 = shown( 1024.0 );
	return plan;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapView2D::CMapView2D( CMapEditor &editor, MapViewType_t nType, QWidget *pParent )
	: CMapView( editor, pParent ),
	  m_nType( nType )
{
	setMouseTracking( true );
	setAttribute( Qt::WA_OpaquePaintEvent );
	m_pHorizontal = new QScrollBar( Qt::Horizontal, this );
	m_pVertical = new QScrollBar( Qt::Vertical, this );
	for ( QScrollBar *pBar : { m_pHorizontal, m_pVertical } )
	{
		pBar->setFocusPolicy( Qt::NoFocus );
		pBar->setRange( static_cast<int>( -mapstyle::WORLD_LIMIT ), static_cast<int>( mapstyle::WORLD_LIMIT ) );
		connect( pBar, &QScrollBar::valueChanged, this, [ this ] {
			if ( !m_bUpdatingScrollBars )
			{
				SetCenter( { static_cast<double>( m_pHorizontal->value() ), -static_cast<double>( m_pVertical->value() ) } );
			}
		} );
	}
	Rebuild();
	Fit();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMapView2D::GetAxis( int nIndex ) const
{
	switch ( m_nType )
	{
		case MapViewType_t::MAP_VIEW_TYPE_FRONT: return nIndex == 0 ? 1 : 2;
		case MapViewType_t::MAP_VIEW_TYPE_SIDE: return nIndex == 0 ? 0 : 2;
		default: return nIndex == 0 ? 0 : 1;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QPointF CMapView2D::Flatten( const xv::map::Point_t &p ) const
{
	return { p[ static_cast<std::size_t>( GetAxis( 0 ) ) ], p[ static_cast<std::size_t>( GetAxis( 1 ) ) ] };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::SetType( MapViewType_t nType )
{
	if ( !Is2DView( nType ) || nType == m_nType )
	{
		return;
	}
	m_nType = nType;
	m_pBuiltFor = nullptr;
	Rebuild();
	Fit();
}

//-----------------------------------------------------------------------------
// Purpose: Projects every solid's edges (displacements' grids included) and every point
//          entity's box onto the view's axes, once per scene. Edges that land on each other,
//          or on a point, are kept once.
//-----------------------------------------------------------------------------
void CMapView2D::Rebuild()
{
	const xv::map::CScene *pScene = GetDocument().GetScene();
	if ( pScene == m_pBuiltFor )
	{
		return;
	}
	m_pBuiltFor = pScene;
	m_Objects.clear();
	m_Segments.clear();
	m_ObjectIndex.clear();
	if ( !pScene )
	{
		update();
		return;
	}
	m_ObjectIndex.assign( pScene->GetObjectCount(), NO_OBJECT );
	const xv::map::CMapMesh &mesh = pScene->m_Mesh;
	const auto a0 = static_cast<std::size_t>( GetAxis( 0 ) );
	const auto a1 = static_cast<std::size_t>( GetAxis( 1 ) );
	std::vector<std::array<std::int64_t, 4>> keys;
	for ( std::uint32_t i = 0; i < pScene->m_Solids.size(); i++ )
	{
		const xv::map::SolidGeometry_t &geometry = pScene->m_Solids[ i ];
		if ( geometry.m_Faces.empty() )
		{
			continue;
		}
		MapFlatObject_t object;
		object.m_nObject = i;
		object.m_bInvalid = !geometry.IsValid();
		object.m_Bounds = QRectF{ QPointF{ geometry.m_Bounds.m_Mins[ a0 ], geometry.m_Bounds.m_Mins[ a1 ] }, QPointF{ geometry.m_Bounds.m_Maxs[ a0 ], geometry.m_Bounds.m_Maxs[ a1 ] } };
		object.m_nFirstSegment = static_cast<std::uint32_t>( m_Segments.size() );
		keys.clear();
		const xv::map::MeshRange_t range = i < mesh.m_SolidLines.size() ? mesh.m_SolidLines[ i ] : xv::map::MeshRange_t{};
		for ( std::uint32_t k = 0; k < range.m_nCount; k++ )
		{
			const xv::CVec3 &a = mesh.m_Vertices[ mesh.m_Lines[ ( range.m_nFirst + k ) * 2 ] ].m_vecPosition;
			const xv::CVec3 &b = mesh.m_Vertices[ mesh.m_Lines[ ( range.m_nFirst + k ) * 2 + 1 ] ].m_vecPosition;
			const std::array<float, 3> pa{ a.m_flX, a.m_flY, a.m_flZ };
			const std::array<float, 3> pb{ b.m_flX, b.m_flY, b.m_flZ };
			// Sixteenths of a unit are as close as two edges need to be to count as one.
			std::array<std::int64_t, 4> key{ std::llround( pa[ a0 ] * 16.0 ), std::llround( pa[ a1 ] * 16.0 ), std::llround( pb[ a0 ] * 16.0 ), std::llround( pb[ a1 ] * 16.0 ) };
			if ( key[ 0 ] == key[ 2 ] && key[ 1 ] == key[ 3 ] )
			{
				continue;
			}
			if ( std::pair{ key[ 2 ], key[ 3 ] } < std::pair{ key[ 0 ], key[ 1 ] } )
			{
				key = { key[ 2 ], key[ 3 ], key[ 0 ], key[ 1 ] };
			}
			keys.push_back( key );
		}
		std::ranges::sort( keys );
		keys.erase( std::unique( keys.begin(), keys.end() ), keys.end() );
		for ( const auto &key : keys )
		{
			m_Segments.emplace_back( static_cast<double>( key[ 0 ] ) / 16.0, static_cast<double>( key[ 1 ] ) / 16.0, static_cast<double>( key[ 2 ] ) / 16.0, static_cast<double>( key[ 3 ] ) / 16.0 );
		}
		object.m_nSegmentCount = static_cast<std::uint32_t>( m_Segments.size() ) - object.m_nFirstSegment;
		m_ObjectIndex[ i ] = static_cast<std::uint32_t>( m_Objects.size() );
		m_Objects.push_back( object );
	}
	for ( std::size_t e = 0; e < pScene->m_Entities.size(); e++ )
	{
		if ( !pScene->m_Entities[ e ].m_bPoint )
		{
			continue;
		}
		const xv::map::Box_t box = pScene->GetEntityBox( e );
		MapFlatObject_t object;
		object.m_nObject = pScene->EntityObject( e );
		object.m_bPointEntity = true;
		object.m_Bounds = QRectF{ QPointF{ box.m_Mins[ a0 ], box.m_Mins[ a1 ] }, QPointF{ box.m_Maxs[ a0 ], box.m_Maxs[ a1 ] } };
		m_ObjectIndex[ object.m_nObject ] = static_cast<std::uint32_t>( m_Objects.size() );
		m_Objects.push_back( object );
	}
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::Fit()
{
	const xv::map::CScene *pScene = GetDocument().GetScene();
	if ( !pScene || pScene->m_Bounds.IsEmpty() || width() < 16 || height() < 16 )
	{
		SetCenter( {} );
		return;
	}
	const QPointF mins = Flatten( pScene->m_Bounds.m_Mins );
	const QPointF maxs = Flatten( pScene->m_Bounds.m_Maxs );
	const double flWidth = std::max( maxs.x() - mins.x(), 64.0 );
	const double flHeight = std::max( maxs.y() - mins.y(), 64.0 );
	m_flZoom = std::clamp( std::min( ( width() - SCROLLBAR_SIZE ) / flWidth, ( height() - SCROLLBAR_SIZE ) / flHeight ) * 0.9, mapstyle::ZOOM_SMALLEST, mapstyle::ZOOM_LARGEST );
	SetCenter( ( mins + maxs ) / 2.0 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::SetZoom( double flZoom )
{
	m_flZoom = std::clamp( flZoom, mapstyle::ZOOM_SMALLEST, mapstyle::ZOOM_LARGEST );
	UpdateScrollBars();
	m_Editor.ShowZoom( m_flZoom );
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Zooms by flFactor, keeping the map point under at (a widget point) where it is.
//-----------------------------------------------------------------------------
void CMapView2D::Zoom( double flFactor, std::optional<QPointF> at )
{
	const QPointF pivot = at.value_or( QPointF{ width() / 2.0, height() / 2.0 } );
	const QPointF before = ToMap( pivot );
	m_flZoom = std::clamp( m_flZoom * flFactor, mapstyle::ZOOM_SMALLEST, mapstyle::ZOOM_LARGEST );
	const QPointF after = ToMap( pivot );
	SetCenter( m_Center + ( before - after ) );
	m_Editor.ShowZoom( m_flZoom );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::SetCenter( QPointF center )
{
	m_Center = QPointF{ std::clamp( center.x(), -mapstyle::WORLD_LIMIT, mapstyle::WORLD_LIMIT ), std::clamp( center.y(), -mapstyle::WORLD_LIMIT, mapstyle::WORLD_LIMIT ) };
	UpdateScrollBars();
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::CenterOn( const xv::map::Box_t &box )
{
	if ( !box.IsEmpty() )
	{
		SetCenter( Flatten( box.GetCenter() ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QPointF CMapView2D::ToMap( QPointF at ) const
{
	return { m_Center.x() + ( at.x() - width() / 2.0 ) / m_flZoom, m_Center.y() - ( at.y() - height() / 2.0 ) / m_flZoom };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QPointF CMapView2D::ToWidget( QPointF map ) const
{
	return { ( map.x() - m_Center.x() ) * m_flZoom + width() / 2.0, height() / 2.0 - ( map.y() - m_Center.y() ) * m_flZoom };
}

//-----------------------------------------------------------------------------
// Purpose: The map units the widget shows.
//-----------------------------------------------------------------------------
QRectF CMapView2D::GetVisible() const
{
	return QRectF{ ToMap( QPointF{ 0, static_cast<double>( height() ) } ), ToMap( QPointF{ static_cast<double>( width() ), 0 } ) }.normalized();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::UpdateScrollBars()
{
	m_bUpdatingScrollBars = true;
	const int nPage = static_cast<int>( std::max( 1.0, width() / m_flZoom ) );
	m_pHorizontal->setPageStep( nPage );
	m_pVertical->setPageStep( static_cast<int>( std::max( 1.0, height() / m_flZoom ) ) );
	m_pHorizontal->setSingleStep( std::max( 1, static_cast<int>( mapstyle::SCROLL_STEP_PIXELS / m_flZoom ) ) );
	m_pVertical->setSingleStep( std::max( 1, static_cast<int>( mapstyle::SCROLL_STEP_PIXELS / m_flZoom ) ) );
	m_pHorizontal->setValue( static_cast<int>( std::lround( m_Center.x() ) ) );
	m_pVertical->setValue( static_cast<int>( -std::lround( m_Center.y() ) ) );
	m_bUpdatingScrollBars = false;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's colours: the selection, then a visgroup's colour, then the entity's, then
//          white for the world.
//-----------------------------------------------------------------------------
QColor CMapView2D::ColorOf( const MapFlatObject_t &object ) const
{
	const CMapDocument &document = GetDocument();
	if ( document.IsSelected( object.m_nObject ) )
	{
		return mapstyle::SELECTION;
	}
	const xv::map::CScene &scene = *document.GetScene();
	if ( object.m_bInvalid )
	{
		return mapstyle::INVALID;
	}
	const int nEntity = scene.GetObjectEntity( object.m_nObject );
	const xv::map::EditorInfo_t &editor = nEntity >= 0 ? scene.m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ].m_Editor : scene.GetObjectEditor( object.m_nObject );
	if ( m_Editor.GetSettings().m_bUseGroupColors && !editor.m_Visgroups.empty() )
	{
		for ( const xv::map::Visgroup_t &visgroup : scene.m_Map.m_Visgroups )
		{
			if ( visgroup.m_nId == editor.m_Visgroups.front() )
			{
				return QColor{ visgroup.m_Color.m_nR, visgroup.m_Color.m_nG, visgroup.m_Color.m_nB };
			}
		}
	}
	if ( nEntity >= 0 )
	{
		const xv::map::Color_t color = scene.m_Entities[ static_cast<std::size_t>( nEntity ) ].m_Color;
		return QColor{ color.m_nR, color.m_nG, color.m_nB };
	}
	return mapstyle::BRUSH;
}

//-----------------------------------------------------------------------------
// Purpose: Lines every so many units, from finest to coarsest, then the axes.
//-----------------------------------------------------------------------------
void CMapView2D::PaintGrid( QPainter &painter, const QRectF &visible ) const
{
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	auto lines = [ & ]( double flSpacing, const QColor &color ) {
		if ( flSpacing <= 0.0 )
		{
			return;
		}
		std::vector<QLineF> found;
		for ( double x = std::floor( visible.left() / flSpacing ) * flSpacing; x <= visible.right(); x += flSpacing )
		{
			const double flPixel = std::round( ToWidget( { x, 0 } ).x() ) + 0.5;
			found.emplace_back( flPixel, 0.0, flPixel, static_cast<double>( height() ) );
		}
		for ( double y = std::floor( visible.top() / flSpacing ) * flSpacing; y <= visible.bottom(); y += flSpacing )
		{
			const double flPixel = std::round( ToWidget( { 0, y } ).y() ) + 0.5;
			found.emplace_back( 0.0, flPixel, static_cast<double>( width() ), flPixel );
		}
		painter.setPen( QPen{ color, 0 } );
		painter.drawLines( found.data(), static_cast<int>( found.size() ) );
	};
	if ( settings.m_bShowGrid )
	{
		const GridPlan_t plan = PlanGrid( m_flZoom, settings.m_nGridSize );
		lines( plan.m_flBase, mapstyle::GRID );
		lines( plan.m_flSixtyFour, mapstyle::GRID_10 );
		lines( plan.m_flHighlight, mapstyle::GRID_10 );
		lines( plan.m_fl1024, mapstyle::GRID_1024 );
	}
	// The axes show with or without the grid.
	painter.setPen( QPen{ mapstyle::AXIS, 0 } );
	const QPointF origin = ToWidget( { 0, 0 } );
	const double flX = std::round( origin.x() ) + 0.5;
	const double flY = std::round( origin.y() ) + 0.5;
	painter.drawLine( QLineF{ flX, 0.0, flX, static_cast<double>( height() ) } );
	painter.drawLine( QLineF{ 0.0, flY, static_cast<double>( width() ), flY } );
}

//-----------------------------------------------------------------------------
// Purpose: Every shown object in view, in batches of one colour; selected ones last, on top.
//-----------------------------------------------------------------------------
void CMapView2D::PaintObjects( QPainter &painter, const QRectF &visible ) const
{
	const CMapDocument &document = GetDocument();
	const xv::map::CScene *pScene = document.GetScene();
	if ( !pScene )
	{
		return;
	}
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	struct Batch_t
	{
		QColor m_Color;
		std::vector<QLineF> m_Lines;
		std::vector<QRectF> m_Boxes;
		std::vector<QPointF> m_Points;
	};
	std::vector<Batch_t> batches;
	auto batchFor = [ & ]( const QColor &color ) -> Batch_t & {
		for ( Batch_t &batch : batches )
		{
			if ( batch.m_Color == color )
			{
				return batch;
			}
		}
		batches.push_back( { color, {}, {}, {} } );
		return batches.back();
	};
	std::vector<QPointF> vertices;
	std::vector<const MapFlatObject_t *> selected;
	const double flHalfHandle = mapstyle::HANDLE_SIZE / 2.0;
	auto add = [ & ]( const MapFlatObject_t &object, Batch_t &batch ) {
		const QRectF bounds{ ToWidget( object.m_Bounds.topLeft() ), ToWidget( object.m_Bounds.bottomRight() ) };
		const QRectF box = bounds.normalized();
		if ( box.width() < 1.0 && box.height() < 1.0 )
		{
			// Smaller than a pixel: a dot is all there is to see.
			batch.m_Points.push_back( box.center() );
			return;
		}
		if ( object.m_bPointEntity )
		{
			batch.m_Boxes.push_back( box.adjusted( 0.5, 0.5, -0.5, -0.5 ) );
		}
		for ( std::uint32_t k = 0; k < object.m_nSegmentCount; k++ )
		{
			const QLineF &segment = m_Segments[ object.m_nFirstSegment + k ];
			const QLineF line{ ToWidget( segment.p1() ), ToWidget( segment.p2() ) };
			batch.m_Lines.push_back( line );
			if ( settings.m_bDrawVertices && box.width() > 8.0 && box.height() > 8.0 )
			{
				vertices.push_back( line.p1() );
				vertices.push_back( line.p2() );
			}
		}
		// The centre handle Hammer draws on every object big enough to have room for it.
		if ( settings.m_bCenterHandles && box.width() > 3 * mapstyle::HANDLE_SIZE && box.height() > 3 * mapstyle::HANDLE_SIZE )
		{
			const QPointF c = box.center();
			batch.m_Lines.emplace_back( c.x() - flHalfHandle, c.y() - flHalfHandle, c.x() + flHalfHandle, c.y() + flHalfHandle );
			batch.m_Lines.emplace_back( c.x() - flHalfHandle, c.y() + flHalfHandle, c.x() + flHalfHandle, c.y() - flHalfHandle );
		}
	};
	for ( const MapFlatObject_t &object : m_Objects )
	{
		if ( !Overlaps( object.m_Bounds, visible ) || !document.IsShown( object.m_nObject ) )
		{
			continue;
		}
		if ( document.IsSelected( object.m_nObject ) )
		{
			selected.push_back( &object );
			continue;
		}
		add( object, batchFor( ColorOf( object ) ) );
	}
	auto draw = [ & ]( const Batch_t &batch ) {
		painter.setPen( QPen{ batch.m_Color, 0 } );
		painter.setBrush( Qt::NoBrush );
		painter.drawLines( batch.m_Lines.data(), static_cast<int>( batch.m_Lines.size() ) );
		painter.drawRects( batch.m_Boxes.data(), static_cast<int>( batch.m_Boxes.size() ) );
		painter.drawPoints( batch.m_Points.data(), static_cast<int>( batch.m_Points.size() ) );
	};
	for ( const Batch_t &batch : batches )
	{
		draw( batch );
	}
	if ( !vertices.empty() )
	{
		painter.setPen( QPen{ mapstyle::VERTEX, static_cast<double>( mapstyle::VERTEX_SIZE ), Qt::SolidLine, Qt::SquareCap } );
		painter.drawPoints( vertices.data(), static_cast<int>( vertices.size() ) );
	}
	vertices.clear();
	Batch_t chosen{ mapstyle::SELECTION, {}, {}, {} };
	for ( const MapFlatObject_t *pObject : selected )
	{
		add( *pObject, chosen );
	}
	draw( chosen );
	if ( !vertices.empty() )
	{
		painter.setPen( QPen{ mapstyle::VERTEX, static_cast<double>( mapstyle::VERTEX_SIZE ), Qt::SolidLine, Qt::SquareCap } );
		painter.drawPoints( vertices.data(), static_cast<int>( vertices.size() ) );
	}

	// Entity names, where there's room for them.
	if ( settings.m_bNames && m_flZoom * 16.0 >= mapstyle::NAMES_FROM_PIXELS )
	{
		QFont font = painter.font();
		font.setPointSizeF( std::max( 6.0, font.pointSizeF() * 0.8 ) );
		painter.setFont( font );
		for ( const MapFlatObject_t &object : m_Objects )
		{
			if ( !object.m_bPointEntity || !Overlaps( object.m_Bounds, visible ) || !document.IsShown( object.m_nObject ) )
			{
				continue;
			}
			const std::size_t nEntity = object.m_nObject - pScene->m_Solids.size();
			const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ nEntity ];
			const std::string_view szName = entity.GetValue( "targetname" ).empty() ? entity.GetClassname() : entity.GetValue( "targetname" );
			const QRectF box = QRectF{ ToWidget( object.m_Bounds.topLeft() ), ToWidget( object.m_Bounds.bottomRight() ) }.normalized();
			painter.setPen( ColorOf( object ) );
			painter.drawText( QPointF{ box.left(), box.top() - 3.0 }, QString::fromUtf8( szName.data(), static_cast<qsizetype>( szName.size() ) ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The selection's box with Hammer's handles and its size, and a box selection
//          being dragged or waiting for Enter.
//-----------------------------------------------------------------------------
void CMapView2D::PaintSelection( QPainter &painter ) const
{
	const CMapDocument &document = GetDocument();
	auto dotted = [ & ]( const QRectF &box ) {
		QPen pen{ mapstyle::TOOL_SELECTION, 0, Qt::DotLine };
		painter.setPen( pen );
		painter.setBrush( Qt::NoBrush );
		painter.drawRect( box );
	};
	// Hammer's eight handles, just outside the box's corners and sides.
	auto handles = [ & ]( const QRectF &box ) {
		painter.setPen( QPen{ Qt::black, 0 } );
		painter.setBrush( mapstyle::TOOL_HANDLE );
		const double h = mapstyle::HANDLE_SIZE;
		const double flOut = h / 2.0 + 2.0;
		for ( int ix = -1; ix <= 1; ix++ )
		{
			for ( int iy = -1; iy <= 1; iy++ )
			{
				if ( ix == 0 && iy == 0 )
				{
					continue;
				}
				const double x = ix < 0 ? box.left() - flOut : ix > 0 ? box.right() + flOut
																	  : box.center().x();
				const double y = iy < 0 ? box.top() - flOut : iy > 0 ? box.bottom() + flOut
																	 : box.center().y();
				painter.drawRect( QRectF{ x - h / 2.0, y - h / 2.0, h, h } );
			}
		}
	};
	if ( !document.GetSelection().empty() )
	{
		const xv::map::Box_t bounds = document.GetSelectionBounds();
		if ( !bounds.IsEmpty() )
		{
			const QPointF mins = Flatten( bounds.m_Mins );
			const QPointF maxs = Flatten( bounds.m_Maxs );
			const QRectF box = QRectF{ ToWidget( mins ), ToWidget( maxs ) }.normalized();
			painter.fillRect( box, mapstyle::FILL_SELECTION );
			dotted( box );
			handles( box );
			// Its size on two sides, as Hammer shows it while selecting.
			painter.setPen( mapstyle::TOOL_SELECTION );
			const QString szWidth = QString::number( std::lround( maxs.x() - mins.x() ) );
			const QString szHeight = QString::number( std::lround( maxs.y() - mins.y() ) );
			const QFontMetrics metrics = painter.fontMetrics();
			painter.drawText( QPointF{ box.center().x() - metrics.horizontalAdvance( szWidth ) / 2.0, box.top() - 10.0 }, szWidth );
			painter.drawText( QPointF{ box.left() - metrics.horizontalAdvance( szHeight ) - 10.0, box.center().y() + metrics.ascent() / 2.0 }, szHeight );
		}
	}
	if ( m_BoxFrom && m_PressAt != QPoint{} )
	{
		const QPointF now = mapFromGlobal( QCursor::pos() );
		dotted( QRectF{ ToWidget( *m_BoxFrom ), now }.normalized() );
	}
	else if ( m_PendingBox )
	{
		const QRectF box = QRectF{ ToWidget( m_PendingBox->topLeft() ), ToWidget( m_PendingBox->bottomRight() ) }.normalized();
		dotted( box );
		handles( box );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Where the 3D view's camera is and which way it looks, seen from this axis.
//-----------------------------------------------------------------------------
void CMapView2D::PaintCamera( QPainter &painter ) const
{
	const std::optional<xv::map::CViewCamera> camera = m_Editor.GetCamera();
	if ( !camera )
	{
		return;
	}
	const xv::map::Point_t forward = camera->GetForward();
	const double flLength = mapstyle::CAMERA_LOOK_PIXELS / std::max( m_flZoom, 1e-9 );
	const QPointF eye = ToWidget( Flatten( camera->m_Eye ) );
	const QPointF look = ToWidget( Flatten( xv::map::Add( camera->m_Eye, xv::map::Scale( forward, flLength ) ) ) );
	painter.setRenderHint( QPainter::Antialiasing, true );
	painter.setPen( QPen{ mapstyle::CAMERA, 0 } );
	painter.setBrush( Qt::NoBrush );
	painter.drawEllipse( eye, mapstyle::CAMERA_EYE_PIXELS, mapstyle::CAMERA_EYE_PIXELS );
	painter.drawLine( eye, look );
	painter.setRenderHint( QPainter::Antialiasing, false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::paintEvent( QPaintEvent * )
{
	QElapsedTimer timer;
	timer.start();
	Rebuild();
	QPainter painter( this );
	painter.fillRect( rect(), mapstyle::BACKGROUND );
	const QRectF visible = GetVisible();
	PaintGrid( painter, visible );
	PaintObjects( painter, visible );
	PaintCamera( painter );
	PaintSelection( painter );
	if ( hasFocus() )
	{
		// The view keys go to: Hammer marks it with its caption, a thin frame here.
		painter.setPen( QPen{ palette().highlight().color(), 0 } );
		painter.setBrush( Qt::NoBrush );
		painter.drawRect( rect().adjusted( 0, 0, -1 - SCROLLBAR_SIZE, -1 - SCROLLBAR_SIZE ) );
	}
	m_flPaintMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::resizeEvent( QResizeEvent *pEvent )
{
	CMapView::resizeEvent( pEvent );
	m_pHorizontal->setGeometry( 0, height() - SCROLLBAR_SIZE, width() - SCROLLBAR_SIZE, SCROLLBAR_SIZE );
	m_pVertical->setGeometry( width() - SCROLLBAR_SIZE, 0, SCROLLBAR_SIZE, height() - SCROLLBAR_SIZE );
	UpdateScrollBars();
}

//-----------------------------------------------------------------------------
// Purpose: The object whose edge or centre handle is nearest the point, within a handle and a
//          bit; point entities also take clicks inside their box. Smaller objects win ties,
//          so a brush inside a big one can still be picked.
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> CMapView2D::HitAt( QPointF at ) const
{
	const CMapDocument &document = GetDocument();
	if ( !document.GetScene() )
	{
		return std::nullopt;
	}
	const double flReach = mapstyle::HANDLE_SIZE / 2.0 + 2.0;
	const QRectF near = QRectF{ ToMap( at - QPointF{ flReach, -flReach } ), ToMap( at + QPointF{ flReach, -flReach } ) }.normalized();
	std::optional<std::uint32_t> best;
	double flBestArea = 0.0;
	for ( const MapFlatObject_t &object : m_Objects )
	{
		if ( !Overlaps( object.m_Bounds.adjusted( -near.width(), -near.height(), near.width(), near.height() ), near ) || !document.IsShown( object.m_nObject ) )
		{
			continue;
		}
		const QRectF box = QRectF{ ToWidget( object.m_Bounds.topLeft() ), ToWidget( object.m_Bounds.bottomRight() ) }.normalized();
		bool bHit = std::abs( at.x() - box.center().x() ) <= flReach && std::abs( at.y() - box.center().y() ) <= flReach;
		if ( object.m_bPointEntity && box.adjusted( -flReach, -flReach, flReach, flReach ).contains( at ) )
		{
			bHit = true;
		}
		for ( std::uint32_t k = 0; k < object.m_nSegmentCount && !bHit; k++ )
		{
			const QLineF &segment = m_Segments[ object.m_nFirstSegment + k ];
			bHit = DistanceToSegment( at, ToWidget( segment.p1() ), ToWidget( segment.p2() ) ) <= flReach;
		}
		if ( !bHit )
		{
			continue;
		}
		const double flArea = object.m_Bounds.width() * object.m_Bounds.height();
		if ( !best || flArea < flBestArea )
		{
			best = object.m_nObject;
			flBestArea = flArea;
		}
	}
	return best;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> CMapView2D::ObjectsIn( const QRectF &box ) const
{
	std::vector<std::uint32_t> found;
	const CMapDocument &document = GetDocument();
	for ( const MapFlatObject_t &object : m_Objects )
	{
		if ( Overlaps( object.m_Bounds, box ) && document.IsShown( object.m_nObject ) )
		{
			found.push_back( object.m_nObject );
		}
	}
	return found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::mousePressEvent( QMouseEvent *pEvent )
{
	setFocus( Qt::MouseFocusReason );
	const QPointF at = pEvent->position();
	if ( pEvent->button() == Qt::MiddleButton || ( pEvent->button() == Qt::LeftButton && m_bSpaceHeld ) )
	{
		m_PanFrom = pEvent->position().toPoint();
		setCursor( Qt::ClosedHandCursor );
		return;
	}
	const MapTool_t nTool = m_Editor.GetTool();
	if ( nTool == MapTool_t::MAP_TOOL_MAGNIFY )
	{
		if ( pEvent->button() == Qt::LeftButton || pEvent->button() == Qt::RightButton )
		{
			Zoom( pEvent->button() == Qt::LeftButton ? 2.0 : 0.5, at );
		}
		return;
	}
	if ( pEvent->button() == Qt::LeftButton )
	{
		m_PressAt = pEvent->position().toPoint();
		m_BoxFrom = ToMap( at );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::mouseMoveEvent( QMouseEvent *pEvent )
{
	ReportCursor( pEvent->position() );
	if ( m_PanFrom )
	{
		const QPoint delta = pEvent->position().toPoint() - *m_PanFrom;
		m_PanFrom = pEvent->position().toPoint();
		SetCenter( m_Center + QPointF{ -delta.x() / m_flZoom, delta.y() / m_flZoom } );
		return;
	}
	if ( m_BoxFrom )
	{
		update();
	}
}

//-----------------------------------------------------------------------------
// Purpose: A click picks (Ctrl toggles); a drag leaves a box to select with Enter.
//-----------------------------------------------------------------------------
void CMapView2D::mouseReleaseEvent( QMouseEvent *pEvent )
{
	if ( m_PanFrom )
	{
		m_PanFrom.reset();
		setCursor( m_bSpaceHeld ? Qt::OpenHandCursor : Qt::ArrowCursor );
		return;
	}
	if ( pEvent->button() != Qt::LeftButton || !m_BoxFrom )
	{
		return;
	}
	CMapDocument &document = GetDocument();
	const QPoint moved = pEvent->position().toPoint() - m_PressAt;
	const bool bControl = pEvent->modifiers().testFlag( Qt::ControlModifier );
	if ( std::abs( moved.x() ) < DRAG_THRESHOLD && std::abs( moved.y() ) < DRAG_THRESHOLD )
	{
		m_PendingBox.reset();
		if ( const auto hit = HitAt( pEvent->position() ) )
		{
			const std::vector<std::uint32_t> objects = document.Expand( *hit );
			if ( bControl )
			{
				document.ToggleSelection( objects );
			}
			else
			{
				document.SetSelection( objects );
			}
		}
		else if ( !bControl )
		{
			document.ClearSelection();
		}
	}
	else
	{
		m_PendingBox = QRectF{ *m_BoxFrom, ToMap( pEvent->position() ) }.normalized();
	}
	m_BoxFrom.reset();
	m_PressAt = {};
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::wheelEvent( QWheelEvent *pEvent )
{
	const double flNotches = pEvent->angleDelta().y() / 120.0;
	if ( flNotches != 0.0 )
	{
		Zoom( std::pow( mapstyle::WHEEL_ZOOM_STEP, flNotches ), pEvent->position() );
	}
	pEvent->accept();
}

//-----------------------------------------------------------------------------
// Purpose: Space is held to pan; everything else goes through the key map.
//-----------------------------------------------------------------------------
void CMapView2D::keyPressEvent( QKeyEvent *pEvent )
{
	if ( pEvent->key() == Qt::Key_Space && pEvent->modifiers() == Qt::NoModifier )
	{
		if ( !pEvent->isAutoRepeat() )
		{
			m_bSpaceHeld = true;
			setCursor( Qt::OpenHandCursor );
		}
		return;
	}
	CMapView::keyPressEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::keyReleaseEvent( QKeyEvent *pEvent )
{
	if ( pEvent->key() == Qt::Key_Space && !pEvent->isAutoRepeat() )
	{
		m_bSpaceHeld = false;
		if ( !m_PanFrom )
		{
			setCursor( Qt::ArrowCursor );
		}
		return;
	}
	CMapView::keyReleaseEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::leaveEvent( QEvent *pEvent )
{
	m_Editor.ShowCursor( {} );
	CMapView::leaveEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: The status bar's "@ x, y" in this view's axes.
//-----------------------------------------------------------------------------
void CMapView2D::ReportCursor( QPointF at )
{
	const QPointF map = ToMap( at );
	m_Editor.ShowCursor( QStringLiteral( "@%1, %2" ).arg( std::lround( map.x() ) ).arg( std::lround( map.y() ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapView2D::Run( MapAction_t nAction )
{
	const double flStep = mapstyle::SCROLL_STEP_PIXELS / m_flZoom;
	switch ( nAction )
	{
		case MapAction_t::MAP_ACTION_NEXT_AXIS:
			SetType( m_nType == MapViewType_t::MAP_VIEW_TYPE_TOP ? MapViewType_t::MAP_VIEW_TYPE_FRONT : m_nType == MapViewType_t::MAP_VIEW_TYPE_FRONT ? MapViewType_t::MAP_VIEW_TYPE_SIDE
																																					  : MapViewType_t::MAP_VIEW_TYPE_TOP );
			m_Editor.ViewTypeChanged( this );
			return true;
		case MapAction_t::MAP_ACTION_ZOOM_IN: Zoom( mapstyle::KEY_ZOOM_STEP ); return true;
		case MapAction_t::MAP_ACTION_ZOOM_OUT: Zoom( 1.0 / mapstyle::KEY_ZOOM_STEP ); return true;
		case MapAction_t::MAP_ACTION_SCROLL_UP: SetCenter( m_Center + QPointF{ 0, flStep } ); return true;
		case MapAction_t::MAP_ACTION_SCROLL_DOWN: SetCenter( m_Center - QPointF{ 0, flStep } ); return true;
		case MapAction_t::MAP_ACTION_SCROLL_LEFT: SetCenter( m_Center - QPointF{ flStep, 0 } ); return true;
		case MapAction_t::MAP_ACTION_SCROLL_RIGHT: SetCenter( m_Center + QPointF{ flStep, 0 } ); return true;
		case MapAction_t::MAP_ACTION_ENTER:
			if ( m_PendingBox )
			{
				std::vector<std::uint32_t> objects;
				for ( const std::uint32_t n : ObjectsIn( *m_PendingBox ) )
				{
					const std::vector<std::uint32_t> expanded = GetDocument().Expand( n );
					objects.insert( objects.end(), expanded.begin(), expanded.end() );
				}
				if ( QApplication::keyboardModifiers().testFlag( Qt::ControlModifier ) )
				{
					GetDocument().AddToSelection( objects );
				}
				else
				{
					GetDocument().SetSelection( std::move( objects ) );
				}
				m_PendingBox.reset();
				update();
				return true;
			}
			return false;
		case MapAction_t::MAP_ACTION_ESCAPE:
			if ( m_PendingBox || m_BoxFrom )
			{
				m_PendingBox.reset();
				m_BoxFrom.reset();
				update();
				return true;
			}
			return false;
		default:
			return false;
	}
}
