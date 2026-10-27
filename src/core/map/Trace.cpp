//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Trace.h. The mesh's areas (1024-unit chunks) are tested first, so a ray only
//          looks at the triangles of the areas it passes through.
//
//===========================================================================//
#include "Trace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Where the ray enters the box, if it does within flMax.
//-----------------------------------------------------------------------------
bool HitsBox( const Box_t &box, Point_t from, Point_t dir, double flMax )
{
	double flNear = 0.0;
	double flFar = flMax;
	for ( std::size_t a = 0; a < 3; a++ )
	{
		if ( std::abs( dir[ a ] ) < 1e-12 )
		{
			if ( from[ a ] < box.m_Mins[ a ] || from[ a ] > box.m_Maxs[ a ] )
			{
				return false;
			}
			continue;
		}
		double t0 = ( box.m_Mins[ a ] - from[ a ] ) / dir[ a ];
		double t1 = ( box.m_Maxs[ a ] - from[ a ] ) / dir[ a ];
		if ( t0 > t1 )
		{
			std::swap( t0, t1 );
		}
		flNear = std::max( flNear, t0 );
		flFar = std::min( flFar, t1 );
		if ( flNear > flFar )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The ray's distance to a triangle (Möller and Trumbore's test), both sides counting.
//-----------------------------------------------------------------------------
std::optional<double> HitsTriangle( Point_t a, Point_t b, Point_t c, Point_t from, Point_t dir )
{
	const Point_t e1 = Subtract( b, a );
	const Point_t e2 = Subtract( c, a );
	const Point_t p = Cross( dir, e2 );
	const double flDet = Dot( e1, p );
	if ( std::abs( flDet ) < 1e-12 )
	{
		return std::nullopt;
	}
	const double flInv = 1.0 / flDet;
	const Point_t s = Subtract( from, a );
	const double u = Dot( s, p ) * flInv;
	if ( u < 0.0 || u > 1.0 )
	{
		return std::nullopt;
	}
	const Point_t q = Cross( s, e1 );
	const double v = Dot( dir, q ) * flInv;
	if ( v < 0.0 || u + v > 1.0 )
	{
		return std::nullopt;
	}
	const double t = Dot( e2, q ) * flInv;
	return t > 0.0 ? std::optional<double>{ t } : std::nullopt;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<TraceHit_t> TraceScene( const RasterScene_t &scene, bool bToolTextures, Point_t from, Point_t dir, double flMax )
{
	std::unordered_map<std::uint32_t, TraceHit_t> nearest;
	for ( std::uint32_t nMesh = 0; nMesh < scene.m_Meshes.size(); nMesh++ )
	{
		const CMapMesh &mesh = *scene.m_Meshes[ nMesh ];
		std::vector<std::uint8_t> chunkHit( mesh.m_Chunks.size(), 2 ); // 2: not tested yet
		for ( const MeshBatch_t &batch : mesh.m_Batches )
		{
			if ( batch.m_nChunk < mesh.m_Chunks.size() )
			{
				std::uint8_t &nHit = chunkHit[ batch.m_nChunk ];
				if ( nHit == 2 )
				{
					nHit = HitsBox( mesh.m_Chunks[ batch.m_nChunk ].m_Bounds, from, dir, flMax ) ? 1 : 0;
				}
				if ( nHit == 0 )
				{
					continue;
				}
			}
			if ( !bToolTextures && batch.m_nMaterial < scene.m_Materials.size() && scene.m_Materials[ batch.m_nMaterial ].m_bTool )
			{
				continue;
			}
			for ( std::uint32_t i = batch.m_nFirstIndex; i < batch.m_nFirstIndex + batch.m_nIndexCount; i += 3 )
			{
				const MeshVertex_t &a = mesh.m_Vertices[ mesh.m_Indices[ i ] ];
				if ( a.m_nObject < scene.m_ObjectFlags.size() && !IsDrawn( scene, &mesh, scene.m_ObjectFlags[ a.m_nObject ] ) )
				{
					continue;
				}
				const auto t = HitsTriangle( ToPoint( a.m_vecPosition ), ToPoint( mesh.m_Vertices[ mesh.m_Indices[ i + 1 ] ].m_vecPosition ), ToPoint( mesh.m_Vertices[ mesh.m_Indices[ i + 2 ] ].m_vecPosition ), from, dir );
				if ( !t || *t > flMax )
				{
					continue;
				}
				auto [ it, bNew ] = nearest.try_emplace( a.m_nObject, TraceHit_t{ a.m_nObject, nMesh, i / 3, *t } );
				if ( !bNew && *t < it->second.m_flDistance )
				{
					it->second = TraceHit_t{ a.m_nObject, nMesh, i / 3, *t };
				}
			}
		}
	}
	std::vector<TraceHit_t> hits;
	hits.reserve( nearest.size() );
	for ( const auto &[ nObject, hit ] : nearest )
	{
		hits.push_back( hit );
	}
	std::ranges::sort( hits, {}, &TraceHit_t::m_flDistance );
	return hits;
}

} // namespace xv::map
