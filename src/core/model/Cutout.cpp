//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Preserve extruded cutout geometry as convex prisms without volumetric approximation.
//===========================================================================//
#include "Vmf.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
namespace xv::model
{
namespace
{
using Point_t = std::array<double, 3>;
using Polygon_t = std::vector<int>;
//-----------------------------------------------------------------------------
// Purpose: Projects in double precision so thin cap layers remain distinguishable.
//-----------------------------------------------------------------------------
double DotPoint( Point_t a, Point_t b )
{
	return a[ 0 ] * b[ 0 ] + a[ 1 ] * b[ 1 ] + a[ 2 ] * b[ 2 ];
}
//-----------------------------------------------------------------------------
// Purpose: Measures cap triangle area independently of render winding.
//-----------------------------------------------------------------------------
Point_t CrossPoint( Point_t a, Point_t b )
{
	return { a[ 1 ] * b[ 2 ] - a[ 2 ] * b[ 1 ], a[ 2 ] * b[ 0 ] - a[ 0 ] * b[ 2 ], a[ 0 ] * b[ 1 ] - a[ 1 ] * b[ 0 ] };
}
//-----------------------------------------------------------------------------
// Purpose: Forms a direction between two points.
//-----------------------------------------------------------------------------
Point_t Difference( Point_t a, Point_t b )
{
	return { a[ 0 ] - b[ 0 ], a[ 1 ] - b[ 1 ], a[ 2 ] - b[ 2 ] };
}
//-----------------------------------------------------------------------------
// Purpose: Tests a turn in the cap plane, retaining concavities as separate brushes.
//-----------------------------------------------------------------------------
double Turn( Point_t a, Point_t b, Point_t c, Point_t vecNormal )
{
	return DotPoint( CrossPoint( Difference( b, a ), Difference( c, b ) ), vecNormal );
}
//-----------------------------------------------------------------------------
// Purpose: Joins adjacent polygons only when their exact boundary remains simple and convex.
//-----------------------------------------------------------------------------
std::optional<Polygon_t> Merge( const Polygon_t &a, std::size_t i, const Polygon_t &b, std::size_t j, const std::vector<Point_t> &points, Point_t vecNormal, int nMaxSides )
{
	Polygon_t polygon;
	for ( std::size_t k = 0; k < a.size(); k++ )
		polygon.push_back( a[ ( i + 1 + k ) % a.size() ] );
	for ( std::size_t k = 2; k < b.size(); k++ )
		polygon.push_back( b[ ( j + k ) % b.size() ] );
	if ( polygon.size() + 2 > static_cast<std::size_t>( nMaxSides ) || std::set<int>( polygon.begin(), polygon.end() ).size() != polygon.size() )
		return std::nullopt;
	for ( std::size_t k = 0; k < polygon.size(); k++ )
		if ( Turn( points[ polygon[ ( k + polygon.size() - 1 ) % polygon.size() ] ], points[ polygon[ k ] ], points[ polygon[ ( k + 1 ) % polygon.size() ] ], vecNormal ) < -1e-9 )
			return std::nullopt;
	return polygon;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Builds prisms directly from matching mesh caps, allowing disconnected pieces and holes.
//-----------------------------------------------------------------------------
std::optional<std::vector<Convex_t>> ExtractCutoutHulls( const CModel &model, const VmfOptions_t &options, std::string *pError )
{
	const auto pfnReject = [ pError ]( const char *szReason ) -> std::optional<std::vector<Convex_t>> { if ( pError ) *pError = szReason; return std::nullopt; };
	if ( options.m_nMaxSides < 5 || options.m_nMaxSides > 128 )
		return pfnReject( "Cutout prisms need at least five brush sides" );
	std::vector<std::array<Point_t, 3>> triangles;
	std::vector<Point_t> vertices;
	Point_t vecNormal{};
	double flLargest = 0;
	for ( const auto &mesh : model.m_Meshes )
	{
		if ( mesh.m_Indices.size() % 3 )
			return pfnReject( "Invalid cutout triangle list" );
		for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
		{
			std::array<Point_t, 3> triangle;
			for ( int j = 0; j < 3; j++ )
			{
				if ( mesh.m_Indices[ i + j ] >= model.m_Vertices.size() )
					return pfnReject( "Invalid cutout vertex index" );
				const auto p = model.m_Vertices[ mesh.m_Indices[ i + j ] ].m_vecPosition;
				triangle[ j ] = { p.m_flX, p.m_flY, p.m_flZ };
				for ( double flValue : triangle[ j ] )
					if ( !std::isfinite( flValue ) || std::abs( flValue ) >= 16384 )
						return pfnReject( "Cutout coordinates exceed export bounds" );
				vertices.push_back( triangle[ j ] );
			}
			const auto vecArea = CrossPoint( Difference( triangle[ 1 ], triangle[ 0 ] ), Difference( triangle[ 2 ], triangle[ 0 ] ) );
			const double flArea = DotPoint( vecArea, vecArea );
			if ( flArea < 1e-16 )
				return pfnReject( "Cutout contains a zero-area triangle" );
			if ( flArea > flLargest )
			{
				flLargest = flArea;
				vecNormal = vecArea;
			}
			triangles.push_back( triangle );
			if ( triangles.size() > 10000 )
				return pfnReject( "Cutout exceeds the 10,000-triangle limit" );
		}
	}
	if ( triangles.size() < 8 || flLargest == 0 )
		return pfnReject( "Cutout needs two matching caps and existing thickness" );
	for ( auto &flComponent : vecNormal )
		flComponent /= std::sqrt( flLargest );
	// Exported cutout meshes often carry a few float ULPs of noise along their extrusion axis.
	for ( auto &flComponent : vecNormal )
		if ( std::abs( flComponent ) < 1e-6 )
			flComponent = 0;
	const double flLength = std::sqrt( DotPoint( vecNormal, vecNormal ) );
	for ( auto &flComponent : vecNormal )
		flComponent /= flLength;
	const auto nAxis = static_cast<int>( std::max_element( vecNormal.begin(), vecNormal.end(), []( double a, double b ) { return std::abs( a ) < std::abs( b ); } ) - vecNormal.begin() );
	if ( vecNormal[ nAxis ] < 0 )
		for ( auto &flComponent : vecNormal )
			flComponent = -flComponent;
	double flMin = std::numeric_limits<double>::infinity(), flMax = -flMin;
	for ( auto p : vertices )
	{
		const double flDistance = DotPoint( p, vecNormal );
		flMin = std::min( flMin, flDistance );
		flMax = std::max( flMax, flDistance );
	}
	const double flThickness = flMax - flMin, flTolerance = 0.0001;
	if ( flThickness < 0.125 )
		return pfnReject( "Cutout has no usable thickness; flat image cards need silhouette tracing" );
	for ( auto p : vertices )
	{
		const double flDistance = DotPoint( p, vecNormal );
		if ( std::min( std::abs( flDistance - flMin ), std::abs( flDistance - flMax ) ) > flTolerance )
			return pfnReject( "Mesh is not an extrusion between two parallel caps" );
	}
	std::vector<Point_t> points;
	std::map<std::array<long long, 3>, int> welded;
	std::set<std::array<int, 3>> caps[ 2 ];
	std::map<std::pair<int, int>, int> boundaries[ 2 ];
	std::vector<Polygon_t> polygons;
	std::vector<std::array<int, 3>> sideProjections;
	for ( const auto &triangle : triangles )
	{
		std::array<int, 3> indices;
		int nCap = -1;
		for ( int j = 0; j < 3; j++ )
		{
			const auto p = triangle[ j ];
			const double flDistance = DotPoint( p, vecNormal );
			const int nLayer = std::abs( flDistance - flMin ) <= flTolerance ? 0 : 1;
			if ( j == 0 )
				nCap = nLayer;
			else if ( nCap != nLayer )
				nCap = 2;
			Point_t projected;
			std::array<long long, 3> key;
			for ( int k = 0; k < 3; k++ )
			{
				projected[ k ] = p[ k ] - vecNormal[ k ] * ( flDistance - flMin );
				key[ k ] = std::llround( projected[ k ] / flTolerance );
			}
			const auto [ it, bNew ] = welded.emplace( key, static_cast<int>( points.size() ) );
			if ( bNew )
				points.push_back( projected );
			indices[ j ] = it->second;
		}
		if ( nCap == 2 )
		{
			sideProjections.push_back( indices );
			continue;
		}
		auto sorted = indices;
		std::sort( sorted.begin(), sorted.end() );
		if ( sorted[ 0 ] == sorted[ 1 ] || sorted[ 1 ] == sorted[ 2 ] )
			return pfnReject( "Cutout cap collapses at the geometry tolerance" );
		if ( !caps[ nCap ].insert( sorted ).second )
			continue;
		if ( Turn( points[ indices[ 0 ] ], points[ indices[ 1 ] ], points[ indices[ 2 ] ], vecNormal ) < 0 )
			std::swap( indices[ 1 ], indices[ 2 ] );
		for ( int i = 0; i < 3; i++ )
		{
			const int a = indices[ i ], b = indices[ ( i + 1 ) % 3 ];
			boundaries[ nCap ][ std::minmax( a, b ) ] += a < b ? 1 : -1;
		}
		if ( nCap == 0 )
		{
			polygons.emplace_back( indices.begin(), indices.end() );
		}
	}
	// Internal diagonals cancel, so the caps may use different triangulations of the same shape.
	for ( auto &boundary : boundaries )
		std::erase_if( boundary, []( const auto &entry ) { return entry.second == 0; } );
	if ( caps[ 0 ].empty() || caps[ 1 ].empty() || boundaries[ 0 ] != boundaries[ 1 ] )
		return pfnReject( "Cutout front and back outlines do not match" );
	if ( sideProjections.empty() )
		return pfnReject( "Cutout has no side faces" );
	std::set<std::pair<int, int>> capEdges;
	for ( const auto &triangle : caps[ 0 ] )
		for ( int i = 0; i < 3; i++ )
			capEdges.insert( std::minmax( triangle[ i ], triangle[ ( i + 1 ) % 3 ] ) );
	for ( const auto &triangle : sideProjections )
	{
		const std::set<int> unique( triangle.begin(), triangle.end() );
		if ( unique.size() != 2 || !capEdges.contains( std::minmax( *unique.begin(), *unique.rbegin() ) ) )
			return pfnReject( "Cutout side faces do not follow its cap edges" );
	}
	// Each merge removes a shared diagonal, never a silhouette edge or a hole boundary.
	std::uint64_t nWork = 0;
	for ( ;; )
	{
		using Edge_t = std::pair<int, int>;
		std::map<Edge_t, std::vector<std::pair<std::size_t, std::size_t>>> edges;
		for ( std::size_t i = 0; i < polygons.size(); i++ )
			for ( std::size_t j = 0; j < polygons[ i ].size(); j++ )
			{
				if ( ++nWork > 100000000 )
					return pfnReject( "Cutout exceeds the convex merging work limit" );
				edges[ { polygons[ i ][ j ], polygons[ i ][ ( j + 1 ) % polygons[ i ].size() ] } ].push_back( { i, j } );
			}
		bool bMerged = false;
		for ( const auto &[ edge, entries ] : edges )
		{
			const auto found = edges.find( { edge.second, edge.first } );
			if ( found == edges.end() )
				continue;
			for ( const auto &[ a, i ] : entries )
			{
				for ( const auto &[ b, j ] : found->second )
				{
					if ( ++nWork > 100000000 )
						return pfnReject( "Cutout exceeds the convex merging work limit" );
					if ( a >= b )
						continue;
					if ( auto merged = Merge( polygons[ a ], i, polygons[ b ], j, points, vecNormal, options.m_nMaxSides ) )
					{
						polygons[ a ] = std::move( *merged );
						polygons[ b ].clear();
						bMerged = true;
						break;
					}
				}
				if ( bMerged )
					break;
			}
			if ( bMerged )
				break;
		}
		if ( !bMerged )
			break;
	}
	std::vector<Convex_t> hulls;
	for ( const auto &polygon : polygons )
	{
		if ( polygon.empty() )
			continue;
		Convex_t hull;
		for ( int nLayer = 0; nLayer < 2; nLayer++ )
			for ( int nPoint : polygon )
			{
				auto p = points[ nPoint ];
				for ( int k = 0; k < 3; k++ )
					p[ k ] += vecNormal[ k ] * ( flThickness * nLayer );
				hull.m_Vertices.push_back( { static_cast<float>( p[ 0 ] ), static_cast<float>( p[ 1 ] ), static_cast<float>( p[ 2 ] ) } );
			}
		const auto nCount = static_cast<std::uint32_t>( polygon.size() );
		for ( std::uint32_t i = 1; i + 1 < nCount; i++ )
			hull.m_Indices.insert( hull.m_Indices.end(), { 0, i + 1, i, nCount, nCount + i, nCount + i + 1 } );
		for ( std::uint32_t i = 0; i < nCount; i++ )
		{
			const auto j = ( i + 1 ) % nCount;
			hull.m_Indices.insert( hull.m_Indices.end(), { i, j, nCount + j, i, nCount + j, nCount + i } );
		}
		hulls.push_back( std::move( hull ) );
	}
	return hulls;
}
} // namespace xv::model
