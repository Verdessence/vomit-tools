//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Sprites as squares facing the camera.
//
//===========================================================================//
#include "map/Sprites.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace xv::map
{
namespace
{
// How big an area one batch of sprites covers: small enough that views and the 3D skybox's pass
// leave out what they can't see, big enough that a map's sprites are few draws.
constexpr double SPRITE_CELL = 1024.0;
} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapMesh BuildSpriteMesh( std::span<const SpritePlacement_t> sprites, Point_t right, Point_t up )
{
	CMapMesh mesh;
	// The camera looks along up x right; the squares face back the other way, towards it.
	const Point_t facing = Cross( right, up );
	std::vector<std::size_t> order( sprites.size() );
	for ( std::size_t i = 0; i < order.size(); i++ )
	{
		order[ i ] = i;
	}
	const auto cellOf = [ & ]( std::size_t i ) {
		const Point_t &at = sprites[ i ].m_Origin;
		return std::tuple{ sprites[ i ].m_nMaterial, std::floor( at[ 0 ] / SPRITE_CELL ), std::floor( at[ 1 ] / SPRITE_CELL ), std::floor( at[ 2 ] / SPRITE_CELL ) };
	};
	std::ranges::stable_sort( order, {}, cellOf );
	for ( std::size_t n = 0; n < order.size(); n++ )
	{
		const std::size_t i = order[ n ];
		const SpritePlacement_t &sprite = sprites[ i ];
		const Point_t r = Scale( right, sprite.m_flHalfWidth );
		const Point_t u = Scale( up, sprite.m_flHalfHeight );
		// Top left, top right, bottom right, bottom left: clockwise seen from the camera.
		const std::array<Point_t, 4> corners{ Add( Subtract( sprite.m_Origin, r ), u ), Add( Add( sprite.m_Origin, r ), u ), Subtract( Add( sprite.m_Origin, r ), u ), Subtract( Subtract( sprite.m_Origin, r ), u ) };
		const std::array<std::array<float, 2>, 4> texels{ { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } } };
		const auto nBase = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
		for ( std::size_t k = 0; k < 4; k++ )
		{
			MeshVertex_t vertex;
			vertex.m_vecPosition = ToVec3( corners[ k ] );
			vertex.m_vecNormal = ToVec3( facing );
			vertex.m_Texel = { texels[ k ][ 0 ] * static_cast<float>( sprite.m_TextureSize[ 0 ] ), texels[ k ][ 1 ] * static_cast<float>( sprite.m_TextureSize[ 1 ] ) };
			vertex.m_nObject = sprite.m_nObject;
			mesh.m_Vertices.push_back( vertex );
		}
		if ( n == 0 || cellOf( order[ n - 1 ] ) != cellOf( i ) )
		{
			mesh.m_Batches.push_back( { sprite.m_nMaterial, static_cast<std::uint32_t>( mesh.m_Chunks.size() ), static_cast<std::uint32_t>( mesh.m_Indices.size() ), 0 } );
			mesh.m_Chunks.emplace_back();
		}
		for ( const Point_t &corner : corners )
			mesh.m_Chunks.back().m_Bounds.Add( corner );
		mesh.m_Indices.insert( mesh.m_Indices.end(), { nBase, nBase + 1, nBase + 2, nBase, nBase + 2, nBase + 3 } );
		mesh.m_Batches.back().m_nIndexCount += 6;
		mesh.m_Lines.insert( mesh.m_Lines.end(), { nBase, nBase + 1, nBase + 1, nBase + 2, nBase + 2, nBase + 3, nBase + 3, nBase } );
	}
	return mesh;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapMesh BuildParticleMesh( std::span<const ParticleSprite_t> particles )
{
	CMapMesh mesh;
	mesh.m_Vertices.reserve( particles.size() * 4 );
	mesh.m_Indices.reserve( particles.size() * 6 );
	for ( const ParticleSprite_t &particle : particles )
	{
		const auto &r = particle.m_Rect;
		const std::array<std::array<float, 2>, 4> texels{ { { r[ 0 ], r[ 1 ] }, { r[ 2 ], r[ 1 ] }, { r[ 2 ], r[ 3 ] }, { r[ 0 ], r[ 3 ] } } };
		const auto nBase = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
		for ( std::size_t k = 0; k < 4; k++ )
		{
			MeshVertex_t vertex;
			vertex.m_vecPosition = particle.m_Corners[ k ];
			vertex.m_vecNormal = { particle.m_Color[ 0 ], particle.m_Color[ 1 ], particle.m_Color[ 2 ] };
			vertex.m_flAlpha = particle.m_Color[ 3 ];
			vertex.m_Texel = { texels[ k ][ 0 ] * static_cast<float>( particle.m_TextureSize[ 0 ] ), texels[ k ][ 1 ] * static_cast<float>( particle.m_TextureSize[ 1 ] ) };
			vertex.m_nObject = particle.m_nObject;
			mesh.m_Vertices.push_back( vertex );
		}
		if ( mesh.m_Batches.empty() || mesh.m_Batches.back().m_nMaterial != particle.m_nMaterial )
		{
			mesh.m_Batches.push_back( { particle.m_nMaterial, ~0u, static_cast<std::uint32_t>( mesh.m_Indices.size() ), 0 } );
		}
		mesh.m_Indices.insert( mesh.m_Indices.end(), { nBase, nBase + 1, nBase + 2, nBase, nBase + 2, nBase + 3 } );
		mesh.m_Batches.back().m_nIndexCount += 6;
	}
	return mesh;
}

} // namespace xv::map
