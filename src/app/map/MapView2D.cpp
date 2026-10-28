//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapView2D.h. Colours and grid rules are Hammer++'s (MapConstants.h).
//
//===========================================================================//
#include "MapView2D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <tuple>
#include <unordered_map>

#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include "Preferences.h"
#include "map/MapConstants.h"
#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "map/MapRenderData.h"
#include "map/MapResources.h"

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

//-----------------------------------------------------------------------------
// Purpose: Preferences > Maps, which most of what a 2D view draws follows.
//-----------------------------------------------------------------------------
const MapPreferences_t &Look()
{
	return CPreferences::Instance().GetMapPreferences();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's eight handles round a box, just outside its corners and sides, each with
//          where it is (-1, 0 or 1 across and down).
//-----------------------------------------------------------------------------
std::array<std::pair<QPoint, QRectF>, 8> HandlesOf( const QRectF &box )
{
	std::array<std::pair<QPoint, QRectF>, 8> handles;
	const double h = Look().m_nHandleSize;
	const double flOut = h / 2.0 + 2.0;
	std::size_t n = 0;
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
			handles[ n++ ] = { QPoint{ ix, iy }, QRectF{ x - h / 2.0, y - h / 2.0, h, h } };
		}
	}
	return handles;
}

//-----------------------------------------------------------------------------
// Purpose: Which of the eight handles an edit mode uses: resizing all of them, turning the
//          corners, shearing the sides.
//-----------------------------------------------------------------------------
bool HandleInMode( QPoint where, MapEditMode_t nMode )
{
	const bool bCorner = where.x() != 0 && where.y() != 0;
	return nMode == MapEditMode_t::MAP_EDIT_MODE_RESIZE || ( nMode == MapEditMode_t::MAP_EDIT_MODE_ROTATE ) == bCorner;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's handles drawn round box: squares to resize, circles to turn, bars to shear.
//-----------------------------------------------------------------------------
void PaintHandles( QPainter &painter, const QRectF &box, MapEditMode_t nMode = MapEditMode_t::MAP_EDIT_MODE_RESIZE )
{
	painter.setPen( QPen{ Qt::black, 0 } );
	painter.setBrush( Look().m_Colors.m_ToolHandle );
	for ( const auto &[ where, handle ] : HandlesOf( box ) )
	{
		if ( !HandleInMode( where, nMode ) )
		{
			continue;
		}
		switch ( nMode )
		{
			case MapEditMode_t::MAP_EDIT_MODE_RESIZE: painter.drawRect( handle ); break;
			case MapEditMode_t::MAP_EDIT_MODE_ROTATE:
				painter.setRenderHint( QPainter::Antialiasing, true );
				painter.drawEllipse( handle.adjusted( -1, -1, 1, 1 ) );
				painter.setRenderHint( QPainter::Antialiasing, false );
				break;
			case MapEditMode_t::MAP_EDIT_MODE_SHEAR:
				// A bar along the side it slides.
				painter.drawRect( where.x() == 0 ? handle.adjusted( -handle.width(), 0, handle.width(), 0 ) : handle.adjusted( 0, -handle.height(), 0, handle.height() ) );
				break;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A box's width and height written above it and to its left, as Hammer shows them.
//-----------------------------------------------------------------------------
void PaintSizes( QPainter &painter, const QRectF &box, QPointF size, const QColor &color )
{
	painter.setPen( color );
	const QString szWidth = QString::number( std::lround( size.x() ) );
	const QString szHeight = QString::number( std::lround( size.y() ) );
	const QFontMetrics metrics = painter.fontMetrics();
	painter.drawText( QPointF{ box.center().x() - metrics.horizontalAdvance( szWidth ) / 2.0, box.top() - 10.0 }, szWidth );
	painter.drawText( QPointF{ box.left() - metrics.horizontalAdvance( szHeight ) - 10.0, box.center().y() + metrics.ascent() / 2.0 }, szHeight );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QColor ToColor( const std::array<std::uint8_t, 3> &color )
{
	return QColor{ color[ 0 ], color[ 1 ], color[ 2 ] };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Each kind of line is left out once it would come closer than Hammer++'s smallest
//          spacing in pixels.
//-----------------------------------------------------------------------------
GridPlan_t PlanGrid( double flPixelsPerUnit, int nGridSize, int nHighlightEvery, bool bHideSmall, double flHideBelowPixels )
{
	// Without HideSmallGrid lines still stop where they would be closer than a pixel.
	const double flHideBelow = bHideSmall ? std::max( flHideBelowPixels, 1.0 ) : 1.0;
	auto shown = [ flPixelsPerUnit, flHideBelow ]( double flSpacing ) {
		return flSpacing * flPixelsPerUnit >= flHideBelow ? flSpacing : 0.0;
	};
	GridPlan_t plan;
	plan.m_flBase = shown( nGridSize );
	plan.m_flHighlight = shown( static_cast<double>( nGridSize ) * std::max( nHighlightEvery, 1 ) );
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
	std::shared_ptr<const xv::map::CMapMesh> pProps = m_Editor.GetResources().GetProps();
	if ( pScene == m_pBuiltFor && pProps == m_pProps )
	{
		return;
	}
	// The same map edited: only what the edits changed is projected again, while what is left
	// behind stays under a third of the segments.
	if ( pScene && m_pBuiltFor && pProps == m_pProps && pScene->m_pMesh.get() == m_pBuiltMesh && m_nStaleSegments * 3 < m_Segments.size() )
	{
		Patch( *pScene );
		return;
	}
	m_pBuiltMesh = pScene ? pScene->m_pMesh.get() : nullptr;
	m_PendingObjects.clear();
	m_nStaleSegments = 0;
	m_pBuiltFor = pScene;
	m_pProps = std::move( pProps );
	for ( QScrollBar *pBar : { m_pHorizontal, m_pVertical } )
	{
		pBar->setRange( static_cast<int>( -GetWorldLimit() ), static_cast<int>( GetWorldLimit() ) );
	}
	m_Objects.clear();
	m_Segments.clear();
	m_ObjectIndex.clear();
	if ( !pScene )
	{
		update();
		return;
	}
	m_ObjectIndex.assign( pScene->GetObjectCount(), NO_OBJECT );
	const xv::map::CMapMesh &mesh = pScene->GetMesh();
	for ( std::uint32_t i = 0; i < pScene->m_Solids.size(); i++ )
	{
		// Edited solids' lines come from the edit mesh.
		if ( pScene->IsEditedSolid( i ) )
		{
			PlaceSolid( *pScene, i );
			continue;
		}
		const xv::map::MeshRange_t range = i < mesh.m_SolidLines.size() ? mesh.m_SolidLines[ i ] : xv::map::MeshRange_t{};
		PlaceSolid( *pScene, i, mesh, range );
	}
	for ( std::size_t e = 0; e < pScene->m_Entities.size(); e++ )
	{
		PlaceEntity( *pScene, e );
	}
	const auto a0 = static_cast<std::size_t>( GetAxis( 0 ) );
	const auto a1 = static_cast<std::size_t>( GetAxis( 1 ) );
	// Props and instances: each one's edges lie together in its mesh, and its box becomes the
	// box around them.
	auto lines = [ & ]( const xv::map::CMapMesh &mesh, bool bInstances ) {
		for ( std::uint32_t k = 0; k + 1 < mesh.m_Lines.size(); )
		{
			const std::uint32_t nObject = mesh.m_Vertices[ mesh.m_Lines[ k ] ].m_nObject;
			const std::uint32_t nFirst = k;
			double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
			for ( ; k + 1 < mesh.m_Lines.size() && mesh.m_Vertices[ mesh.m_Lines[ k ] ].m_nObject == nObject; k += 2 )
			{
				for ( const std::uint32_t nVertex : { mesh.m_Lines[ k ], mesh.m_Lines[ k + 1 ] } )
				{
					const xv::CVec3 &p = mesh.m_Vertices[ nVertex ].m_vecPosition;
					const std::array<double, 3> at{ p.m_flX, p.m_flY, p.m_flZ };
					x0 = std::min( x0, at[ a0 ] );
					x1 = std::max( x1, at[ a0 ] );
					y0 = std::min( y0, at[ a1 ] );
					y1 = std::max( y1, at[ a1 ] );
				}
			}
			if ( nObject < m_ObjectIndex.size() && m_ObjectIndex[ nObject ] != NO_OBJECT )
			{
				MapFlatObject_t &object = m_Objects[ m_ObjectIndex[ nObject ] ];
				( bInstances ? object.m_nFirstInstanceLine : object.m_nFirstModelLine ) = nFirst / 2;
				( bInstances ? object.m_nInstanceLineCount : object.m_nModelLineCount ) = ( k - nFirst ) / 2;
				object.m_Bounds = QRectF{ QPointF{ x0, y0 }, QPointF{ x1, y1 } };
			}
		}
	};
	if ( m_pProps )
	{
		lines( *m_pProps, false );
	}
	lines( pScene->GetInstanceMesh(), true );
	update();
}

//-----------------------------------------------------------------------------
// Purpose: A solid's edges projected onto the view's axes, from range of mesh's lines. Edges that
//          land on each other, or on a point, are kept once. It takes the place it had among
//          the objects, or a new one.
//-----------------------------------------------------------------------------
void CMapView2D::PlaceSolid( const xv::map::CScene &scene, std::uint32_t nSolid, const xv::map::CMapMesh &mesh, xv::map::MeshRange_t range )
{
	const xv::map::SolidGeometry_t &geometry = scene.m_Solids[ nSolid ];
	const std::uint32_t nObject = scene.SolidObject( nSolid );
	if ( geometry.m_Faces.empty() || scene.m_Map.m_Solids[ nSolid ].m_bDeleted )
	{
		return;
	}
	const auto a0 = static_cast<std::size_t>( GetAxis( 0 ) );
	const auto a1 = static_cast<std::size_t>( GetAxis( 1 ) );
	MapFlatObject_t object;
	object.m_nObject = nObject;
	object.m_bInvalid = !geometry.IsValid();
	object.m_Bounds = QRectF{ QPointF{ geometry.m_Bounds.m_Mins[ a0 ], geometry.m_Bounds.m_Mins[ a1 ] }, QPointF{ geometry.m_Bounds.m_Maxs[ a0 ], geometry.m_Bounds.m_Maxs[ a1 ] } };
	object.m_nFirstSegment = static_cast<std::uint32_t>( m_Segments.size() );
	std::vector<std::array<std::int64_t, 4>> keys;
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
	Place( object );
}

//-----------------------------------------------------------------------------
// Purpose: An edited solid: its lines in the edit mesh.
//-----------------------------------------------------------------------------
void CMapView2D::PlaceSolid( const xv::map::CScene &scene, std::uint32_t nSolid )
{
	const xv::map::CMapMesh &edits = scene.GetEditMesh();
	const auto it = std::ranges::lower_bound( edits.m_SolidIndices, nSolid );
	const xv::map::MeshRange_t range = it != edits.m_SolidIndices.end() && *it == nSolid ? edits.m_SolidLines[ static_cast<std::size_t>( it - edits.m_SolidIndices.begin() ) ] : xv::map::MeshRange_t{};
	PlaceSolid( scene, nSolid, edits, range );
}

//-----------------------------------------------------------------------------
// Purpose: A point entity's box (brush entities show as their solids).
//-----------------------------------------------------------------------------
void CMapView2D::PlaceEntity( const xv::map::CScene &scene, std::size_t nEntity )
{
	if ( !scene.m_Entities[ nEntity ].m_bPoint || scene.m_Map.m_Entities[ nEntity ].m_bDeleted )
	{
		return;
	}
	const auto a0 = static_cast<std::size_t>( GetAxis( 0 ) );
	const auto a1 = static_cast<std::size_t>( GetAxis( 1 ) );
	const xv::map::Box_t box = scene.GetEntityBox( nEntity );
	MapFlatObject_t object;
	object.m_nObject = scene.EntityObject( nEntity );
	object.m_bPointEntity = true;
	object.m_Bounds = QRectF{ QPointF{ box.m_Mins[ a0 ], box.m_Mins[ a1 ] }, QPointF{ box.m_Maxs[ a0 ], box.m_Maxs[ a1 ] } };
	Place( object );
}

//-----------------------------------------------------------------------------
// Purpose: An object into the list: in the place it had (what its segments were becomes
//          stale), or a new one. A prop's or an instance's lines stay with it.
//-----------------------------------------------------------------------------
void CMapView2D::Place( MapFlatObject_t object )
{
	if ( object.m_nObject >= m_ObjectIndex.size() )
	{
		m_ObjectIndex.resize( object.m_nObject + 1, NO_OBJECT );
	}
	std::uint32_t &nIndex = m_ObjectIndex[ object.m_nObject ];
	if ( nIndex == NO_OBJECT )
	{
		nIndex = static_cast<std::uint32_t>( m_Objects.size() );
		m_Objects.push_back( object );
		return;
	}
	MapFlatObject_t &old = m_Objects[ nIndex ];
	m_nStaleSegments += old.m_nSegmentCount;
	object.m_nFirstModelLine = old.m_nFirstModelLine;
	object.m_nModelLineCount = old.m_nModelLineCount;
	object.m_nFirstInstanceLine = old.m_nFirstInstanceLine;
	object.m_nInstanceLineCount = old.m_nInstanceLineCount;
	old = object;
}

//-----------------------------------------------------------------------------
// Purpose: The objects edits changed since the view last drew, placed again.
//-----------------------------------------------------------------------------
void CMapView2D::Patch( const xv::map::CScene &scene )
{
	m_pBuiltFor = &scene;
	std::ranges::sort( m_PendingObjects );
	m_PendingObjects.erase( std::unique( m_PendingObjects.begin(), m_PendingObjects.end() ), m_PendingObjects.end() );
	for ( const std::uint32_t n : m_PendingObjects )
	{
		if ( n >= scene.GetObjectCount() )
		{
			continue;
		}
		if ( scene.IsSolidObject( n ) )
		{
			PlaceSolid( scene, scene.GetIndex( n ) );
		}
		else
		{
			PlaceEntity( scene, scene.GetIndex( n ) );
		}
	}
	m_PendingObjects.clear();
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::ObjectsChanged( const std::vector<std::uint32_t> &objects )
{
	m_PendingObjects.insert( m_PendingObjects.end(), objects.begin(), objects.end() );
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
	m_flZoom = std::clamp( std::min( ( width() - SCROLLBAR_SIZE ) / flWidth, ( height() - SCROLLBAR_SIZE ) / flHeight ) * 0.9, Look().m_flZoomSmallest, Look().m_flZoomLargest );
	SetCenter( ( mins + maxs ) / 2.0 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::SetZoom( double flZoom )
{
	m_flZoom = std::clamp( flZoom, Look().m_flZoomSmallest, Look().m_flZoomLargest );
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
	m_flZoom = std::clamp( m_flZoom * flFactor, Look().m_flZoomSmallest, Look().m_flZoomLargest );
	const QPointF after = ToMap( pivot );
	SetCenter( m_Center + ( before - after ) );
	m_Editor.ShowZoom( m_flZoom );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::SetCenter( QPointF center )
{
	const double flLimit = GetWorldLimit();
	m_Center = QPointF{ std::clamp( center.x(), -flLimit, flLimit ), std::clamp( center.y(), -flLimit, flLimit ) };
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
	m_pHorizontal->setSingleStep( std::max( 1, static_cast<int>( Look().m_nScrollStep / m_flZoom ) ) );
	m_pVertical->setSingleStep( std::max( 1, static_cast<int>( Look().m_nScrollStep / m_flZoom ) ) );
	m_pHorizontal->setValue( static_cast<int>( std::lround( m_Center.x() ) ) );
	m_pVertical->setValue( static_cast<int>( -std::lround( m_Center.y() ) ) );
	m_bUpdatingScrollBars = false;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's colours: the selection's, else the object's line colour (MapLineColors).
//-----------------------------------------------------------------------------
QColor CMapView2D::ColorOf( const MapFlatObject_t &object ) const
{
	if ( GetDocument().IsSelected( object.m_nObject ) )
	{
		return Look().m_Colors.m_Selection;
	}
	return m_pLineColors && object.m_nObject < m_pLineColors->size() ? ToColor( ( *m_pLineColors )[ object.m_nObject ] ) : Look().m_Colors.m_Brush;
}

//-----------------------------------------------------------------------------
// Purpose: Lines every so many units, from finest to coarsest, then the axes.
//-----------------------------------------------------------------------------
void CMapView2D::PaintGrid( QPainter &painter, const QRectF &all ) const
{
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	// The map can't reach past its limit, so neither does the grid: a line stops at the edge,
	// and beyond it there is only the background.
	const double flLimit = GetWorldLimit();
	const QRectF visible = all.intersected( QRectF{ -flLimit, -flLimit, 2.0 * flLimit, 2.0 * flLimit } );
	if ( visible.isEmpty() )
	{
		return;
	}
	const QRectF edge = QRectF{ ToWidget( visible.topLeft() ), ToWidget( visible.bottomRight() ) }.normalized();
	auto lines = [ & ]( double flSpacing, const QColor &color ) {
		if ( flSpacing <= 0.0 )
		{
			return;
		}
		std::vector<QLineF> found;
		for ( double x = std::ceil( visible.left() / flSpacing ) * flSpacing; x <= visible.right(); x += flSpacing )
		{
			const double flPixel = std::round( ToWidget( { x, 0 } ).x() ) + 0.5;
			found.emplace_back( flPixel, edge.top(), flPixel, edge.bottom() );
		}
		for ( double y = std::ceil( visible.top() / flSpacing ) * flSpacing; y <= visible.bottom(); y += flSpacing )
		{
			const double flPixel = std::round( ToWidget( { 0, y } ).y() ) + 0.5;
			found.emplace_back( edge.left(), flPixel, edge.right(), flPixel );
		}
		painter.setPen( QPen{ color, 0 } );
		painter.drawLines( found.data(), static_cast<int>( found.size() ) );
	};
	if ( settings.m_bShowGrid )
	{
		const MapColors_t &colors = Look().m_Colors;
		const GridPlan_t plan = PlanGrid( m_flZoom, settings.m_nGridSize, settings.m_nHighlightEvery, settings.m_bHideSmallGrid, Look().m_flHideGridBelow );
		lines( plan.m_flBase, colors.m_Grid );
		lines( plan.m_flSixtyFour, colors.m_GridHighlight );
		lines( plan.m_flHighlight, colors.m_GridHighlight );
		lines( plan.m_fl1024, colors.m_Grid1024 );
	}
	// The axes show with or without the grid.
	painter.setPen( QPen{ Look().m_Colors.m_Axis, 0 } );
	const QPointF origin = ToWidget( { 0, 0 } );
	const double flX = std::round( origin.x() ) + 0.5;
	const double flY = std::round( origin.y() ) + 0.5;
	painter.drawLine( QLineF{ flX, edge.top(), flX, edge.bottom() } );
	painter.drawLine( QLineF{ edge.left(), flY, edge.right(), flY } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CMapView2D::GetWorldLimit() const
{
	const xv::map::CScene *pScene = GetDocument().GetScene();
	return pScene ? pScene->m_flWorldLimit : mapstyle::WORLD_LIMIT;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapView2D::IsTexturedModel( const MapFlatObject_t &object, const QRectF &box ) const
{
	return object.m_nModelLineCount > 0 && m_Editor.GetSettings().m_bModels2D && Look().m_bTexturedModels2D && std::max( box.width(), box.height() ) >= Look().m_nModelsFromPixels;
}

//-----------------------------------------------------------------------------
// Purpose: The props big enough to show their models, drawn textured by the software renderer
//          looking straight along this view's axis, over the grid and under every line.
//          Selected ones are left to PaintObjects, which draws their edges, unless they show
//          their box instead (MapSelectedModelEdges).
//-----------------------------------------------------------------------------
void CMapView2D::PaintModels( QPainter &painter, const QRectF &visible ) const
{
	const CMapDocument &document = GetDocument();
	const xv::map::CScene *pScene = document.GetScene();
	if ( !pScene || !m_pProps || !m_Editor.GetSettings().m_bModels2D || !Look().m_bTexturedModels2D )
	{
		return;
	}
	const MapFrame_t frame = m_Editor.GetRenderData().GetShared();
	if ( !frame.m_pPropMesh || !frame.m_pMaterials )
	{
		return;
	}
	std::vector<std::uint8_t> flags( pScene->GetObjectCount(), 0 );
	bool bAny = false;
	for ( const MapFlatObject_t &object : m_Objects )
	{
		if ( object.m_nModelLineCount == 0 || !Overlaps( object.m_Bounds, visible ) || !document.IsShown( object.m_nObject ) || ( document.IsSelected( object.m_nObject ) && MapSelectedModelEdges( *pScene, object.m_nObject ) ) )
		{
			continue;
		}
		const QRectF box = QRectF{ ToWidget( object.m_Bounds.topLeft() ), ToWidget( object.m_Bounds.bottomRight() ) }.normalized();
		if ( IsTexturedModel( object, box ) )
		{
			flags[ object.m_nObject ] = xv::map::RASTER_OBJECT_SHOWN;
			bAny = true;
		}
	}
	if ( !bAny )
	{
		return;
	}
	const qreal flRatio = devicePixelRatioF();
	xv::map::Point_t centre{};
	centre[ static_cast<std::size_t>( GetAxis( 0 ) ) ] = m_Center.x();
	centre[ static_cast<std::size_t>( GetAxis( 1 ) ) ] = m_Center.y();
	const xv::map::Point_t forward = m_nType == MapViewType_t::MAP_VIEW_TYPE_FRONT ? xv::map::Point_t{ -1, 0, 0 } : m_nType == MapViewType_t::MAP_VIEW_TYPE_SIDE ? xv::map::Point_t{ 0, 1, 0 }
																																								 : xv::map::Point_t{ 0, 0, -1 };
	xv::map::CViewCamera camera;
	camera.m_nWidth = std::max( 1, static_cast<int>( std::lround( width() * flRatio ) ) );
	camera.m_nHeight = std::max( 1, static_cast<int>( std::lround( height() * flRatio ) ) );
	camera.LookAlongAxis( centre, forward, m_flZoom * flRatio, GetWorldLimit() * 2.0 );
	xv::map::RasterScene_t scene;
	scene.m_Meshes = { frame.m_pPropMesh.get() };
	scene.m_Materials = *frame.m_pMaterials;
	scene.m_ObjectFlags = flags;
	if ( frame.m_pFlatColors )
	{
		scene.m_ObjectColors = *frame.m_pFlatColors;
	}
	xv::map::RasterOptions_t options;
	options.m_Shade.m_nMode = xv::map::RasterMode_t::RASTER_MODE_TEXTURED;
	options.m_bTransparent = true;
	options.m_bSelectionEdges = false;
	xv::texture::CImage image;
	xv::map::RenderView( scene, camera, options, image );
	QImage picture( image.m_Pixels.data(), image.m_nWidth, image.m_nHeight, image.m_nWidth * 4, QImage::Format_RGBA8888 );
	picture.setDevicePixelRatio( flRatio );
	painter.drawImage( QPointF{ 0, 0 }, picture );
}

//-----------------------------------------------------------------------------
// Purpose: Every shown object in view, in batches of one colour; broken solids over the rest
//          (Hammer++ draws them over everything), and selected ones last, on top.
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
	const MapPreferences_t &look = Look();
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
	std::vector<QLineF> handles;
	std::vector<const MapFlatObject_t *> invalid;
	std::vector<const MapFlatObject_t *> selected;
	const double flHalfHandle = look.m_nHandleSize / 2.0;
	auto add = [ & ]( const MapFlatObject_t &object, Batch_t &batch, bool bSelected ) {
		const QRectF bounds{ ToWidget( object.m_Bounds.topLeft() ), ToWidget( object.m_Bounds.bottomRight() ) };
		const QRectF box = bounds.normalized();
		if ( box.width() < 1.0 && box.height() < 1.0 )
		{
			// Smaller than a pixel: a dot is all there is to see.
			batch.m_Points.push_back( box.center() );
			return;
		}
		// An instance shows what it holds, at every size: in the func_instance's colour, or the
		// preferences' own.
		Batch_t &contents = !bSelected && look.m_bInstanceColor2D && object.m_nInstanceLineCount > 0 ? batchFor( look.m_Colors.m_Instance2D ) : batch;
		const xv::map::CMapMesh &instances = pScene->GetInstanceMesh();
		for ( std::uint32_t k = 0; k < object.m_nInstanceLineCount; k++ )
		{
			const std::uint32_t nLine = ( object.m_nFirstInstanceLine + k ) * 2;
			const xv::CVec3 &a = instances.m_Vertices[ instances.m_Lines[ nLine ] ].m_vecPosition;
			const xv::CVec3 &b = instances.m_Vertices[ instances.m_Lines[ nLine + 1 ] ].m_vecPosition;
			contents.m_Lines.emplace_back( ToWidget( Flatten( { a.m_flX, a.m_flY, a.m_flZ } ) ), ToWidget( Flatten( { b.m_flX, b.m_flY, b.m_flZ } ) ) );
		}
		// A prop big enough on screen shows its model (Shift+M): textured (PaintModels) unless it is
		// selected, when Hammer++ shows its edges (or, for models that aren't props, their box over
		// the picture); else its box.
		const bool bModel = object.m_nModelLineCount > 0 && settings.m_bModels2D && std::max( box.width(), box.height() ) >= look.m_nModelsFromPixels;
		const bool bBoxed = bModel && bSelected && !MapSelectedModelEdges( *pScene, object.m_nObject );
		const bool bTextured = bModel && ( !bSelected || bBoxed ) && IsTexturedModel( object, box );
		if ( bModel && !bTextured && !bBoxed )
		{
			const xv::map::CMapMesh &props = *m_pProps;
			for ( std::uint32_t k = 0; k < object.m_nModelLineCount; k++ )
			{
				const std::uint32_t nLine = ( object.m_nFirstModelLine + k ) * 2;
				const xv::CVec3 &a = props.m_Vertices[ props.m_Lines[ nLine ] ].m_vecPosition;
				const xv::CVec3 &b = props.m_Vertices[ props.m_Lines[ nLine + 1 ] ].m_vecPosition;
				batch.m_Lines.emplace_back( ToWidget( Flatten( { a.m_flX, a.m_flY, a.m_flZ } ) ), ToWidget( Flatten( { b.m_flX, b.m_flY, b.m_flZ } ) ) );
			}
		}
		if ( bBoxed || ( !bModel && object.m_bPointEntity && object.m_nInstanceLineCount == 0 ) )
		{
			batch.m_Boxes.push_back( box.adjusted( 0.5, 0.5, -0.5, -0.5 ) );
		}
		const bool bCorners = settings.m_bDrawVertices && box.width() > look.m_nVerticesFromPixels && box.height() > look.m_nVerticesFromPixels;
		for ( std::uint32_t k = 0; k < object.m_nSegmentCount; k++ )
		{
			const QLineF &segment = m_Segments[ object.m_nFirstSegment + k ];
			const QLineF line{ ToWidget( segment.p1() ), ToWidget( segment.p2() ) };
			batch.m_Lines.push_back( line );
			if ( bCorners )
			{
				vertices.push_back( line.p1() );
				vertices.push_back( line.p2() );
			}
		}
		// The centre handle Hammer draws on every object big enough to have room for it.
		if ( settings.m_bCenterHandles && box.width() > look.m_nCenterHandlesFromPixels && box.height() > look.m_nCenterHandlesFromPixels )
		{
			const QPointF c = box.center();
			handles.emplace_back( c.x() - flHalfHandle, c.y() - flHalfHandle, c.x() + flHalfHandle, c.y() + flHalfHandle );
			handles.emplace_back( c.x() - flHalfHandle, c.y() + flHalfHandle, c.x() + flHalfHandle, c.y() - flHalfHandle );
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
		if ( object.m_bInvalid )
		{
			invalid.push_back( &object );
			continue;
		}
		add( object, batchFor( ColorOf( object ) ), false );
	}
	auto draw = [ & ]( const Batch_t &batch ) {
		painter.setPen( QPen{ batch.m_Color, 0 } );
		painter.setBrush( Qt::NoBrush );
		painter.drawLines( batch.m_Lines.data(), static_cast<int>( batch.m_Lines.size() ) );
		painter.drawRects( batch.m_Boxes.data(), static_cast<int>( batch.m_Boxes.size() ) );
		painter.drawPoints( batch.m_Points.data(), static_cast<int>( batch.m_Points.size() ) );
	};
	auto dots = [ & ] {
		if ( !vertices.empty() )
		{
			painter.setPen( QPen{ look.m_Colors.m_Vertex, static_cast<double>( look.m_nVertexSize ), Qt::SolidLine, Qt::SquareCap } );
			painter.drawPoints( vertices.data(), static_cast<int>( vertices.size() ) );
		}
		vertices.clear();
		if ( !handles.empty() )
		{
			painter.setPen( QPen{ look.m_Colors.m_CenterHandle, 0 } );
			painter.drawLines( handles.data(), static_cast<int>( handles.size() ) );
		}
		handles.clear();
	};
	for ( const Batch_t &batch : batches )
	{
		draw( batch );
	}
	Batch_t broken{ look.m_Colors.m_Invalid, {}, {}, {} };
	for ( const MapFlatObject_t *pObject : invalid )
	{
		add( *pObject, broken, false );
	}
	draw( broken );
	dots();
	Batch_t chosen{ look.m_Colors.m_Selection, {}, {}, {} };
	for ( const MapFlatObject_t *pObject : selected )
	{
		add( *pObject, chosen, true );
	}
	draw( chosen );
	// A selected entity's sphere helpers (a light's falloff distances, say), in its colour.
	for ( const MapFlatObject_t *pObject : selected )
	{
		if ( !settings.m_bHelpers || !pObject->m_bPointEntity || !m_pLineColors || pObject->m_nObject >= m_pLineColors->size() )
		{
			continue;
		}
		const xv::map::EntityVisual_t &visual = pScene->m_Entities[ pScene->GetIndex( pObject->m_nObject ) ];
		painter.setPen( QPen{ ToColor( ( *m_pLineColors )[ pObject->m_nObject ] ), 0 } );
		const QPointF centre = ToWidget( Flatten( visual.m_Origin ) );
		for ( const double flRadius : visual.m_Spheres )
		{
			if ( flRadius > 0.0 )
			{
				painter.drawEllipse( centre, flRadius * m_flZoom, flRadius * m_flZoom );
			}
		}
	}
	dots();

	// Entity names, where there's room for them.
	if ( settings.m_bNames && m_flZoom * 16.0 >= look.m_nNamesFromPixels )
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
			const std::size_t nEntity = pScene->GetIndex( object.m_nObject );
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
		QPen pen{ Look().m_Colors.m_ToolSelection, 0, Qt::DotLine };
		painter.setPen( pen );
		painter.setBrush( Qt::NoBrush );
		painter.drawRect( box );
	};
	if ( !document.GetSelection().empty() )
	{
		const xv::map::Box_t bounds = document.GetSelectionBounds();
		if ( !bounds.IsEmpty() )
		{
			const QRectF box = RectOf( bounds );
			painter.fillRect( box, Look().m_Colors.m_SelectionFill );
			dotted( box );
			PaintHandles( painter, box, m_Editor.GetEditMode() );
			// Its size on two sides, as Hammer shows it while selecting.
			PaintSizes( painter, box, Flatten( bounds.m_Maxs ) - Flatten( bounds.m_Mins ), Look().m_Colors.m_ToolSelection );
		}
	}
	if ( m_BoxFrom && m_PressAt != QPoint{} && !m_pMoveScene )
	{
		const QPointF now = mapFromGlobal( QCursor::pos() );
		dotted( QRectF{ ToWidget( *m_BoxFrom ), now }.normalized() );
	}
	else if ( m_PendingBox )
	{
		const QRectF box = QRectF{ ToWidget( m_PendingBox->topLeft() ), ToWidget( m_PendingBox->bottomRight() ) }.normalized();
		dotted( box );
		PaintHandles( painter, box );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The new box as Hammer's block tool draws it, with its handles and size; the entity
//          marker as lines across the view through where the entity will go.
//-----------------------------------------------------------------------------
void CMapView2D::PaintTools( QPainter &painter ) const
{
	const QColor &color = Look().m_Colors.m_ToolBlock;
	if ( const std::optional<std::vector<xv::map::Point_t>> &pointfile = GetDocument().GetPointfile(); pointfile && pointfile->size() >= 2 )
	{
		// The leak's line, from where vbsp started out to the entity, as Hammer draws it.
		QPolygonF line;
		for ( const xv::map::Point_t &p : *pointfile )
		{
			line << ToWidget( Flatten( p ) );
		}
		painter.setPen( QPen{ Look().m_Colors.m_Pointfile, 0 } );
		painter.drawPolyline( line );
	}
	if ( const std::optional<xv::map::Box_t> &newBox = m_Editor.GetNewBox() )
	{
		const QRectF box = RectOf( *newBox );
		painter.setPen( QPen{ color, 0 } );
		painter.setBrush( Qt::NoBrush );
		painter.drawRect( box );
		PaintHandles( painter, box );
		PaintSizes( painter, box, Flatten( newBox->m_Maxs ) - Flatten( newBox->m_Mins ), color );
	}
	if ( const std::optional<xv::map::Point_t> &marker = m_Editor.GetEntityMarker() )
	{
		const QPointF at = ToWidget( Flatten( *marker ) );
		painter.setPen( QPen{ color, 0, Qt::DotLine } );
		painter.drawLine( QPointF{ 0.0, at.y() }, QPointF{ static_cast<double>( width() ), at.y() } );
		painter.drawLine( QPointF{ at.x(), 0.0 }, QPointF{ at.x(), static_cast<double>( height() ) } );
		painter.setPen( QPen{ color, 0 } );
		painter.setBrush( Qt::NoBrush );
		painter.drawRect( QRectF{ at.x() - 4.0, at.y() - 4.0, 8.0, 8.0 } );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The 3D view's camera as Blender draws one, seen from this axis: a pyramid from the eye
//          to a rectangle the shape of the 3D view, its field of view wide, with a triangle over
//          the rectangle's top for which way is up. It is the same size on screen at any zoom.
//-----------------------------------------------------------------------------
void CMapView2D::PaintCamera( QPainter &painter ) const
{
	const std::optional<xv::map::CViewCamera> camera = m_Editor.GetCamera();
	if ( !camera )
	{
		return;
	}
	const MapPreferences_t &look = Look();
	const double flLength = look.m_nCameraPixels / std::max( m_flZoom, 1e-9 );
	const double flHalfWidth = flLength * std::tan( std::clamp( camera->m_flFov, 1.0, 179.0 ) * std::numbers::pi / 360.0 );
	const double flHalfHeight = flHalfWidth * std::max( camera->m_nHeight, 1 ) / std::max( camera->m_nWidth, 1 );
	const xv::map::Point_t forward = camera->GetForward();
	const xv::map::Point_t right = camera->GetRight();
	const xv::map::Point_t up = camera->GetUp();
	// A point on the rectangle's plane, so far right and up of its middle.
	auto at = [ & ]( double flRight, double flUp ) {
		const xv::map::Point_t p = xv::map::Add( xv::map::Add( xv::map::Add( camera->m_Eye, xv::map::Scale( forward, flLength ) ), xv::map::Scale( right, flRight ) ), xv::map::Scale( up, flUp ) );
		return ToWidget( Flatten( p ) );
	};
	const QPointF eye = ToWidget( Flatten( camera->m_Eye ) );
	const std::array<QPointF, 4> corners{ at( -flHalfWidth, flHalfHeight ), at( flHalfWidth, flHalfHeight ), at( flHalfWidth, -flHalfHeight ), at( -flHalfWidth, -flHalfHeight ) };
	painter.setRenderHint( QPainter::Antialiasing, true );
	painter.setPen( QPen{ look.m_Colors.m_Camera, 0 } );
	painter.setBrush( Qt::NoBrush );
	for ( std::size_t i = 0; i < corners.size(); i++ )
	{
		painter.drawLine( eye, corners[ i ] );
		painter.drawLine( corners[ i ], corners[ ( i + 1 ) % corners.size() ] );
	}
	const std::array<QPointF, 3> top{ at( -flHalfWidth * 0.6, flHalfHeight * 1.1 ), at( flHalfWidth * 0.6, flHalfHeight * 1.1 ), at( 0.0, flHalfHeight * 1.1 + flHalfWidth * 0.5 ) };
	painter.setBrush( look.m_Colors.m_Camera );
	painter.drawPolygon( top.data(), static_cast<int>( top.size() ) );
	painter.setRenderHint( QPainter::Antialiasing, false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::PaintLayer()
{
	LayerKey_t key;
	key.m_pScene = m_pBuiltFor;
	key.m_pProps = m_pProps.get();
	key.m_pMaterials = m_Editor.GetRenderData().GetShared().m_pMaterials.get();
	key.m_nState = GetDocument().GetStateGeneration();
	key.m_Settings = m_Editor.GetSettings();
	key.m_Center = m_Center;
	key.m_flZoom = m_flZoom;
	key.m_Size = size();
	key.m_flRatio = devicePixelRatioF();
	key.m_nRevision = CPreferences::Instance().GetMapRevision();
	if ( key == m_LayerKey && !m_Layer.isNull() )
	{
		return;
	}
	m_LayerKey = key;
	m_nLayerPaints++;
	if ( m_Layer.size() != size() * key.m_flRatio )
	{
		m_Layer = QPixmap( size() * key.m_flRatio );
		m_Layer.setDevicePixelRatio( key.m_flRatio );
	}
	m_Layer.fill( Look().m_Colors.m_Background );
	QPainter painter( &m_Layer );
	const QRectF visible = GetVisible();
	PaintGrid( painter, visible );
	PaintModels( painter, visible );
	PaintObjects( painter, visible );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView2D::paintEvent( QPaintEvent * )
{
	QElapsedTimer timer;
	timer.start();
	Rebuild();
	m_pLineColors = m_Editor.GetRenderData().GetLineColors( m_Editor.GetSettings() );
	PaintLayer();
	QPainter painter( this );
	painter.drawPixmap( 0, 0, m_Layer );
	PaintCamera( painter );
	PaintSelection( painter );
	PaintTools( painter );
	if ( hasFocus() )
	{
		// The view keys go to: Hammer marks it with its caption, a thin frame here.
		painter.setPen( QPen{ palette().highlight().color(), 0 } );
		painter.setBrush( Qt::NoBrush );
		painter.drawRect( rect().adjusted( 0, 0, -1 - SCROLLBAR_SIZE, -1 - SCROLLBAR_SIZE ) );
	}
	m_flPaintMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
	m_nPaints++;
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
	const double flReach = Look().m_nHandleSize / 2.0 + 2.0;
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
	if ( nTool == MapTool_t::MAP_TOOL_BLOCK )
	{
		if ( pEvent->button() == Qt::LeftButton )
		{
			PressBlock( at );
		}
		else if ( pEvent->button() == Qt::RightButton && m_Editor.GetNewBox() && RectOf( *m_Editor.GetNewBox() ).contains( at ) )
		{
			// Hammer's other way to make the box a block.
			QMenu menu( this );
			QAction *pCreate = menu.addAction( tr( "Create Object" ) );
			if ( menu.exec( pEvent->globalPosition().toPoint() ) == pCreate )
			{
				m_Editor.CreateBlock();
			}
		}
		return;
	}
	if ( nTool == MapTool_t::MAP_TOOL_ENTITY )
	{
		if ( pEvent->button() == Qt::LeftButton )
		{
			m_PressAt = at.toPoint();
			m_nBoxDrag = BoxDrag_t::BOX_DRAG_NEW;
			DragBlock( at, pEvent->modifiers() );
		}
		return;
	}
	if ( pEvent->button() == Qt::LeftButton )
	{
		m_PressAt = pEvent->position().toPoint();
		m_BoxFrom = ToMap( at );
		// On one of the selection's handles, a drag resizes, turns or shears it; inside its box, a
		// drag moves it (Hammer's selection tool).
		const xv::map::Box_t bounds = GetDocument().GetSelectionBounds();
		if ( !bounds.IsEmpty() && nTool == MapTool_t::MAP_TOOL_SELECT )
		{
			for ( const auto &[ where, handle ] : HandlesOf( RectOf( bounds ) ) )
			{
				if ( HandleInMode( where, m_Editor.GetEditMode() ) && handle.adjusted( -2.0, -2.0, 2.0, 2.0 ).contains( at ) )
				{
					m_HandleDrag = where;
					m_nHandleMode = m_Editor.GetEditMode();
					m_HandleFrom = ToMap( at );
					m_DragBounds = bounds;
					m_BoxFrom.reset();
					return;
				}
			}
			const double flSlack = 2.0 / m_flZoom;
			const QRectF box = QRectF{ Flatten( bounds.m_Mins ), Flatten( bounds.m_Maxs ) }.normalized().adjusted( -flSlack, -flSlack, flSlack, flSlack );
			if ( box.contains( ToMap( at ) ) )
			{
				m_MoveFrom = ToMap( at );
				m_MoveCorner = Flatten( bounds.m_Mins );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CMapView2D::Snap( double flValue, Qt::KeyboardModifiers modifiers ) const
{
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	if ( !settings.m_bSnapToGrid || modifiers.testFlag( Qt::AltModifier ) )
	{
		return flValue;
	}
	const auto flGrid = static_cast<double>( settings.m_nGridSize );
	return std::round( flValue / flGrid ) * flGrid;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QRectF CMapView2D::RectOf( const xv::map::Box_t &box ) const
{
	return QRectF{ ToWidget( Flatten( box.m_Mins ) ), ToWidget( Flatten( box.m_Maxs ) ) }.normalized();
}

//-----------------------------------------------------------------------------
// Purpose: On one of the box's handles, that side or corner follows the drag; inside it, the box
//          moves; anywhere else, a new box is dragged out from there.
//-----------------------------------------------------------------------------
void CMapView2D::PressBlock( QPointF at )
{
	m_PressAt = at.toPoint();
	m_nBoxDrag = BoxDrag_t::BOX_DRAG_NEW;
	m_bBoxDragMoved = false;
	m_BoxDragFrom = ToMap( at );
	if ( const std::optional<xv::map::Box_t> &box = m_Editor.GetNewBox() )
	{
		m_BoxDragStart = *box;
		const QRectF rect = RectOf( *box );
		for ( const auto &[ where, handle ] : HandlesOf( rect ) )
		{
			if ( handle.adjusted( -2.0, -2.0, 2.0, 2.0 ).contains( at ) )
			{
				m_nBoxDrag = BoxDrag_t::BOX_DRAG_HANDLE;
				m_BoxHandle = where;
				return;
			}
		}
		if ( rect.adjusted( -2.0, -2.0, 2.0, 2.0 ).contains( at ) )
		{
			m_nBoxDrag = BoxDrag_t::BOX_DRAG_MOVE;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The block tool's drag (and the entity tool's marker, which follows the mouse while
//          the button is down). Everything lands on the grid: a new box's corners, a pulled side,
//          a moved box's lowest corner.
//-----------------------------------------------------------------------------
void CMapView2D::DragBlock( QPointF at, Qt::KeyboardModifiers modifiers )
{
	const QPointF map = ToMap( at );
	const auto a0 = static_cast<std::size_t>( GetAxis( 0 ) );
	const auto a1 = static_cast<std::size_t>( GetAxis( 1 ) );
	const std::size_t a2 = 3 - a0 - a1;
	if ( m_Editor.GetTool() == MapTool_t::MAP_TOOL_ENTITY )
	{
		xv::map::Point_t marker = m_Editor.GetEntityMarker().value_or( xv::map::Point_t{} );
		marker[ a0 ] = Snap( map.x(), modifiers );
		marker[ a1 ] = Snap( map.y(), modifiers );
		m_Editor.SetEntityMarker( marker );
		return;
	}
	xv::map::Box_t box = m_BoxDragStart;
	switch ( m_nBoxDrag )
	{
		case BoxDrag_t::BOX_DRAG_NONE:
			return;
		case BoxDrag_t::BOX_DRAG_NEW:
		{
			// A click outside the box leaves it be.
			if ( !m_bBoxDragMoved && ( at.toPoint() - m_PressAt ).manhattanLength() < DRAG_THRESHOLD )
			{
				return;
			}
			m_bBoxDragMoved = true;
			const QPointF from{ Snap( m_BoxDragFrom.x(), modifiers ), Snap( m_BoxDragFrom.y(), modifiers ) };
			const QPointF to{ Snap( map.x(), modifiers ), Snap( map.y(), modifiers ) };
			const auto [ flLow, flHigh ] = m_Editor.GetNewBoxDepth( static_cast<int>( a2 ) );
			box.m_Mins[ a0 ] = std::min( from.x(), to.x() );
			box.m_Maxs[ a0 ] = std::max( from.x(), to.x() );
			box.m_Mins[ a1 ] = std::min( from.y(), to.y() );
			box.m_Maxs[ a1 ] = std::max( from.y(), to.y() );
			box.m_Mins[ a2 ] = flLow;
			box.m_Maxs[ a2 ] = flHigh;
			break;
		}
		case BoxDrag_t::BOX_DRAG_MOVE:
		{
			const QPointF corner{ box.m_Mins[ a0 ], box.m_Mins[ a1 ] };
			const QPointF moved = corner + ( map - m_BoxDragFrom );
			const QPointF delta = QPointF{ Snap( moved.x(), modifiers ), Snap( moved.y(), modifiers ) } - corner;
			box.m_Mins[ a0 ] += delta.x();
			box.m_Maxs[ a0 ] += delta.x();
			box.m_Mins[ a1 ] += delta.y();
			box.m_Maxs[ a1 ] += delta.y();
			break;
		}
		case BoxDrag_t::BOX_DRAG_HANDLE:
			// Down the widget is down the view's vertical axis: the handle on top pulls its maximum.
			if ( m_BoxHandle.x() != 0 )
			{
				( m_BoxHandle.x() < 0 ? box.m_Mins : box.m_Maxs )[ a0 ] = Snap( map.x(), modifiers );
			}
			if ( m_BoxHandle.y() != 0 )
			{
				( m_BoxHandle.y() < 0 ? box.m_Maxs : box.m_Mins )[ a1 ] = Snap( map.y(), modifiers );
			}
			for ( const std::size_t a : { a0, a1 } )
			{
				if ( box.m_Mins[ a ] > box.m_Maxs[ a ] )
				{
					std::swap( box.m_Mins[ a ], box.m_Maxs[ a ] );
				}
			}
			break;
	}
	m_Editor.SetNewBox( box );
}

//-----------------------------------------------------------------------------
// Purpose: Resizing pulls the side (or corner) held, the opposite one staying put, so the box's
//          edge lands on the grid. Turning goes about the box's centre, by the angle the mouse
//          has swept round it, in steps (Preferences). Shearing slides the side held along
//          itself, a grid step at a time, the opposite side staying.
//-----------------------------------------------------------------------------
xv::map::Transform_t CMapView2D::HandleTransform( QPointF at, Qt::KeyboardModifiers modifiers ) const
{
	using xv::map::Point_t;
	using xv::map::Transform_t;
	const QPoint h = m_HandleDrag.value_or( QPoint{} );
	const QPointF map = ToMap( at );
	const int a0 = GetAxis( 0 );
	const int a1 = GetAxis( 1 );
	const int a2 = 3 - a0 - a1;
	const auto n0 = static_cast<std::size_t>( a0 );
	const auto n1 = static_cast<std::size_t>( a1 );
	const xv::map::Box_t &bounds = m_DragBounds;
	switch ( m_nHandleMode )
	{
		case MapEditMode_t::MAP_EDIT_MODE_RESIZE:
		{
			Point_t factors{ 1, 1, 1 };
			Point_t pivot = bounds.m_Mins;
			// Down the widget is down the view's vertical axis: the top handle pulls the maximum.
			const std::array<std::tuple<int, std::size_t, bool, double>, 2> pulls{ { { h.x(), n0, h.x() > 0, map.x() }, { h.y(), n1, h.y() < 0, map.y() } } };
			for ( const auto &[ nHandle, nAxis, bMaxs, flTo ] : pulls )
			{
				if ( nHandle == 0 )
				{
					continue;
				}
				const double flDragged = bMaxs ? bounds.m_Maxs[ nAxis ] : bounds.m_Mins[ nAxis ];
				const double flFixed = bMaxs ? bounds.m_Mins[ nAxis ] : bounds.m_Maxs[ nAxis ];
				const double flNew = Snap( flTo, modifiers );
				if ( std::abs( flDragged - flFixed ) > 1e-9 && std::abs( flNew - flFixed ) > 1e-9 )
				{
					factors[ nAxis ] = ( flNew - flFixed ) / ( flDragged - flFixed );
					pivot[ nAxis ] = flFixed;
				}
			}
			return Transform_t::Scaling( pivot, factors );
		}
		case MapEditMode_t::MAP_EDIT_MODE_ROTATE:
		{
			const Point_t pivot = bounds.GetCenter();
			const QPointF centre = Flatten( pivot );
			const QPointF was = m_HandleFrom - centre;
			const QPointF now = map - centre;
			if ( QPointF::dotProduct( was, was ) < 1e-12 || QPointF::dotProduct( now, now ) < 1e-12 )
			{
				return {};
			}
			double flDegrees = ( std::atan2( now.y(), now.x() ) - std::atan2( was.y(), was.x() ) ) * 180.0 / std::numbers::pi;
			if ( !modifiers.testFlag( Qt::AltModifier ) )
			{
				const double flStep = std::max( 1.0, Look().m_flRotationSnap );
				flDegrees = std::round( flDegrees / flStep ) * flStep;
			}
			// Anticlockwise on screen is the right-hand turn about the axis out of the view only
			// where the view's axes are a right-handed pair (x/y and y/z; x/z is the other way).
			const double flSign = ( a0 + 1 ) % 3 == a1 ? 1.0 : -1.0;
			return Transform_t::Rotation( pivot, a2, flSign * flDegrees );
		}
		case MapEditMode_t::MAP_EDIT_MODE_SHEAR:
		{
			if ( ( h.x() == 0 ) == ( h.y() == 0 ) )
			{
				return {};
			}
			// The held side slides along the other axis; the opposite side is the pivot.
			const bool bAcross = h.y() != 0; // the top or bottom slides along axis 0
			const std::size_t nAxis = bAcross ? n1 : n0;   // across which the slide grows
			const std::size_t nAlong = bAcross ? n0 : n1;
			const double flDelta = Snap( bAcross ? map.x() - m_HandleFrom.x() : map.y() - m_HandleFrom.y(), modifiers );
			const double flExtent = bounds.m_Maxs[ nAxis ] - bounds.m_Mins[ nAxis ];
			if ( flExtent <= 1e-9 )
			{
				return {};
			}
			const bool bMaxsSlides = bAcross ? h.y() < 0 : h.x() > 0;
			Point_t pivot = bounds.m_Mins;
			pivot[ nAxis ] = bMaxsSlides ? bounds.m_Mins[ nAxis ] : bounds.m_Maxs[ nAxis ];
			return Transform_t::Shear( pivot, static_cast<int>( nAxis ), static_cast<int>( nAlong ), ( bMaxsSlides ? 1.0 : -1.0 ) * flDelta / flExtent );
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The handle's pull shown as it goes.
//-----------------------------------------------------------------------------
void CMapView2D::PreviewHandle( QPointF at, Qt::KeyboardModifiers modifiers )
{
	CMapDocument &document = GetDocument();
	if ( !m_pMoveScene )
	{
		m_pMoveScene = document.GetSharedScene();
		m_MoveSelection = document.GetSelection();
	}
	const xv::map::Transform_t transform = HandleTransform( at, modifiers );
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	document.Preview( xv::map::TransformObjects( *m_pMoveScene, m_MoveSelection, transform, settings.m_bTextureLock, settings.m_bScaleLock ) );
	if ( m_nHandleMode == MapEditMode_t::MAP_EDIT_MODE_ROTATE )
	{
		const double flSign = ( GetAxis( 0 ) + 1 ) % 3 == GetAxis( 1 ) ? 1.0 : -1.0;
		const auto &m = transform.m_Rotation;
		// The angle back out of the matrix, in the view's plane.
		const int a0 = GetAxis( 0 );
		const int a1 = GetAxis( 1 );
		const double flDegrees = flSign * std::atan2( m[ static_cast<std::size_t>( a1 * 3 + a0 ) ], m[ static_cast<std::size_t>( a0 * 3 + a0 ) ] ) * 180.0 / std::numbers::pi;
		m_Editor.ShowCursor( QStringLiteral( "%1°" ).arg( std::lround( flDegrees ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: How far a drag that began at m_MoveFrom moves the selection: snapped so the selection's
//          lowest corner lands on the grid (Alt moves freely), along the view's two axes.
//-----------------------------------------------------------------------------
xv::map::Point_t CMapView2D::DragOffset( QPointF at, Qt::KeyboardModifiers modifiers ) const
{
	QPointF delta = ToMap( at ) - *m_MoveFrom;
	const MapViewSettings_t &settings = m_Editor.GetSettings();
	if ( settings.m_bSnapToGrid && !modifiers.testFlag( Qt::AltModifier ) )
	{
		const auto flGrid = static_cast<double>( settings.m_nGridSize );
		const QPointF corner = m_MoveCorner + delta;
		delta = QPointF{ std::round( corner.x() / flGrid ) * flGrid, std::round( corner.y() / flGrid ) * flGrid } - m_MoveCorner;
	}
	xv::map::Point_t offset{};
	offset[ static_cast<std::size_t>( GetAxis( 0 ) ) ] = delta.x();
	offset[ static_cast<std::size_t>( GetAxis( 1 ) ) ] = delta.y();
	return offset;
}

//-----------------------------------------------------------------------------
// Purpose: The drag's move shown as it goes: the selection itself, or with Shift a copy of it.
//-----------------------------------------------------------------------------
void CMapView2D::PreviewMove( QPointF at, Qt::KeyboardModifiers modifiers )
{
	CMapDocument &document = GetDocument();
	if ( !m_pMoveScene )
	{
		m_pMoveScene = document.GetSharedScene();
		m_MoveSelection = document.GetSelection();
	}
	xv::map::Transform_t move;
	move.m_Offset = DragOffset( at, modifiers );
	const bool bLock = m_Editor.GetSettings().m_bTextureLock;
	document.Preview( modifiers.testFlag( Qt::ShiftModifier ) ? xv::map::CopyObjects( *m_pMoveScene, m_MoveSelection, move, bLock, nullptr ) : xv::map::TransformObjects( *m_pMoveScene, m_MoveSelection, move, bLock ) );
}

//-----------------------------------------------------------------------------
// Purpose: A drag's move given up (Esc): the scene as it was.
//-----------------------------------------------------------------------------
void CMapView2D::CancelMove()
{
	if ( m_pMoveScene )
	{
		GetDocument().EndPreview();
	}
	m_pMoveScene.reset();
	m_MoveSelection.clear();
	m_MoveFrom.reset();
	m_HandleDrag.reset();
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
	if ( m_nBoxDrag != BoxDrag_t::BOX_DRAG_NONE && ( pEvent->buttons() & Qt::LeftButton ) )
	{
		DragBlock( pEvent->position(), pEvent->modifiers() );
		return;
	}
	if ( m_HandleDrag && ( pEvent->buttons() & Qt::LeftButton ) )
	{
		PreviewHandle( pEvent->position(), pEvent->modifiers() );
		return;
	}
	if ( m_MoveFrom && ( pEvent->buttons() & Qt::LeftButton ) && ( m_pMoveScene || ( pEvent->position().toPoint() - m_PressAt ).manhattanLength() >= DRAG_THRESHOLD ) )
	{
		PreviewMove( pEvent->position(), pEvent->modifiers() );
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
	if ( m_nBoxDrag != BoxDrag_t::BOX_DRAG_NONE && pEvent->button() == Qt::LeftButton )
	{
		m_nBoxDrag = BoxDrag_t::BOX_DRAG_NONE;
		m_PressAt = {};
		return;
	}
	if ( pEvent->button() == Qt::LeftButton && m_HandleDrag )
	{
		// The handle's pull, as one step to undo.
		const xv::map::Transform_t transform = HandleTransform( pEvent->position(), pEvent->modifiers() );
		const QString szName = m_nHandleMode == MapEditMode_t::MAP_EDIT_MODE_RESIZE ? tr( "Resize" ) : m_nHandleMode == MapEditMode_t::MAP_EDIT_MODE_ROTATE ? tr( "Rotate" )
																																							: tr( "Shear" );
		CancelMove();
		m_Editor.TransformSelection( transform, szName );
		m_PressAt = {};
		update();
		return;
	}
	if ( pEvent->button() != Qt::LeftButton || !m_BoxFrom )
	{
		return;
	}
	if ( m_pMoveScene )
	{
		// The drag's move, as one step to undo; Shift held as it ends makes it a copy.
		const xv::map::Point_t offset = DragOffset( pEvent->position(), pEvent->modifiers() );
		CancelMove();
		if ( offset != xv::map::Point_t{} )
		{
			m_Editor.MoveSelection( offset, pEvent->modifiers().testFlag( Qt::ShiftModifier ) );
		}
		m_BoxFrom.reset();
		m_PressAt = {};
		update();
		return;
	}
	const bool bInside = m_MoveFrom.has_value();
	m_MoveFrom.reset();
	CMapDocument &document = GetDocument();
	const QPoint moved = pEvent->position().toPoint() - m_PressAt;
	const bool bControl = pEvent->modifiers().testFlag( Qt::ControlModifier );
	if ( std::abs( moved.x() ) < DRAG_THRESHOLD && std::abs( moved.y() ) < DRAG_THRESHOLD )
	{
		m_PendingBox.reset();
		if ( bInside && !bControl )
		{
			// A click inside the selection goes to the next of Hammer's edit modes.
			m_Editor.RunAction( MapAction_t::MAP_ACTION_EDIT_MODE, this );
		}
		else if ( const auto hit = HitAt( pEvent->position() ) )
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
		Zoom( std::pow( Look().m_flWheelZoom, flNotches ), pEvent->position() );
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
	const double flStep = Look().m_nScrollStep / m_flZoom;
	// The arrows nudge the selection a grid step along the view's axes (with Shift, a copy).
	auto nudge = [ this ]( int nRight, int nUp, bool bClone ) {
		const auto flGrid = static_cast<double>( m_Editor.GetSettings().m_nGridSize );
		xv::map::Point_t offset{};
		offset[ static_cast<std::size_t>( GetAxis( 0 ) ) ] = nRight * flGrid;
		offset[ static_cast<std::size_t>( GetAxis( 1 ) ) ] = nUp * flGrid;
		m_Editor.MoveSelection( offset, bClone );
		return true;
	};
	const bool bNudge = Look().m_bArrowsNudge && !GetDocument().GetSelection().empty();
	switch ( nAction )
	{
		case MapAction_t::MAP_ACTION_SCROLL_UP:
			if ( bNudge )
			{
				return nudge( 0, 1, false );
			}
			break;
		case MapAction_t::MAP_ACTION_SCROLL_DOWN:
			if ( bNudge )
			{
				return nudge( 0, -1, false );
			}
			break;
		case MapAction_t::MAP_ACTION_SCROLL_LEFT:
			if ( bNudge )
			{
				return nudge( -1, 0, false );
			}
			break;
		case MapAction_t::MAP_ACTION_SCROLL_RIGHT:
			if ( bNudge )
			{
				return nudge( 1, 0, false );
			}
			break;
		case MapAction_t::MAP_ACTION_CLONE_UP: return nudge( 0, 1, true );
		case MapAction_t::MAP_ACTION_CLONE_DOWN: return nudge( 0, -1, true );
		case MapAction_t::MAP_ACTION_CLONE_LEFT: return nudge( -1, 0, true );
		case MapAction_t::MAP_ACTION_CLONE_RIGHT: return nudge( 1, 0, true );
		default:
			break;
	}
	switch ( nAction )
	{
		case MapAction_t::MAP_ACTION_NEXT_AXIS:
			SetType( m_nType == MapViewType_t::MAP_VIEW_TYPE_TOP ? MapViewType_t::MAP_VIEW_TYPE_FRONT : m_nType == MapViewType_t::MAP_VIEW_TYPE_FRONT ? MapViewType_t::MAP_VIEW_TYPE_SIDE
																																					  : MapViewType_t::MAP_VIEW_TYPE_TOP );
			m_Editor.ViewTypeChanged( this );
			return true;
		case MapAction_t::MAP_ACTION_ZOOM_IN: Zoom( Look().m_flKeyZoom ); return true;
		case MapAction_t::MAP_ACTION_ZOOM_OUT: Zoom( 1.0 / Look().m_flKeyZoom ); return true;
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
			// The editor drops the box or marker; the drag mustn't bring it back.
			m_nBoxDrag = BoxDrag_t::BOX_DRAG_NONE;
			if ( m_pMoveScene || m_HandleDrag )
			{
				CancelMove();
				m_BoxFrom.reset();
				update();
				return true;
			}
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
