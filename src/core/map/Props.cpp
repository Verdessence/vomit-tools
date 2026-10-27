//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Props placed in the map: which models the entities show, and their triangles in
//          world space.
//
//===========================================================================//
#include "map/Props.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

#include "map/Scene.h"

namespace xv::map
{

namespace
{

struct PropTriangle_t
{
	std::uint64_t m_nKey = 0; // material, then chunk
	std::array<std::uint32_t, 3> m_Indices{};
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool EndsWithMdl( std::string_view szPath )
{
	if ( szPath.size() < 4 )
	{
		return false;
	}
	std::string szEnd{ szPath.substr( szPath.size() - 4 ) };
	std::ranges::transform( szEnd, szEnd.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return szEnd == ".mdl";
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PropList_t ListProps( const CScene &scene )
{
	PropList_t list;
	std::map<PropKey_t, std::uint32_t> known;
	for ( std::size_t e = 0; e < scene.m_Entities.size(); e++ )
	{
		const EntityVisual_t &visual = scene.m_Entities[ e ];
		if ( !visual.m_bPoint || !EndsWithMdl( visual.m_szModel ) )
		{
			continue;
		}
		// The game's file system ignores case; one load serves every spelling.
		PropKey_t key{ visual.m_szModel, visual.m_nSkin };
		std::ranges::transform( key.m_szModel, key.m_szModel.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
		const auto [ it, bAdded ] = known.try_emplace( key, static_cast<std::uint32_t>( list.m_Keys.size() ) );
		if ( bAdded )
		{
			list.m_Keys.push_back( key );
		}
		list.m_Entities.emplace_back( scene.EntityObject( e ), it->second );
	}
	return list;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapMesh BuildPropMesh( std::span<const PropModel_t> models, std::span<const PropPlacement_t> placements, double flChunkSize )
{
	CMapMesh mesh;
	std::vector<PropTriangle_t> triangles;
	std::map<std::array<std::int64_t, 3>, std::uint32_t> chunkOf;
	std::vector<std::uint32_t> remap;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
	for ( const PropPlacement_t &placement : placements )
	{
		if ( placement.m_nModel >= models.size() || !models[ placement.m_nModel ].m_pModel )
		{
			continue;
		}
		const PropModel_t &prop = models[ placement.m_nModel ];
		const model::CModel &model = *prop.m_pModel;
		const Transform_t &place = placement.m_Transform;
		std::array<std::int64_t, 3> cell{};
		for ( std::size_t a = 0; a < 3; a++ )
		{
			cell[ a ] = static_cast<std::int64_t>( std::floor( place.m_Offset[ a ] / flChunkSize ) );
		}
		const auto [ chunk, bNewChunk ] = chunkOf.try_emplace( cell, static_cast<std::uint32_t>( mesh.m_Chunks.size() ) );
		if ( bNewChunk )
		{
			mesh.m_Chunks.emplace_back();
		}
		const std::uint32_t nChunk = chunk->second;

		remap.assign( model.m_Vertices.size(), ~0u );
		for ( const model::Mesh_t &part : model.m_Meshes )
		{
			if ( part.m_nMaterial < 0 || static_cast<std::size_t>( part.m_nMaterial ) >= prop.m_Materials.size() )
			{
				continue;
			}
			const std::uint32_t nMaterial = prop.m_Materials[ static_cast<std::size_t>( part.m_nMaterial ) ];
			const std::array<int, 2> size = prop.m_TextureSizes[ static_cast<std::size_t>( part.m_nMaterial ) ];
			// A vertex is placed once per part: parts with other materials take other texel sizes.
			std::vector<std::uint32_t> placed;
			edges.clear();
			for ( std::size_t i = 0; i + 2 < part.m_Indices.size(); i += 3 )
			{
				std::array<std::uint32_t, 3> corners{};
				bool bValid = true;
				for ( std::size_t k = 0; k < 3; k++ )
				{
					const std::uint32_t nVertex = part.m_Indices[ i + k ];
					if ( nVertex >= model.m_Vertices.size() )
					{
						bValid = false;
						break;
					}
					if ( remap[ nVertex ] == ~0u )
					{
						const model::Vertex_t &source = model.m_Vertices[ nVertex ];
						MeshVertex_t vertex;
						vertex.m_vecPosition = ToVec3( place.Apply( ToPoint( source.m_vecPosition ) ) );
						vertex.m_vecNormal = ToVec3( Normalized( place.Rotate( ToPoint( source.m_vecNormal ) ) ) );
						vertex.m_Texel = { source.m_Uv[ 0 ] * static_cast<float>( size[ 0 ] ), source.m_Uv[ 1 ] * static_cast<float>( size[ 1 ] ) };
						vertex.m_nObject = placement.m_nObject;
						remap[ nVertex ] = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
						placed.push_back( nVertex );
						mesh.m_Chunks[ nChunk ].m_Bounds.Add( Point_t{ vertex.m_vecPosition.m_flX, vertex.m_vecPosition.m_flY, vertex.m_vecPosition.m_flZ } );
						mesh.m_Vertices.push_back( vertex );
					}
					corners[ k ] = remap[ nVertex ];
				}
				if ( !bValid )
				{
					continue;
				}
				triangles.push_back( { ( static_cast<std::uint64_t>( nMaterial ) << 32 ) | nChunk, corners } );
				for ( std::size_t k = 0; k < 3; k++ )
				{
					edges.emplace_back( std::min( corners[ k ], corners[ ( k + 1 ) % 3 ] ), std::max( corners[ k ], corners[ ( k + 1 ) % 3 ] ) );
				}
			}
			std::ranges::sort( edges );
			const auto [ first, last ] = std::ranges::unique( edges );
			edges.erase( first, last );
			for ( const auto &[ nA, nB ] : edges )
			{
				mesh.m_Lines.insert( mesh.m_Lines.end(), { nA, nB } );
			}
			for ( const std::uint32_t nVertex : placed )
			{
				remap[ nVertex ] = ~0u;
			}
		}
	}
	std::ranges::stable_sort( triangles, {}, &PropTriangle_t::m_nKey );
	mesh.m_Indices.reserve( triangles.size() * 3 );
	for ( const PropTriangle_t &triangle : triangles )
	{
		const auto nMaterial = static_cast<std::uint32_t>( triangle.m_nKey >> 32 );
		const auto nChunk = static_cast<std::uint32_t>( triangle.m_nKey & 0xFFFFFFFFu );
		if ( mesh.m_Batches.empty() || mesh.m_Batches.back().m_nMaterial != nMaterial || mesh.m_Batches.back().m_nChunk != nChunk )
		{
			mesh.m_Batches.push_back( { nMaterial, nChunk, static_cast<std::uint32_t>( mesh.m_Indices.size() ), 0 } );
		}
		mesh.m_Indices.insert( mesh.m_Indices.end(), triangle.m_Indices.begin(), triangle.m_Indices.end() );
		mesh.m_Batches.back().m_nIndexCount += 3;
	}
	return mesh;
}

} // namespace xv::map
