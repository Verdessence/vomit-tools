//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See BrushMesh.h.
//
//===========================================================================//
#include "BrushMesh.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "map/Clip.h"
#include "map/Vertex.h"

namespace xv::map
{

namespace
{

// How far a corner may lie off a plane and still be on it: Hammer's epsilon for brushes.
constexpr double ON_PLANE = 0.01;
// Half the size of the box concave shapes are cut from.
constexpr double CELL_BOUND = 65536.0;
constexpr std::string_view NODRAW = "TOOLS/TOOLSNODRAW";

//-----------------------------------------------------------------------------
// Purpose: The three points of a VMF side for a plane through polygon (along normal), so that
//          (p0 - p1) x (p2 - p1) points the way normal does.
//-----------------------------------------------------------------------------
std::array<Point_t, 3> PlanePoints( std::span<const Point_t> polygon, const Point_t &normal )
{
	std::array<Point_t, 3> points{ polygon[ 0 ], polygon[ 1 ], polygon[ 2 ] };
	for ( std::size_t k = 3; k < polygon.size() && Length( Cross( Subtract( points[ 0 ], points[ 1 ] ), Subtract( points[ 2 ], points[ 1 ] ) ) ) < 1e-6; k++ )
	{
		points[ 2 ] = polygon[ k ];
	}
	if ( Dot( Cross( Subtract( points[ 0 ], points[ 1 ] ), Subtract( points[ 2 ], points[ 1 ] ) ), normal ) < 0.0 )
	{
		std::swap( points[ 0 ], points[ 2 ] );
	}
	return points;
}

//-----------------------------------------------------------------------------
// Purpose: Newell's normal of a polygon (its area's direction, twice its size), wound as
//          FaceGeometry_t's faces are: clockwise seen from outside, so it points in.
//-----------------------------------------------------------------------------
Point_t NewellNormal( std::span<const Point_t> polygon )
{
	Point_t normal{};
	for ( std::size_t i = 0; i < polygon.size(); i++ )
	{
		normal = Add( normal, Cross( polygon[ i ], polygon[ ( i + 1 ) % polygon.size() ] ) );
	}
	return normal;
}

//-----------------------------------------------------------------------------
// Purpose: Whether p is inside the closed mesh: the solid angle its triangles make there (the
//          winding number), which is 4 pi inside and 0 outside however the shape bends.
//-----------------------------------------------------------------------------
bool IsInside( const std::vector<std::vector<Point_t>> &faces, const Point_t &p )
{
	double flAngle = 0.0;
	for ( const std::vector<Point_t> &face : faces )
	{
		for ( std::size_t k = 1; k + 1 < face.size(); k++ )
		{
			const Point_t a = Subtract( face[ 0 ], p );
			const Point_t b = Subtract( face[ k ], p );
			const Point_t c = Subtract( face[ k + 1 ], p );
			const double la = Length( a ), lb = Length( b ), lc = Length( c );
			const double flNumerator = Dot( a, Cross( b, c ) );
			const double flDenominator = la * lb * lc + Dot( a, b ) * lc + Dot( a, c ) * lb + Dot( b, c ) * la;
			flAngle += 2.0 * std::atan2( flNumerator, flDenominator );
		}
	}
	return std::abs( flAngle ) > 2.0 * std::numbers::pi;
}

// A convex piece of a concave shape: the planes round it, and its corners and volume.
struct Cell_t
{
	std::vector<Plane_t> m_Planes;
	std::vector<Polygon_t> m_Polygons;
	double m_flVolume = 0.0;
};

//-----------------------------------------------------------------------------
// Purpose: The polygons and volume of the convex solid behind planes; false when it is empty.
//-----------------------------------------------------------------------------
bool MakeCell( Cell_t &cell )
{
	std::optional<std::vector<Polygon_t>> polygons = IntersectHalfSpaces( cell.m_Planes, CELL_BOUND, 1e-6 );
	if ( !polygons || polygons->size() < 4 )
	{
		return false;
	}
	Point_t centre{};
	std::size_t nCount = 0;
	for ( const Polygon_t &polygon : *polygons )
	{
		for ( const Point_t &p : polygon.m_Points )
		{
			centre = Add( centre, p );
			nCount++;
		}
	}
	centre = Scale( centre, 1.0 / static_cast<double>( std::max<std::size_t>( nCount, 1 ) ) );
	double flVolume = 0.0;
	for ( const Polygon_t &polygon : *polygons )
	{
		for ( std::size_t k = 1; k + 1 < polygon.m_Points.size(); k++ )
		{
			flVolume += std::abs( Dot( Subtract( polygon.m_Points[ 0 ], centre ), Cross( Subtract( polygon.m_Points[ k ], centre ), Subtract( polygon.m_Points[ k + 1 ], centre ) ) ) ) / 6.0;
		}
	}
	if ( flVolume < 1e-3 )
	{
		return false;
	}
	cell.m_Polygons = std::move( *polygons );
	cell.m_flVolume = flVolume;
	// Only the planes that touch it: those a cut left behind it entirely add nothing.
	std::vector<Plane_t> used;
	for ( const Polygon_t &polygon : cell.m_Polygons )
	{
		if ( polygon.m_nPlane >= 0 )
			used.push_back( cell.m_Planes[ static_cast<std::size_t>( polygon.m_nPlane ) ] );
	}
	for ( std::size_t i = 0; i < cell.m_Polygons.size(); i++ )
	{
		cell.m_Polygons[ i ].m_nPlane = static_cast<int>( i );
	}
	cell.m_Planes = std::move( used );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t CellCentre( const Cell_t &cell )
{
	Point_t centre{};
	std::size_t nCount = 0;
	for ( const Polygon_t &polygon : cell.m_Polygons )
	{
		for ( const Point_t &p : polygon.m_Points )
		{
			centre = Add( centre, p );
			nCount++;
		}
	}
	return Scale( centre, 1.0 / static_cast<double>( std::max<std::size_t>( nCount, 1 ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool SamePlane( const Plane_t &a, const Plane_t &b )
{
	return Dot( a.m_Normal, b.m_Normal ) > 1.0 - 1e-6 && std::abs( a.m_flDistance - b.m_flDistance ) < ON_PLANE;
}

// A flat face of the mesh, ready to become a side.
struct FlatFace_t
{
	std::vector<Point_t> m_Points;
	Plane_t m_Plane;
	const BrushFace_t *m_pFace = nullptr;
};

//-----------------------------------------------------------------------------
// Purpose: The side for a face: its own side with the new plane, or (new or nodraw) one like
//          its source aligned to the world. polygon is wound as FaceGeometry_t's.
//-----------------------------------------------------------------------------
Side_t SideFor( const CMap &map, const Solid_t &source, const BrushFace_t *pFace, const Plane_t &plane, std::span<const Point_t> polygon )
{
	const Side_t &like = pFace && pFace->m_nSide >= 0 && static_cast<std::size_t>( pFace->m_nSide ) < source.m_Sides.size() ? source.m_Sides[ static_cast<std::size_t>( pFace->m_nSide ) ] : source.m_Sides.front();
	Side_t side;
	if ( pFace && !pFace->m_bNew && !pFace->m_bNodraw && pFace->m_nSide >= 0 )
	{
		side = like;
	}
	else
	{
		side = SideOnPlane( map, plane, like );
		side.m_nId = 0;
		if ( !pFace || pFace->m_bNodraw )
		{
			side.m_szMaterial = NODRAW;
			side.m_nMaterial = NEW_MATERIAL;
		}
	}
	std::vector<Point_t> fitted;
	fitted.reserve( polygon.size() );
	for ( const Point_t &point : polygon )
		fitted.push_back( Subtract( point, Scale( plane.m_Normal, plane.Distance( point ) ) ) );
	side.m_Plane = PlanePoints( fitted, plane.m_Normal );
	side.m_bPlaneRead = true;
	side.m_PlusVertices.assign( polygon.begin(), polygon.end() );
	side.m_Span = {};
	return side;
}

//-----------------------------------------------------------------------------
// Purpose: Decide which original face owns a fragment, including concave face outlines.
//-----------------------------------------------------------------------------
bool OnFace( const FlatFace_t &face, const Point_t &point )
{
	double flAngle = 0.0;
	for ( std::size_t i = 0; i < face.m_Points.size(); i++ )
	{
		const Point_t a = Subtract( face.m_Points[ i ], point );
		const Point_t b = Subtract( face.m_Points[ ( i + 1 ) % face.m_Points.size() ], point );
		flAngle += std::atan2( Dot( face.m_Plane.m_Normal, Cross( a, b ) ), Dot( a, b ) );
	}
	return std::abs( flAngle ) > std::numbers::pi;
}

//-----------------------------------------------------------------------------
// Purpose: Concave: cut the space inside the faces that are on the convex hull by the planes of
//          the faces that aren't, keep the cells inside the mesh, and merge neighbours while
//          their union is still convex. Each cell's sides come from the face on its plane, or
//          are nodraw where there is none.
//-----------------------------------------------------------------------------
std::vector<Solid_t> SplitConvex( const CMap &map, const Solid_t &source, const std::vector<FlatFace_t> &faces, const std::vector<Point_t> &corners )
{
	std::vector<Plane_t> outer;
	std::vector<Plane_t> cuts;
	for ( const FlatFace_t &face : faces )
	{
		const bool bReflex = std::ranges::any_of( corners, [ & ]( const Point_t &p ) { return face.m_Plane.Distance( p ) > ON_PLANE; } );
		std::vector<Plane_t> &list = bReflex ? cuts : outer;
		if ( std::ranges::none_of( list, [ & ]( const Plane_t &p ) { return SamePlane( p, face.m_Plane ); } ) )
			list.push_back( face.m_Plane );
	}
	std::vector<Cell_t> cells( 1 );
	cells.front().m_Planes = outer;
	if ( !MakeCell( cells.front() ) )
	{
		// The outer faces alone don't close it (a shape folded over itself): start from its hull.
		const std::optional<std::vector<Plane_t>> hull = ConvexHull( corners );
		if ( !hull )
			return {};
		cells.front().m_Planes = *hull;
		if ( !MakeCell( cells.front() ) )
			return {};
	}
	for ( const Plane_t &cut : cuts )
	{
		std::vector<Cell_t> next;
		for ( Cell_t &cell : cells )
		{
			bool bFront = false, bBack = false;
			for ( const Polygon_t &polygon : cell.m_Polygons )
			{
				for ( const Point_t &p : polygon.m_Points )
				{
					const double d = cut.Distance( p );
					bFront |= d > ON_PLANE;
					bBack |= d < -ON_PLANE;
				}
			}
			if ( !bFront || !bBack )
			{
				next.push_back( std::move( cell ) );
				continue;
			}
			Cell_t behind;
			behind.m_Planes = cell.m_Planes;
			behind.m_Planes.push_back( cut );
			Cell_t before;
			before.m_Planes = cell.m_Planes;
			before.m_Planes.push_back( { Scale( cut.m_Normal, -1.0 ), -cut.m_flDistance } );
			if ( MakeCell( behind ) )
				next.push_back( std::move( behind ) );
			if ( MakeCell( before ) )
				next.push_back( std::move( before ) );
		}
		if ( next.size() > 4096 )
			return {};
		cells = std::move( next );
	}
	std::vector<std::vector<Point_t>> surface;
	for ( const FlatFace_t &face : faces )
		surface.push_back( face.m_Points );
	std::erase_if( cells, [ & ]( const Cell_t &cell ) { return !IsInside( surface, CellCentre( cell ) ); } );
	// Merge two cells whenever their hull holds exactly the two (their union is convex).
	for ( bool bMerged = true; bMerged; )
	{
		bMerged = false;
		for ( std::size_t i = 0; i < cells.size() && !bMerged; i++ )
		{
			for ( std::size_t j = i + 1; j < cells.size() && !bMerged; j++ )
			{
				std::vector<Point_t> points;
				for ( const Cell_t *pCell : { &cells[ i ], &cells[ j ] } )
					for ( const Polygon_t &polygon : pCell->m_Polygons )
						points.insert( points.end(), polygon.m_Points.begin(), polygon.m_Points.end() );
				const std::optional<std::vector<Plane_t>> hull = ConvexHull( points );
				if ( !hull )
					continue;
				Cell_t merged;
				merged.m_Planes = *hull;
				if ( !MakeCell( merged ) )
					continue;
				const double flBoth = cells[ i ].m_flVolume + cells[ j ].m_flVolume;
				if ( std::abs( merged.m_flVolume - flBoth ) <= std::max( 1e-3, flBoth * 1e-6 ) )
				{
					cells[ i ] = std::move( merged );
					cells.erase( cells.begin() + static_cast<std::ptrdiff_t>( j ) );
					bMerged = true;
				}
			}
		}
	}
	std::vector<Solid_t> pieces;
	for ( const Cell_t &cell : cells )
	{
		Solid_t piece = source;
		piece.m_Sides.clear();
		for ( const Polygon_t &polygon : cell.m_Polygons )
		{
			const Plane_t &plane = cell.m_Planes[ static_cast<std::size_t>( polygon.m_nPlane ) ];
			// A cell face can be partly exposed and partly internal on the same plane.
			// Cut at the source outlines before assigning a material to each fragment.
			std::vector<std::vector<Point_t>> fragments{ polygon.m_Points };
			for ( const FlatFace_t &face : faces )
			{
				if ( !SamePlane( face.m_Plane, plane ) )
					continue;
				for ( std::size_t i = 0; i < face.m_Points.size(); i++ )
				{
					const Point_t normal = Normalized( Cross( plane.m_Normal, Subtract( face.m_Points[ ( i + 1 ) % face.m_Points.size() ], face.m_Points[ i ] ) ) );
					const Plane_t cut{ normal, Dot( normal, face.m_Points[ i ] ) };
					std::vector<std::vector<Point_t>> next;
					for ( const auto &fragment : fragments )
					{
						const bool bFront = std::ranges::any_of( fragment, [ & ]( const Point_t &p ) { return cut.Distance( p ) > ON_PLANE; } );
						const bool bBack = std::ranges::any_of( fragment, [ & ]( const Point_t &p ) { return cut.Distance( p ) < -ON_PLANE; } );
						if ( !bFront || !bBack )
						{
							next.push_back( fragment );
							continue;
						}
						next.push_back( ClipPolygon( fragment, cut, 1e-6 ) );
						next.push_back( ClipPolygon( fragment, { Scale( normal, -1 ), -cut.m_flDistance }, 1e-6 ) );
					}
					fragments = std::move( next );
				}
			}
			for ( const auto &fragment : fragments )
			{
				if ( fragment.size() < 3 || Length( NewellNormal( fragment ) ) < 1e-6 )
					continue;
				Point_t centre{};
				for ( const Point_t &p : fragment )
					centre = Add( centre, p );
				centre = Scale( centre, 1.0 / static_cast<double>( fragment.size() ) );
				const auto found = std::ranges::find_if( faces, [ & ]( const FlatFace_t &face ) { return SamePlane( face.m_Plane, plane ) && OnFace( face, centre ); } );
				std::vector<Point_t> points( fragment.rbegin(), fragment.rend() );
				piece.m_Sides.push_back( SideFor( map, source, found != faces.end() ? found->m_pFace : nullptr, plane, points ) );
			}
		}
		piece.m_bChanged = true;
		pieces.push_back( std::move( piece ) );
	}
	// A side's id belongs to one face: the first piece's first face on it keeps it, every other
	// face gets a new one.
	std::vector<int> used;
	for ( Solid_t &piece : pieces )
	{
		for ( Side_t &side : piece.m_Sides )
		{
			if ( side.m_nId != 0 && std::ranges::find( used, side.m_nId ) != used.end() )
				side.m_nId = 0;
			if ( side.m_nId != 0 )
				used.push_back( side.m_nId );
		}
	}
	for ( std::size_t i = 1; i < pieces.size(); i++ )
	{
		for ( Side_t &side : pieces[ i ].m_Sides )
			side.m_nId = 0;
	}
	return pieces;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBrushMesh CBrushMesh::FromGeometry( const SolidGeometry_t &shape )
{
	const SolidVertices_t corners = VerticesOf( shape );
	CBrushMesh mesh;
	mesh.m_Vertices = corners.m_Vertices;
	for ( std::size_t f = 0; f < shape.m_Faces.size(); f++ )
	{
		BrushFace_t face;
		face.m_Corners = corners.m_FaceVertices[ f ];
		face.m_nSide = static_cast<std::int32_t>( shape.m_Faces[ f ].m_nSide );
		mesh.m_Faces.push_back( std::move( face ) );
	}
	return mesh;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::pair<std::uint32_t, std::uint32_t>> CBrushMesh::GetEdges() const
{
	std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
	for ( const BrushFace_t &face : m_Faces )
	{
		for ( std::size_t i = 0; i < face.m_Corners.size(); i++ )
		{
			const std::uint32_t a = face.m_Corners[ i ];
			const std::uint32_t b = face.m_Corners[ ( i + 1 ) % face.m_Corners.size() ];
			if ( a != b )
				edges.emplace_back( std::min( a, b ), std::max( a, b ) );
		}
	}
	std::ranges::sort( edges );
	edges.erase( std::ranges::unique( edges ).begin(), edges.end() );
	return edges;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t CBrushMesh::FaceCenter( std::size_t nFace ) const
{
	Point_t centre{};
	const auto &corners = m_Faces[ nFace ].m_Corners;
	for ( const std::uint32_t n : corners )
		centre = Add( centre, m_Vertices[ n ] );
	return corners.empty() ? centre : Scale( centre, 1.0 / static_cast<double>( corners.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: The corners' winding is clockwise seen from outside, so Newell's normal points in.
//-----------------------------------------------------------------------------
std::optional<Plane_t> CBrushMesh::FacePlane( std::size_t nFace ) const
{
	std::vector<Point_t> polygon;
	for ( const std::uint32_t n : m_Faces[ nFace ].m_Corners )
		polygon.push_back( m_Vertices[ n ] );
	if ( polygon.size() < 3 )
		return std::nullopt;
	const Point_t inward = NewellNormal( polygon );
	if ( Length( inward ) < 1e-9 )
		return std::nullopt;
	const Point_t normal = Normalized( Scale( inward, -1.0 ) );
	return Plane_t{ normal, Dot( normal, FaceCenter( nFace ) ) };
}

//-----------------------------------------------------------------------------
// Purpose: Reject malformed surfaces before indexing corners or fitting face planes.
//-----------------------------------------------------------------------------
std::string CBrushMesh::Validate() const
{
	if ( m_Vertices.size() < 4 || m_Faces.size() < 4 )
		return "the brush needs at least four corners and four faces";
	for ( const Point_t &point : m_Vertices )
		for ( const double flCoordinate : point )
			if ( !std::isfinite( flCoordinate ) )
				return "a corner has a non-finite coordinate";
	std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, int>> edges;
	for ( std::size_t f = 0; f < m_Faces.size(); f++ )
	{
		const auto &corners = m_Faces[ f ].m_Corners;
		if ( corners.size() < 3 )
			return "a face has fewer than three corners";
		for ( std::size_t i = 0; i < corners.size(); i++ )
		{
			const auto a = corners[ i ], b = corners[ ( i + 1 ) % corners.size() ];
			if ( a >= m_Vertices.size() || b >= m_Vertices.size() )
				return "a face refers to a missing corner";
			if ( a == b )
				return "a face contains a collapsed edge";
			// A subdivision may end on the middle of its neighbour's edge.
			const Point_t delta = Subtract( m_Vertices[ b ], m_Vertices[ a ] );
			const double flLengthSquared = Dot( delta, delta );
			if ( flLengthSquared < 1e-12 )
				return "a face contains a collapsed edge";
			std::vector<std::pair<double, std::uint32_t>> along{ { 0, a }, { 1, b } };
			for ( std::uint32_t n = 0; n < m_Vertices.size(); n++ )
			{
				const double t = Dot( Subtract( m_Vertices[ n ], m_Vertices[ a ] ), delta ) / flLengthSquared;
				if ( t > 1e-6 && t < 1.0 - 1e-6 && Length( Subtract( m_Vertices[ n ], Add( m_Vertices[ a ], Scale( delta, t ) ) ) ) < 1e-6 )
					along.emplace_back( t, n );
			}
			std::ranges::sort( along );
			for ( std::size_t k = 1; k < along.size(); k++ )
			{
				const auto first = along[ k - 1 ].second, second = along[ k ].second;
				auto &edge = edges[ { std::min( first, second ), std::max( first, second ) } ];
				edge.first++;
				edge.second += first < second ? 1 : -1;
			}
		}
		if ( !FacePlane( f ) )
			return "a face has no area";
	}
	for ( const auto &[ edge, counts ] : edges )
		if ( counts.first != 2 || counts.second != 0 )
			return "the brush surface is open or inconsistently wound";
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CBrushMesh::IsFlat( std::size_t nFace, double flEpsilon ) const
{
	const std::optional<Plane_t> plane = FacePlane( nFace );
	return plane && std::ranges::all_of( m_Faces[ nFace ].m_Corners, [ & ]( std::uint32_t n ) { return std::abs( plane->Distance( m_Vertices[ n ] ) ) <= flEpsilon; } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CBrushMesh::IsConvex( double flEpsilon ) const
{
	if ( !Validate().empty() )
		return false;
	for ( std::size_t f = 0; f < m_Faces.size(); f++ )
	{
		if ( !IsFlat( f, flEpsilon ) )
			return false;
		const Plane_t plane = *FacePlane( f );
		if ( std::ranges::any_of( m_Vertices, [ & ]( const Point_t &p ) { return plane.Distance( p ) > flEpsilon; } ) )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Corners brought onto each other (merged) count as one: a face keeps each once.
//-----------------------------------------------------------------------------
void CBrushMesh::Tidy()
{
	std::vector<std::uint32_t> welded( m_Vertices.size() );
	for ( std::uint32_t i = 0; i < m_Vertices.size(); i++ )
	{
		welded[ i ] = i;
		for ( std::uint32_t j = 0; j < i; j++ )
			if ( Length( Subtract( m_Vertices[ i ], m_Vertices[ j ] ) ) <= 1e-6 )
			{
				welded[ i ] = welded[ j ];
				break;
			}
	}
	for ( BrushFace_t &face : m_Faces )
		for ( auto &n : face.m_Corners )
			n = welded[ n ];
	for ( BrushFace_t &face : m_Faces )
	{
		std::vector<std::uint32_t> corners;
		for ( const std::uint32_t n : face.m_Corners )
		{
			if ( corners.empty() || Length( Subtract( m_Vertices[ corners.back() ], m_Vertices[ n ] ) ) > 1e-6 )
				corners.push_back( n );
		}
		while ( corners.size() > 1 && Length( Subtract( m_Vertices[ corners.front() ], m_Vertices[ corners.back() ] ) ) <= 1e-6 )
			corners.pop_back();
		face.m_Corners = std::move( corners );
	}
	std::erase_if( m_Faces, [ this ]( const BrushFace_t &face ) {
		if ( face.m_Corners.size() < 3 )
			return true;
		std::vector<Point_t> polygon;
		for ( const std::uint32_t n : face.m_Corners )
			polygon.push_back( m_Vertices[ n ] );
		return Length( NewellNormal( polygon ) ) < 1e-6;
	} );
	std::vector<std::int64_t> remap( m_Vertices.size(), -1 );
	std::vector<Point_t> kept;
	for ( BrushFace_t &face : m_Faces )
	{
		for ( std::uint32_t &n : face.m_Corners )
		{
			if ( remap[ n ] < 0 )
			{
				remap[ n ] = static_cast<std::int64_t>( kept.size() );
				kept.push_back( m_Vertices[ n ] );
			}
			n = static_cast<std::uint32_t>( remap[ n ] );
		}
	}
	m_Vertices = std::move( kept );
}

//-----------------------------------------------------------------------------
// Purpose: Divide the outline into two consistently wound faces along a diagonal.
//-----------------------------------------------------------------------------
bool SplitFace( CBrushMesh &mesh, std::uint32_t nFace, std::uint32_t nFirst, std::uint32_t nSecond )
{
	if ( nFace >= mesh.m_Faces.size() || nFirst == nSecond )
		return false;
	const BrushFace_t original = mesh.m_Faces[ nFace ];
	const auto first = std::ranges::find( original.m_Corners, nFirst );
	const auto second = std::ranges::find( original.m_Corners, nSecond );
	if ( first == original.m_Corners.end() || second == original.m_Corners.end() )
		return false;
	std::size_t a = first - original.m_Corners.begin(), b = second - original.m_Corners.begin();
	if ( a > b )
		std::swap( a, b );
	if ( b - a < 2 || b - a + 1 == original.m_Corners.size() )
		return false;
	BrushFace_t left = original, right = original;
	left.m_Corners.assign( original.m_Corners.begin() + a, original.m_Corners.begin() + b + 1 );
	right.m_Corners.assign( original.m_Corners.begin() + b, original.m_Corners.end() );
	right.m_Corners.insert( right.m_Corners.end(), original.m_Corners.begin(), original.m_Corners.begin() + a + 1 );
	mesh.m_Faces[ nFace ] = std::move( left );
	mesh.m_Faces.push_back( std::move( right ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Split all crossed faces and share each new corner between adjacent faces.
//-----------------------------------------------------------------------------
bool CutRing( CBrushMesh &mesh, const Plane_t &plane )
{
	if ( !mesh.Validate().empty() || Length( plane.m_Normal ) < 1e-9 )
		return false;
	CBrushMesh result = mesh;
	result.m_Faces.clear();
	bool bCut = false;
	auto indexOf = [ & ]( const Point_t &point ) {
		for ( std::uint32_t n = 0; n < result.m_Vertices.size(); n++ )
			if ( Length( Subtract( point, result.m_Vertices[ n ] ) ) < 1e-6 )
				return n;
		result.m_Vertices.push_back( point );
		return static_cast<std::uint32_t>( result.m_Vertices.size() - 1 );
	};
	for ( const BrushFace_t &face : mesh.m_Faces )
	{
		bool bFront = false, bBack = false;
		for ( const auto n : face.m_Corners )
		{
			bFront |= plane.Distance( mesh.m_Vertices[ n ] ) > ON_PLANE;
			bBack |= plane.Distance( mesh.m_Vertices[ n ] ) < -ON_PLANE;
		}
		if ( !bFront || !bBack )
		{
			result.m_Faces.push_back( face );
			continue;
		}
		bCut = true;
		BrushFace_t front = face, back = face;
		front.m_Corners.clear();
		back.m_Corners.clear();
		for ( std::size_t i = 0; i < face.m_Corners.size(); i++ )
		{
			const auto a = face.m_Corners[ i ], b = face.m_Corners[ ( i + 1 ) % face.m_Corners.size() ];
			const double da = plane.Distance( mesh.m_Vertices[ a ] ), db = plane.Distance( mesh.m_Vertices[ b ] );
			if ( da >= -ON_PLANE )
				front.m_Corners.push_back( a );
			if ( da <= ON_PLANE )
				back.m_Corners.push_back( a );
			if ( ( da > ON_PLANE && db < -ON_PLANE ) || ( da < -ON_PLANE && db > ON_PLANE ) )
			{
				const auto n = indexOf( Add( mesh.m_Vertices[ a ], Scale( Subtract( mesh.m_Vertices[ b ], mesh.m_Vertices[ a ] ), da / ( da - db ) ) ) );
				front.m_Corners.push_back( n );
				back.m_Corners.push_back( n );
			}
		}
		result.m_Faces.push_back( std::move( front ) );
		result.m_Faces.push_back( std::move( back ) );
	}
	if ( !bCut || !result.Validate().empty() )
		return false;
	mesh = std::move( result );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Intersect adjacent offset edge lines on a convex face, rejecting collapsed insets.
//-----------------------------------------------------------------------------
bool InsetFace( CBrushMesh &mesh, std::uint32_t nFace, double flDistance )
{
	if ( nFace >= mesh.m_Faces.size() || !std::isfinite( flDistance ) || flDistance <= 0 || !mesh.IsConvex( ON_PLANE ) )
		return false;
	const BrushFace_t face = mesh.m_Faces[ nFace ];
	const auto plane = mesh.FacePlane( nFace );
	const std::size_t nCount = face.m_Corners.size();
	std::vector<Plane_t> edges;
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		const Point_t a = mesh.m_Vertices[ face.m_Corners[ i ] ], b = mesh.m_Vertices[ face.m_Corners[ ( i + 1 ) % nCount ] ];
		const Point_t normal = Normalized( Cross( plane->m_Normal, Subtract( b, a ) ) );
		edges.push_back( { normal, Dot( normal, a ) - flDistance } );
	}
	std::vector<Point_t> inset;
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		const Plane_t &a = edges[ ( i + nCount - 1 ) % nCount ], &b = edges[ i ];
		const Point_t cross = Cross( a.m_Normal, b.m_Normal );
		const double d = Dot( cross, plane->m_Normal );
		if ( std::abs( d ) < 1e-9 )
			return false;
		const Point_t p = Scale( Add( Add( Scale( Cross( b.m_Normal, plane->m_Normal ), a.m_flDistance ), Scale( Cross( plane->m_Normal, a.m_Normal ), b.m_flDistance ) ), Scale( cross, plane->m_flDistance ) ), 1.0 / d );
		if ( std::ranges::any_of( edges, [ & ]( const Plane_t &edge ) { return edge.Distance( p ) > 1e-6; } ) )
			return false;
		inset.push_back( p );
	}
	if ( Length( NewellNormal( inset ) ) < 1e-6 )
		return false;
	const auto nStart = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
	mesh.m_Vertices.insert( mesh.m_Vertices.end(), inset.begin(), inset.end() );
	mesh.m_Faces[ nFace ].m_Corners.clear();
	for ( std::uint32_t i = 0; i < nCount; i++ )
	{
		const auto j = static_cast<std::uint32_t>( ( i + 1 ) % nCount );
		mesh.m_Faces[ nFace ].m_Corners.push_back( nStart + i );
		BrushFace_t ring = face;
		ring.m_bNew = true;
		ring.m_Corners = { face.m_Corners[ i ], face.m_Corners[ j ], nStart + j, nStart + i };
		mesh.m_Faces.push_back( std::move( ring ) );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A face that isn't flat becomes a fan of triangles when the brush is to be split (so
//          each piece's faces are flat), and stays one bent side when it is to be kept.
//-----------------------------------------------------------------------------
BrushSolids_t ToSolids( const CMap &map, const Solid_t &source, const CBrushMesh &input, BrushConcave_t nConcave )
{
	BrushSolids_t result;
	CBrushMesh mesh = input;
	for ( const auto &face : mesh.m_Faces )
		for ( const auto n : face.m_Corners )
			if ( n >= mesh.m_Vertices.size() )
			{
				result.m_szProblem = "a face refers to a missing corner";
				return result;
			}
	for ( const auto &point : mesh.m_Vertices )
		for ( const double flCoordinate : point )
			if ( !std::isfinite( flCoordinate ) )
			{
				result.m_szProblem = "a corner has a non-finite coordinate";
				return result;
			}
	mesh.Tidy();
	result.m_szProblem = mesh.Validate();
	if ( !result.m_szProblem.empty() )
		return result;
	if ( mesh.m_Faces.size() < 4 || source.m_Sides.empty() )
	{
		result.m_szProblem = "nothing would be left of it";
		return result;
	}
	result.m_bConcave = !mesh.IsConvex( ON_PLANE );
	std::vector<FlatFace_t> faces;
	for ( std::size_t f = 0; f < mesh.m_Faces.size(); f++ )
	{
		const BrushFace_t &face = mesh.m_Faces[ f ];
		std::vector<Point_t> polygon;
		for ( const std::uint32_t n : face.m_Corners )
			polygon.push_back( mesh.m_Vertices[ n ] );
		if ( mesh.IsFlat( f, ON_PLANE ) || nConcave == BrushConcave_t::BRUSH_CONCAVE_KEEP )
		{
			faces.push_back( { std::move( polygon ), *mesh.FacePlane( f ), &face } );
			continue;
		}
		for ( std::size_t k = 1; k + 1 < polygon.size(); k++ )
		{
			std::vector<Point_t> triangle{ polygon[ 0 ], polygon[ k ], polygon[ k + 1 ] };
			const Point_t inward = NewellNormal( triangle );
			if ( Length( inward ) < 1e-9 )
				continue;
			const Point_t normal = Normalized( Scale( inward, -1.0 ) );
			faces.push_back( { std::move( triangle ), { normal, Dot( normal, polygon[ 0 ] ) }, &face } );
		}
	}
	bool bConvex = true;
	for ( const FlatFace_t &face : faces )
	{
		if ( nConcave == BrushConcave_t::BRUSH_CONCAVE_KEEP )
			break;
		if ( std::ranges::any_of( mesh.m_Vertices, [ & ]( const Point_t &p ) { return face.m_Plane.Distance( p ) > ON_PLANE; } ) )
			bConvex = false;
	}
	if ( bConvex || nConcave == BrushConcave_t::BRUSH_CONCAVE_KEEP )
	{
		Solid_t solid = source;
		solid.m_Sides.clear();
		std::vector<std::int32_t> used;
		for ( const FlatFace_t &face : faces )
		{
			solid.m_Sides.push_back( SideFor( map, source, face.m_pFace, face.m_Plane, face.m_Points ) );
			// A side split into several faces keeps its id on the first.
			if ( face.m_pFace->m_nSide >= 0 && !face.m_pFace->m_bNew && !face.m_pFace->m_bNodraw )
			{
				if ( std::ranges::find( used, face.m_pFace->m_nSide ) != used.end() )
					solid.m_Sides.back().m_nId = 0;
				used.push_back( face.m_pFace->m_nSide );
			}
		}
		solid.m_bChanged = true;
		result.m_Solids.push_back( std::move( solid ) );
		return result;
	}
	result.m_Solids = SplitConvex( map, source, faces, mesh.m_Vertices );
	if ( result.m_Solids.empty() )
		result.m_szProblem = "it couldn't be split into convex pieces";
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: Every plane through three of the points with all the others behind it; brushes have
//          few corners, so the plain way is quick enough and exact.
//-----------------------------------------------------------------------------
std::optional<std::vector<Plane_t>> ConvexHull( std::span<const Point_t> input, double flEpsilon )
{
	std::vector<Point_t> points;
	for ( const Point_t &p : input )
	{
		if ( std::ranges::none_of( points, [ & ]( const Point_t &q ) { return Length( Subtract( p, q ) ) <= flEpsilon; } ) )
			points.push_back( p );
	}
	std::vector<Plane_t> planes;
	const std::size_t n = points.size();
	for ( std::size_t i = 0; i < n; i++ )
	{
		for ( std::size_t j = i + 1; j < n; j++ )
		{
			for ( std::size_t k = j + 1; k < n; k++ )
			{
				const Point_t normal = Cross( Subtract( points[ j ], points[ i ] ), Subtract( points[ k ], points[ i ] ) );
				if ( Length( normal ) < 1e-9 )
					continue;
				Plane_t plane{ Normalized( normal ), 0.0 };
				plane.m_flDistance = Dot( plane.m_Normal, points[ i ] );
				bool bFront = false, bBack = false;
				for ( const Point_t &p : points )
				{
					const double d = plane.Distance( p );
					bFront |= d > flEpsilon;
					bBack |= d < -flEpsilon;
				}
				if ( bFront && bBack )
					continue;
				if ( bFront )
					plane = { Scale( plane.m_Normal, -1.0 ), -plane.m_flDistance };
				if ( !bFront && !bBack )
					continue; // every point on it: no volume this way
				if ( std::ranges::none_of( planes, [ & ]( const Plane_t &p ) { return Dot( p.m_Normal, plane.m_Normal ) > 1.0 - 1e-9 && std::abs( p.m_flDistance - plane.m_flDistance ) <= flEpsilon; } ) )
					planes.push_back( plane );
			}
		}
	}
	if ( planes.size() < 4 )
		return std::nullopt;
	return planes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> CornersOf( const CBrushMesh &mesh, BrushPartKind_t nKind, std::uint32_t nIndex )
{
	switch ( nKind )
	{
		case BrushPartKind_t::BRUSH_PART_VERTEX:
			return nIndex < mesh.m_Vertices.size() ? std::vector<std::uint32_t>{ nIndex } : std::vector<std::uint32_t>{};
		case BrushPartKind_t::BRUSH_PART_EDGE:
		{
			const auto edges = mesh.GetEdges();
			return nIndex < edges.size() ? std::vector<std::uint32_t>{ edges[ nIndex ].first, edges[ nIndex ].second } : std::vector<std::uint32_t>{};
		}
		case BrushPartKind_t::BRUSH_PART_FACE:
			return nIndex < mesh.m_Faces.size() ? mesh.m_Faces[ nIndex ].m_Corners : std::vector<std::uint32_t>{};
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t PartPosition( const CBrushMesh &mesh, BrushPartKind_t nKind, std::uint32_t nIndex )
{
	const std::vector<std::uint32_t> corners = CornersOf( mesh, nKind, nIndex );
	Point_t centre{};
	for ( const std::uint32_t n : corners )
		centre = Add( centre, mesh.m_Vertices[ n ] );
	return corners.empty() ? centre : Scale( centre, 1.0 / static_cast<double>( corners.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: A corner named by several parts moves once. Pieces after the first a split makes
//          are new solids, numbered after the map's.
//-----------------------------------------------------------------------------
MapEdit_t MoveParts( const CScene &scene, std::span<const BrushPart_t> parts, const Transform_t &transform, BrushConcave_t nConcave, std::vector<std::uint32_t> *pAdded, std::string *pProblem )
{
	if ( pProblem )
		pProblem->clear();
	MapEdit_t edit;
	std::map<std::uint32_t, std::vector<std::uint32_t>> bySolid;
	std::map<std::uint32_t, CBrushMesh> meshes;
	for ( const BrushPart_t &part : parts )
	{
		if ( part.m_nObject >= scene.GetObjectCount() || !scene.IsSolidObject( part.m_nObject ) || scene.IsDeleted( part.m_nObject ) )
			continue;
		const std::uint32_t nSolid = scene.GetIndex( part.m_nObject );
		auto [ it, bNew ] = meshes.try_emplace( nSolid );
		if ( bNew )
			it->second = CBrushMesh::FromGeometry( scene.m_Solids[ nSolid ] );
		const std::vector<std::uint32_t> corners = CornersOf( it->second, part.m_nKind, part.m_nIndex );
		bySolid[ nSolid ].insert( bySolid[ nSolid ].end(), corners.begin(), corners.end() );
	}
	int nNextId = scene.m_nNextObjectId;
	int nNextSide = scene.m_nNextSideId;
	std::vector<std::uint32_t> addedObjects;
	std::uint32_t nObject = scene.GetObjectCount();
	auto nNewIndex = static_cast<std::uint32_t>( scene.m_Map.m_Solids.size() );
	for ( auto &[ nSolid, corners ] : bySolid )
	{
		std::ranges::sort( corners );
		corners.erase( std::ranges::unique( corners ).begin(), corners.end() );
		CBrushMesh &mesh = meshes[ nSolid ];
		if ( corners.empty() )
			continue;
		const Solid_t &source = scene.m_Map.m_Solids[ nSolid ];
		for ( const BrushFace_t &face : mesh.m_Faces )
		{
			if ( face.m_nSide < 0 || !source.m_Sides[ face.m_nSide ].m_pDisp )
				continue;
			if ( std::ranges::any_of( face.m_Corners, [ & ]( std::uint32_t n ) { return std::ranges::binary_search( corners, n ); } ) )
			{
				if ( pProblem )
					*pProblem = "the selection touches a displacement's corners";
				return {};
			}
		}
		for ( const std::uint32_t n : corners )
			mesh.m_Vertices[ n ] = transform.Apply( mesh.m_Vertices[ n ] );
		Solid_t textured = source;
		const Solid_t transformed = TransformSolid( source, transform, true );
		for ( const BrushFace_t &face : mesh.m_Faces )
		{
			if ( face.m_nSide >= 0 && std::ranges::all_of( face.m_Corners, [ & ]( std::uint32_t n ) { return std::ranges::binary_search( corners, n ); } ) )
			{
				textured.m_Sides[ face.m_nSide ].m_UAxis = transformed.m_Sides[ face.m_nSide ].m_UAxis;
				textured.m_Sides[ face.m_nSide ].m_VAxis = transformed.m_Sides[ face.m_nSide ].m_VAxis;
			}
		}
		BrushSolids_t solids = ToSolids( scene.m_Map, textured, mesh, nConcave );
		if ( solids.m_Solids.empty() )
		{
			if ( pProblem )
				*pProblem = solids.m_szProblem;
			return {};
		}
		if ( solids.m_Solids.size() > 1 && source.m_Editor.m_nGroup == 0 )
		{
			Group_t group;
			group.m_nId = nNextId++;
			group.m_Editor = source.m_Editor;
			group.m_Editor.m_bChanged = true;
			edit.m_Groups.push_back( group );
			for ( Solid_t &piece : solids.m_Solids )
			{
				piece.m_Editor.m_nGroup = group.m_nId;
				piece.m_Editor.m_bChanged = true;
			}
		}
		for ( std::size_t i = 0; i < solids.m_Solids.size(); i++ )
		{
			if ( i == 0 )
			{
				for ( Side_t &side : solids.m_Solids[ 0 ].m_Sides )
					side.m_nId = side.m_nId == 0 ? nNextSide++ : side.m_nId;
				edit.m_Solids.emplace_back( nSolid, std::move( solids.m_Solids[ 0 ] ) );
				continue;
			}
			Solid_t added = AsNew( std::move( solids.m_Solids[ i ] ), nNextId, nNextSide );
			added.m_nEntity = source.m_nEntity;
			edit.m_Solids.emplace_back( nNewIndex++, std::move( added ) );
			addedObjects.push_back( nObject++ );
		}
	}
	if ( pAdded )
		pAdded->insert( pAdded->end(), addedObjects.begin(), addedObjects.end() );
	return edit;
}

} // namespace xv::map
