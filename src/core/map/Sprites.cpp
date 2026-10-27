//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Sprites as squares facing the camera.
//
//===========================================================================//
#include "map/Sprites.h"

#include <algorithm>

namespace xv::map
{

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
	std::ranges::stable_sort( order, {}, [ & ]( std::size_t i ) { return sprites[ i ].m_nMaterial; } );
	for ( const std::size_t i : order )
	{
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
		if ( mesh.m_Batches.empty() || mesh.m_Batches.back().m_nMaterial != sprite.m_nMaterial )
		{
			// Few enough to be drawn whenever their material is: no area of their own.
			mesh.m_Batches.push_back( { sprite.m_nMaterial, ~0u, static_cast<std::uint32_t>( mesh.m_Indices.size() ), 0 } );
		}
		mesh.m_Indices.insert( mesh.m_Indices.end(), { nBase, nBase + 1, nBase + 2, nBase, nBase + 2, nBase + 3 } );
		mesh.m_Batches.back().m_nIndexCount += 6;
		mesh.m_Lines.insert( mesh.m_Lines.end(), { nBase, nBase + 1, nBase + 1, nBase + 2, nBase + 2, nBase + 3, nBase + 3, nBase } );
	}
	return mesh;
}

} // namespace xv::map
