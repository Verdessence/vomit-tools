//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Mesh.h.
//
//===========================================================================//
#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <unordered_map>

#include "map/Displacement.h"
#include "util/Parallel.h"

namespace xv::map
{

namespace
{

// One solid's share, built on its own so solids can be built in parallel.
struct SolidPart_t
{
	std::vector<MeshVertex_t> m_Vertices;
	std::vector<std::array<std::uint32_t, 3>> m_Triangles; // into m_Vertices
	std::vector<std::uint32_t> m_TriangleFaces;            // into m_Faces
	std::vector<MeshFace_t> m_Faces;                       // vertices relative to the part
	std::vector<Point_t> m_Centres;                        // per face, for choosing its chunk
	std::vector<std::uint32_t> m_Lines;
	std::vector<std::string> m_Problems;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MeshVertex_t MakeVertex( Point_t position, Point_t normal, Point_t textured, const Side_t &side, float flAlpha, std::uint32_t nObject )
{
	MeshVertex_t vertex;
	vertex.m_vecPosition = ToVec3( position );
	vertex.m_vecNormal = ToVec3( normal );
	const std::array<double, 2> texel = TexelCoord( textured, side.m_UAxis, side.m_VAxis );
	vertex.m_Texel = { static_cast<float>( texel[ 0 ] ), static_cast<float>( texel[ 1 ] ) };
	vertex.m_flAlpha = flAlpha;
	vertex.m_nObject = nObject;
	return vertex;
}

//-----------------------------------------------------------------------------
// Purpose: A solid's faces: polygons fanned into triangles, or displacement grids.
//-----------------------------------------------------------------------------
SolidPart_t BuildPart( const Solid_t &solid, const SolidGeometry_t &geometry, std::uint32_t nSolid )
{
	SolidPart_t part;
	for ( const FaceGeometry_t &face : geometry.m_Faces )
	{
		const Side_t &side = solid.m_Sides[ face.m_nSide ];
		MeshFace_t meshFace;
		meshFace.m_nSolid = nSolid;
		meshFace.m_nSide = face.m_nSide;
		meshFace.m_nMaterial = side.m_nMaterial;
		meshFace.m_nFirstVertex = static_cast<std::uint32_t>( part.m_Vertices.size() );
		const auto nFace = static_cast<std::uint32_t>( part.m_Faces.size() );
		const auto nBase = static_cast<std::uint32_t>( part.m_Vertices.size() );
		Point_t centre{};

		std::optional<DispMesh_t> disp;
		if ( side.m_pDisp )
		{
			std::string szReason;
			disp = BuildDisplacement( *side.m_pDisp, face, &szReason );
			if ( !disp )
			{
				part.m_Problems.push_back( "side " + std::to_string( side.m_nId ) + ": " + szReason );
			}
		}
		if ( disp )
		{
			meshFace.m_bDisplacement = true;
			for ( std::size_t i = 0; i < disp->m_Points.size(); i++ )
			{
				part.m_Vertices.push_back( MakeVertex( disp->m_Points[ i ], disp->m_Normals[ i ], disp->m_Flat[ i ], side, disp->m_Alphas[ i ], nSolid ) );
				centre = Add( centre, Scale( disp->m_Points[ i ], 1.0 / static_cast<double>( disp->m_Points.size() ) ) );
			}
			std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
			for ( std::size_t i = 0; i < disp->m_Indices.size(); i += 3 )
			{
				const std::array<std::uint32_t, 3> triangle = { nBase + disp->m_Indices[ i ], nBase + disp->m_Indices[ i + 1 ], nBase + disp->m_Indices[ i + 2 ] };
				part.m_Triangles.push_back( triangle );
				part.m_TriangleFaces.push_back( nFace );
				for ( std::size_t k = 0; k < 3; k++ )
				{
					const std::uint32_t a = triangle[ k ];
					const std::uint32_t b = triangle[ ( k + 1 ) % 3 ];
					edges.insert( { std::min( a, b ), std::max( a, b ) } );
				}
			}
			for ( const auto &[ a, b ] : edges )
			{
				part.m_Lines.insert( part.m_Lines.end(), { a, b } );
			}
		}
		else
		{
			for ( const Point_t &p : face.m_Points )
			{
				part.m_Vertices.push_back( MakeVertex( p, face.m_Plane.m_Normal, p, side, 0.0f, nSolid ) );
				centre = Add( centre, Scale( p, 1.0 / static_cast<double>( face.m_Points.size() ) ) );
			}
			const auto nCount = static_cast<std::uint32_t>( face.m_Points.size() );
			for ( std::uint32_t i = 1; i + 1 < nCount; i++ )
			{
				part.m_Triangles.push_back( { nBase, nBase + i, nBase + i + 1 } );
				part.m_TriangleFaces.push_back( nFace );
			}
			for ( std::uint32_t i = 0; i < nCount; i++ )
			{
				part.m_Lines.insert( part.m_Lines.end(), { nBase + i, nBase + ( i + 1 ) % nCount } );
			}
		}
		meshFace.m_nVertexCount = static_cast<std::uint32_t>( part.m_Vertices.size() ) - meshFace.m_nFirstVertex;
		part.m_Faces.push_back( meshFace );
		part.m_Centres.push_back( centre );
	}
	return part;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapMesh BuildMesh( const CMap &map, std::span<const SolidGeometry_t> solids, const MeshOptions_t &options )
{
	const std::size_t nSolids = std::min( map.m_Solids.size(), solids.size() );
	std::vector<SolidPart_t> parts( nSolids );
	ParallelFor( nSolids, 128, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t i = nBegin; i < nEnd; i++ )
		{
			parts[ i ] = BuildPart( map.m_Solids[ i ], solids[ i ], static_cast<std::uint32_t>( i ) );
		}
	} );

	CMapMesh mesh;
	std::size_t nVertices = 0;
	std::size_t nTriangles = 0;
	std::size_t nLines = 0;
	std::size_t nFaces = 0;
	for ( const SolidPart_t &part : parts )
	{
		nVertices += part.m_Vertices.size();
		nTriangles += part.m_Triangles.size();
		nLines += part.m_Lines.size();
		nFaces += part.m_Faces.size();
	}
	mesh.m_Vertices.reserve( nVertices );
	mesh.m_Lines.reserve( nLines );
	mesh.m_Faces.reserve( nFaces );
	mesh.m_SolidLines.resize( nSolids );
	mesh.m_SolidFaces.resize( nSolids );

	// Each face goes to the area its centre is in.
	std::unordered_map<std::uint64_t, std::uint32_t> chunkOf;
	struct Triangle_t
	{
		std::uint64_t m_nKey; // material, then chunk
		std::array<std::uint32_t, 3> m_Corners;
		std::uint32_t m_nFace;
	};
	std::vector<Triangle_t> triangles;
	triangles.reserve( nTriangles );
	for ( std::size_t i = 0; i < nSolids; i++ )
	{
		SolidPart_t &part = parts[ i ];
		const auto nVertexBase = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
		const auto nFaceBase = static_cast<std::uint32_t>( mesh.m_Faces.size() );
		std::vector<std::uint32_t> faceChunks( part.m_Faces.size() );
		for ( std::size_t f = 0; f < part.m_Faces.size(); f++ )
		{
			std::uint64_t nCell = 0;
			for ( std::size_t a = 0; a < 3; a++ )
			{
				const double flCell = std::floor( part.m_Centres[ f ][ a ] / options.m_flChunkSize );
				nCell = ( nCell << 21 ) | ( static_cast<std::uint64_t>( static_cast<std::int64_t>( std::clamp( flCell, -1048576.0, 1048575.0 ) ) + 1048576 ) & 0x1FFFFF );
			}
			auto [ it, bAdded ] = chunkOf.try_emplace( nCell, static_cast<std::uint32_t>( mesh.m_Chunks.size() ) );
			if ( bAdded )
			{
				mesh.m_Chunks.emplace_back();
			}
			faceChunks[ f ] = it->second;
			MeshFace_t face = part.m_Faces[ f ];
			face.m_nFirstVertex += nVertexBase;
			mesh.m_Faces.push_back( face );
			for ( std::uint32_t v = 0; v < face.m_nVertexCount; v++ )
			{
				mesh.m_Chunks[ it->second ].m_Bounds.Add( ToPoint( part.m_Vertices[ face.m_nFirstVertex - nVertexBase + v ].m_vecPosition ) );
			}
		}
		for ( std::size_t t = 0; t < part.m_Triangles.size(); t++ )
		{
			const std::uint32_t nFace = part.m_TriangleFaces[ t ];
			const std::uint64_t nKey = ( static_cast<std::uint64_t>( part.m_Faces[ nFace ].m_nMaterial ) << 32 ) | faceChunks[ nFace ];
			const auto &corners = part.m_Triangles[ t ];
			triangles.push_back( { nKey, { nVertexBase + corners[ 0 ], nVertexBase + corners[ 1 ], nVertexBase + corners[ 2 ] }, nFaceBase + nFace } );
		}
		mesh.m_SolidLines[ i ] = { static_cast<std::uint32_t>( mesh.m_Lines.size() / 2 ), static_cast<std::uint32_t>( part.m_Lines.size() / 2 ) };
		for ( const std::uint32_t nIndex : part.m_Lines )
		{
			mesh.m_Lines.push_back( nVertexBase + nIndex );
		}
		mesh.m_SolidFaces[ i ] = { nFaceBase, static_cast<std::uint32_t>( part.m_Faces.size() ) };
		mesh.m_Vertices.insert( mesh.m_Vertices.end(), part.m_Vertices.begin(), part.m_Vertices.end() );
		for ( std::string &szProblem : part.m_Problems )
		{
			mesh.m_Problems.push_back( std::move( szProblem ) );
		}
		part = {};
	}

	std::ranges::stable_sort( triangles, {}, &Triangle_t::m_nKey );
	mesh.m_Indices.reserve( triangles.size() * 3 );
	mesh.m_TriangleFaces.reserve( triangles.size() );
	for ( const Triangle_t &triangle : triangles )
	{
		const auto nMaterial = static_cast<std::uint32_t>( triangle.m_nKey >> 32 );
		const auto nChunk = static_cast<std::uint32_t>( triangle.m_nKey & 0xFFFFFFFFu );
		if ( mesh.m_Batches.empty() || mesh.m_Batches.back().m_nMaterial != nMaterial || mesh.m_Batches.back().m_nChunk != nChunk )
		{
			mesh.m_Batches.push_back( { nMaterial, nChunk, static_cast<std::uint32_t>( mesh.m_Indices.size() ), 0 } );
		}
		mesh.m_Indices.insert( mesh.m_Indices.end(), triangle.m_Corners.begin(), triangle.m_Corners.end() );
		mesh.m_Batches.back().m_nIndexCount += 3;
		mesh.m_TriangleFaces.push_back( triangle.m_nFace );
	}
	return mesh;
}

} // namespace xv::map
