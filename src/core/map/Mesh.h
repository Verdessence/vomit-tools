//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map's solids as triangles ready to draw: one vertex list, triangles grouped by
//          material and by area, so a renderer draws each material in few calls and skips
//          areas out of sight. Texture positions are kept in texels, so the mesh can be built
//          before the textures (and their sizes) arrive.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "map/Brushes.h"
#include "map/Map.h"
#include "util/Vec3.h"

namespace xv::map
{

struct MeshVertex_t
{
	CVec3 m_vecPosition;
	CVec3 m_vecNormal;
	std::array<float, 2> m_Texel{}; // texture position in texels; divide by the texture's size
	std::array<float, 2> m_Luxel{}; // lightmap position in luxels (the side's lightmap scale)
	float m_flAlpha = 0.0f;         // displacements: how much of the second texture shows
	std::uint32_t m_nObject = 0;    // the solid it belongs to
};

// The triangles of one material in one area.
struct MeshBatch_t
{
	std::uint32_t m_nMaterial = 0;
	std::uint32_t m_nChunk = 0;
	std::uint32_t m_nFirstIndex = 0;
	std::uint32_t m_nIndexCount = 0;
};

struct MeshChunk_t
{
	Box_t m_Bounds;
};

// A side as drawn: its vertices, and whether a displacement replaced its polygon.
struct MeshFace_t
{
	std::uint32_t m_nSolid = 0;
	std::uint32_t m_nSide = 0;
	std::uint32_t m_nMaterial = 0;
	std::uint32_t m_nFirstVertex = 0;
	std::uint32_t m_nVertexCount = 0;
	bool m_bDisplacement = false;
};

struct MeshRange_t
{
	std::uint32_t m_nFirst = 0;
	std::uint32_t m_nCount = 0;
};

class CMapMesh
{
public:
	std::vector<MeshVertex_t> m_Vertices;
	std::vector<std::uint32_t> m_Indices;       // triangles, by batch
	std::vector<std::uint32_t> m_TriangleFaces; // per triangle in m_Indices: its face
	std::vector<MeshFace_t> m_Faces;            // solid by solid
	std::vector<MeshBatch_t> m_Batches;         // by material, then chunk
	std::vector<MeshChunk_t> m_Chunks;
	std::vector<std::uint32_t> m_Lines;         // edges as vertex pairs, solid by solid
	std::vector<MeshRange_t> m_SolidLines;      // per solid: its pairs in m_Lines
	std::vector<MeshRange_t> m_SolidFaces;      // per solid: its faces in m_Faces
	std::vector<std::string> m_Problems;        // displacements drawn flat, and why
};

struct MeshOptions_t
{
	double m_flChunkSize = 1024.0;
};

// solids lines up with map.m_Solids (see BuildSolids).
[[nodiscard]] CMapMesh BuildMesh( const CMap &map, std::span<const SolidGeometry_t> solids, const MeshOptions_t &options = {} );

} // namespace xv::map
