//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Vertex Tool's geometry.
//
//===========================================================================//
#include "Vertex.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace xv::map
{

namespace
{

constexpr double VERTEX_WELD = 1e-3; // corners this close are one

//-----------------------------------------------------------------------------
// Purpose: The index of p among the vertices, added when it is new.
//-----------------------------------------------------------------------------
std::uint32_t IndexOf( std::vector<Point_t> &vertices, const Point_t &p )
{
	for ( std::size_t i = 0; i < vertices.size(); i++ )
	{
		if ( Length( Subtract( vertices[ i ], p ) ) <= VERTEX_WELD )
		{
			return static_cast<std::uint32_t>( i );
		}
	}
	vertices.push_back( p );
	return static_cast<std::uint32_t>( vertices.size() - 1 );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t SolidVertices_t::EdgeMiddle( std::size_t nEdge ) const
{
	const auto &[ a, b ] = m_Edges[ nEdge ];
	return Scale( Add( m_Vertices[ a ], m_Vertices[ b ] ), 0.5 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
SolidVertices_t VerticesOf( const SolidGeometry_t &shape )
{
	SolidVertices_t out;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
	for ( const FaceGeometry_t &face : shape.m_Faces )
	{
		std::vector<std::uint32_t> corners;
		for ( const Point_t &p : face.m_Points )
		{
			const std::uint32_t n = IndexOf( out.m_Vertices, p );
			if ( corners.empty() || corners.back() != n )
			{
				corners.push_back( n );
			}
		}
		if ( corners.size() > 1 && corners.front() == corners.back() )
		{
			corners.pop_back();
		}
		for ( std::size_t i = 0; i < corners.size(); i++ )
		{
			const std::uint32_t a = corners[ i ];
			const std::uint32_t b = corners[ ( i + 1 ) % corners.size() ];
			if ( a != b )
			{
				edges.emplace_back( std::min( a, b ), std::max( a, b ) );
			}
		}
		out.m_FaceVertices.push_back( std::move( corners ) );
	}
	std::ranges::sort( edges );
	edges.erase( std::ranges::unique( edges ).begin(), edges.end() );
	out.m_Edges = std::move( edges );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: A face's plane from its moved corners: the normal by Newell's rule over the polygon
//          (so a slightly bent face still gets a sensible one), pointing the way the face did,
//          and the three points written the way a VMF wants them, (p0 - p1) x (p2 - p1) along it.
//-----------------------------------------------------------------------------
Solid_t MoveVertices( const Solid_t &solid, const SolidGeometry_t &shape, const std::vector<std::uint32_t> &vertices, const Point_t &offset )
{
	Solid_t out = solid;
	const SolidVertices_t handles = VerticesOf( shape );
	std::vector<Point_t> moved = handles.m_Vertices;
	for ( const std::uint32_t n : vertices )
	{
		if ( n < moved.size() )
		{
			moved[ n ] = Add( moved[ n ], offset );
		}
	}
	for ( std::size_t f = 0; f < shape.m_Faces.size(); f++ )
	{
		const FaceGeometry_t &face = shape.m_Faces[ f ];
		const std::vector<std::uint32_t> &corners = handles.m_FaceVertices[ f ];
		if ( face.m_nSide >= out.m_Sides.size() || corners.size() < 3 )
		{
			continue;
		}
		std::vector<Point_t> polygon;
		for ( const std::uint32_t n : corners )
		{
			polygon.push_back( moved[ n ] );
		}
		Point_t normal{};
		for ( std::size_t i = 0; i < polygon.size(); i++ )
		{
			const Point_t &a = polygon[ i ];
			const Point_t &b = polygon[ ( i + 1 ) % polygon.size() ];
			normal = Add( normal, Cross( a, b ) );
		}
		if ( Length( normal ) < 1e-9 )
		{
			continue; // the face collapsed; its plane stays
		}
		// Outward is the way the face pointed before the move (the winding alone doesn't say).
		if ( Dot( normal, face.m_Plane.m_Normal ) < 0.0 )
		{
			normal = Scale( normal, -1.0 );
		}
		// Three corners that aren't in a line, ordered along the normal.
		std::array<Point_t, 3> points{ polygon[ 0 ], polygon[ 1 ], polygon[ 2 ] };
		for ( std::size_t k = 3; k < polygon.size() && Length( Cross( Subtract( points[ 0 ], points[ 1 ] ), Subtract( points[ 2 ], points[ 1 ] ) ) ) < 1e-6; k++ )
		{
			points[ 2 ] = polygon[ k ];
		}
		if ( Dot( Cross( Subtract( points[ 0 ], points[ 1 ] ), Subtract( points[ 2 ], points[ 1 ] ) ), normal ) < 0.0 )
		{
			std::swap( points[ 0 ], points[ 2 ] );
		}
		Side_t &side = out.m_Sides[ face.m_nSide ];
		side.m_Plane = points;
		side.m_bPlaneRead = true;
		side.m_PlusVertices = std::move( polygon );
	}
	out.m_bChanged = true;
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: An edge moves both its corners; a corner named twice moves once.
//-----------------------------------------------------------------------------
MapEdit_t MoveHandles( const CScene &scene, const std::vector<VertexRef_t> &handles, const Point_t &offset )
{
	MapEdit_t edit;
	std::map<std::uint32_t, std::vector<std::uint32_t>> bySolid;
	for ( const VertexRef_t &handle : handles )
	{
		if ( handle.m_nObject >= scene.GetObjectCount() || !scene.IsSolidObject( handle.m_nObject ) || scene.IsDeleted( handle.m_nObject ) )
		{
			continue;
		}
		const std::uint32_t nSolid = scene.GetIndex( handle.m_nObject );
		const SolidVertices_t corners = VerticesOf( scene.m_Solids[ nSolid ] );
		std::vector<std::uint32_t> &list = bySolid[ nSolid ];
		if ( handle.m_bEdge )
		{
			if ( handle.m_nIndex < corners.m_Edges.size() )
			{
				list.push_back( corners.m_Edges[ handle.m_nIndex ].first );
				list.push_back( corners.m_Edges[ handle.m_nIndex ].second );
			}
		}
		else if ( handle.m_nIndex < corners.m_Vertices.size() )
		{
			list.push_back( handle.m_nIndex );
		}
	}
	for ( auto &[ nSolid, list ] : bySolid )
	{
		std::ranges::sort( list );
		list.erase( std::ranges::unique( list ).begin(), list.end() );
		if ( !list.empty() )
		{
			edit.m_Solids.emplace_back( nSolid, MoveVertices( scene.m_Map.m_Solids[ nSolid ], scene.m_Solids[ nSolid ], list, offset ) );
		}
	}
	return edit;
}

} // namespace xv::map
