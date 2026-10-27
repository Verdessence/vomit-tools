//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Brushes.h.
//
//===========================================================================//
#include "Brushes.h"

#include <algorithm>
#include <iterator>
#include <optional>

#include "util/Parallel.h"

namespace xv::map
{

namespace
{

// Half the width of the square each face starts as: past any coordinate a map can hold, small
// enough that cutting it down keeps double precision.
constexpr double WINDING_EXTENT = 131072.0;
constexpr double COORDINATE_LIMIT = 16384.0;

// What building a solid uses over and over, kept on each thread between solids: cutting faces
// then allocates nothing once these have grown, and only the finished faces are allocated (at
// their size). A big map's solids otherwise make millions of short-lived polygons on every
// core, which leaves the heap in pieces long after the map is gone.
struct BrushScratch_t
{
	std::vector<std::optional<Plane_t>> m_Planes;
	std::vector<Point_t> m_Points;
	std::vector<Point_t> m_Clipped;
	std::vector<Point_t> m_Plus;
	std::vector<FaceGeometry_t> m_Faces;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
BrushScratch_t &Scratch()
{
	thread_local BrushScratch_t scratch;
	return scratch;
}

//-----------------------------------------------------------------------------
// Purpose: Whether two planes are the same one.
//-----------------------------------------------------------------------------
bool IsSamePlane( const Plane_t &a, const Plane_t &b, double flEpsilon )
{
	return Dot( a.m_Normal, b.m_Normal ) > 1.0 - 1e-9 && std::abs( a.m_flDistance - b.m_flDistance ) < flEpsilon;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer++'s polygon for side nSide, wound clockwise from outside, when it agrees with
//          the planes: every corner on the side's own plane and behind all the others, within
//          flTolerance. Hammer writes plane points rounded, so where off-grid planes meet they
//          can leave slivers of edge that Hammer++'s exact corners don't have. The polygon goes
//          into points.
//-----------------------------------------------------------------------------
bool UsablePlusVertices( const std::vector<Point_t> &plus, std::size_t nSide, const std::vector<std::optional<Plane_t>> &planes, double flTolerance, std::vector<Point_t> &points )
{
	points.clear();
	for ( const Point_t &p : plus )
	{
		if ( points.empty() || Length( Subtract( p, points.back() ) ) > 1e-6 )
		{
			points.push_back( p );
		}
	}
	while ( points.size() > 1 && Length( Subtract( points.front(), points.back() ) ) <= 1e-6 )
	{
		points.pop_back();
	}
	if ( points.size() < 3 )
	{
		return false;
	}
	const Plane_t &own = *planes[ nSide ];
	for ( const Point_t &p : points )
	{
		if ( std::abs( own.Distance( p ) ) > flTolerance )
		{
			return false;
		}
		for ( std::size_t j = 0; j < planes.size(); j++ )
		{
			if ( j != nSide && planes[ j ] && planes[ j ]->Distance( p ) > flTolerance )
			{
				return false;
			}
		}
	}
	Point_t vecArea{};
	for ( std::size_t k = 1; k + 1 < points.size(); k++ )
	{
		vecArea = Add( vecArea, Cross( Subtract( points[ k ], points[ 0 ] ), Subtract( points[ k + 1 ], points[ 0 ] ) ) );
	}
	const double flTurn = Dot( vecArea, own.m_Normal );
	if ( flTurn == 0.0 )
	{
		return false;
	}
	if ( flTurn > 0.0 )
	{
		std::ranges::reverse( points );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Drops corners that sit on the one before them (where several planes meet at a
//          point, cutting can leave the same corner twice).
//-----------------------------------------------------------------------------
void RemoveRepeats( std::vector<Point_t> &points, double flEpsilon )
{
	std::size_t nKept = 0;
	for ( std::size_t i = 0; i < points.size(); i++ )
	{
		const Point_t d = nKept == 0 ? Point_t{ 1e30, 0, 0 } : Subtract( points[ i ], points[ nKept - 1 ] );
		if ( Dot( d, d ) > flEpsilon * flEpsilon )
		{
			points[ nKept++ ] = points[ i ];
		}
	}
	points.resize( nKept );
	while ( points.size() > 1 )
	{
		const Point_t d = Subtract( points.front(), points.back() );
		if ( Dot( d, d ) > flEpsilon * flEpsilon )
		{
			break;
		}
		points.pop_back();
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const FaceGeometry_t *SolidGeometry_t::FindFace( std::uint32_t nSide ) const
{
	const auto it = std::ranges::find_if( m_Faces, [ nSide ]( const FaceGeometry_t &face ) { return face.m_nSide == nSide; } );
	return it == m_Faces.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose: Each side's plane as a big square, cut by every other side's plane.
//-----------------------------------------------------------------------------
SolidGeometry_t BuildSolid( const Solid_t &solid, const BrushOptions_t &options )
{
	SolidGeometry_t geometry;
	BrushScratch_t &scratch = Scratch();
	std::vector<std::optional<Plane_t>> &planes = scratch.m_Planes;
	planes.assign( solid.m_Sides.size(), std::nullopt );
	for ( std::size_t i = 0; i < solid.m_Sides.size(); i++ )
	{
		const Side_t &side = solid.m_Sides[ i ];
		if ( !side.m_bPlaneRead )
		{
			geometry.m_nProblems |= SOLID_PROBLEM_UNREADABLE_PLANE;
			continue;
		}
		planes[ i ] = PlaneFromPoints( side.m_Plane );
		if ( !planes[ i ] )
		{
			geometry.m_nProblems |= SOLID_PROBLEM_DEGENERATE_PLANE;
			continue;
		}
		for ( std::size_t j = 0; j < i; j++ )
		{
			if ( planes[ j ] && IsSamePlane( *planes[ i ], *planes[ j ], options.m_flEpsilon ) )
			{
				geometry.m_nProblems |= SOLID_PROBLEM_DUPLICATE_PLANE;
				planes[ i ].reset();
				break;
			}
		}
	}
	if ( std::ranges::count_if( planes, []( const auto &plane ) { return plane.has_value(); } ) < 4 )
	{
		geometry.m_nProblems |= SOLID_PROBLEM_TOO_FEW_SIDES;
	}

	for ( std::size_t i = 0; i < planes.size(); i++ )
	{
		if ( !planes[ i ] )
		{
			continue;
		}
		std::vector<Point_t> &points = scratch.m_Points;
		BaseWinding( *planes[ i ], WINDING_EXTENT, points );
		for ( std::size_t j = 0; j < planes.size() && points.size() >= 3; j++ )
		{
			if ( j != i && planes[ j ] )
			{
				ClipPolygon( points, *planes[ j ], options.m_flEpsilon, scratch.m_Clipped );
				std::swap( points, scratch.m_Clipped );
			}
		}
		RemoveRepeats( points, options.m_flEpsilon );
		if ( points.size() < 3 )
		{
			geometry.m_nProblems |= SOLID_PROBLEM_SIDE_WITHOUT_FACE;
			continue;
		}
		FaceGeometry_t face;
		face.m_nSide = static_cast<std::uint32_t>( i );
		face.m_Plane = *planes[ i ];
		face.m_bPlusVertices = UsablePlusVertices( solid.m_Sides[ i ].m_PlusVertices, i, planes, options.m_flPlusTolerance, scratch.m_Plus );
		const std::vector<Point_t> &corners = face.m_bPlusVertices ? scratch.m_Plus : points;
		face.m_Points.assign( corners.begin(), corners.end() );
		for ( const Point_t &p : face.m_Points )
		{
			geometry.m_Bounds.Add( p );
		}
		scratch.m_Faces.push_back( std::move( face ) );
	}
	geometry.m_Faces.reserve( scratch.m_Faces.size() );
	std::ranges::move( scratch.m_Faces, std::back_inserter( geometry.m_Faces ) );
	scratch.m_Faces.clear();

	if ( geometry.m_Faces.empty() )
	{
		geometry.m_nProblems |= SOLID_PROBLEM_EMPTY;
	}
	else
	{
		// A corner out at the starting square's edge means no side closed the solid off there.
		for ( std::size_t a = 0; a < 3; a++ )
		{
			if ( geometry.m_Bounds.m_Maxs[ a ] > WINDING_EXTENT * 0.5 || geometry.m_Bounds.m_Mins[ a ] < -WINDING_EXTENT * 0.5 )
			{
				geometry.m_nProblems |= SOLID_PROBLEM_OPEN;
			}
			if ( geometry.m_Bounds.m_Maxs[ a ] > COORDINATE_LIMIT || geometry.m_Bounds.m_Mins[ a ] < -COORDINATE_LIMIT )
			{
				geometry.m_nProblems |= SOLID_PROBLEM_OUT_OF_BOUNDS;
			}
		}
		if ( geometry.m_Faces.size() < 4 )
		{
			geometry.m_nProblems |= SOLID_PROBLEM_OPEN;
		}
	}
	return geometry;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<SolidGeometry_t> BuildSolids( const CMap &map, const BrushOptions_t &options )
{
	std::vector<SolidGeometry_t> solids( map.m_Solids.size() );
	ParallelFor( solids.size(), 256, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t i = nBegin; i < nEnd; i++ )
		{
			solids[ i ] = BuildSolid( map.m_Solids[ i ], options );
		}
	} );
	return solids;
}

} // namespace xv::map
