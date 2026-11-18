//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A compiled map's faces as the game draws them: each model's faces (the world first,
//          then one model per brush entity) as a mesh, displacements included, and the light
//          the compiler baked for them laid out in an atlas both map renderers sample. Nothing
//          is rebuilt from brushes, so what shows is what the compile made: its splits, its
//          merges, the faces it left out, its lightmaps.
//
//===========================================================================//
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "map/Geometry.h"
#include "map/Lighting.h"
#include "map/Mesh.h"
#include "util/Vec3.h"

namespace xv::bsp
{

// One model of the map: its faces where the compiler left them (a brush entity's are around
// its origin, the entity's own place adding to them), and how each vertex is lit.
struct CompiledModel_t
{
	std::shared_ptr<const map::CMapMesh> m_pMesh;                     // vertices' object: the model's number
	std::shared_ptr<const std::vector<map::LightVertex_t>> m_pLights; // per vertex of m_pMesh
	map::Box_t m_Bounds;                                              // as the model lump gives it
	int m_nFaces = 0;
	std::vector<std::int32_t> m_FaceNumbers; // each of m_pMesh's faces' number in the faces lump
};

// What the compiler worked out can be seen from where (the visibility lump): the map's leafs
// in clusters, each cluster's set of the clusters it may see, and the faces in each leaf.
class CVisibility
{
public:
	CVisibility( std::vector<std::byte> planes, std::vector<std::byte> nodes, std::vector<std::byte> leafs, const std::vector<std::byte> &leafFaces, std::vector<std::byte> visibility, int nHeadNode, std::size_t nFaces );
	// The map was vised: it has clusters and their sets.
	[[nodiscard]] bool IsUsable() const { return m_nClusters > 0; }
	[[nodiscard]] int GetClusterCount() const { return m_nClusters; }
	// The cluster of the leaf a point is in; -1 in solid or outside the map.
	[[nodiscard]] int ClusterAt( map::Point_t point ) const;
	// Each face of the faces lump, whether it can be seen from the cluster (all of them from
	// -1). Faces in no leaf (displacements) are not marked: the caller keeps those.
	[[nodiscard]] std::vector<bool> VisibleFaces( int nCluster ) const;

private:
	std::vector<std::byte> m_Planes, m_Nodes, m_Leafs, m_Visibility;
	int m_nHeadNode = 0;
	std::size_t m_nFaces = 0;
	int m_nClusters = 0;
	std::vector<std::vector<std::uint32_t>> m_ClusterFaces; // by cluster, sorted
};

// The light the compiler sampled in the map's open space (each leaf's ambient cubes), which
// the game lights models with.
class CAmbientLighting
{
public:
	// The light from each way, +x, -x, +y, -y, +z, -z: linear, over 255 as the lightmaps are.
	using Cube_t = std::array<CVec3, 6>;

	CAmbientLighting() = default;
	// The map's planes, nodes and leafs, and the compile's ambient index and samples.
	CAmbientLighting( std::vector<std::byte> planes, std::vector<std::byte> nodes, std::vector<std::byte> leafs, std::vector<std::byte> index, std::vector<std::byte> samples, int nHeadNode )
		: m_Planes( std::move( planes ) ),
		  m_Nodes( std::move( nodes ) ),
		  m_Leafs( std::move( leafs ) ),
		  m_Index( std::move( index ) ),
		  m_Samples( std::move( samples ) ),
		  m_nHeadNode( nHeadNode )
	{
	}

	// The cube nearest a point in the leaf it is in; nullopt in a leaf with none (solid).
	[[nodiscard]] std::optional<Cube_t> CubeAt( map::Point_t point ) const;
	[[nodiscard]] bool IsEmpty() const { return m_Samples.empty(); }
	// The light a cube gives a surface facing along normal (a unit vector): each way's share
	// by the square of the normal's part along it.
	[[nodiscard]] static CVec3 LightFor( const Cube_t &cube, CVec3 vecNormal );

private:
	std::vector<std::byte> m_Planes, m_Nodes, m_Leafs, m_Index, m_Samples;
	int m_nHeadNode = 0;
};

// The light the compiler baked on the world's surfaces, looked up at a point: on the face a
// point lies on (an overlay's), or on the first face below a point (where a model stands, the
// direct light the ambient cubes leave out).
class CSurfaceLight
{
public:
	CSurfaceLight( std::shared_ptr<const map::CMapMesh> pWorld, std::shared_ptr<const std::vector<map::LightVertex_t>> pLights, std::shared_ptr<const map::CLighting> pAtlas );

	// On a face the point lies on (within a couple of units), facing about along normal.
	[[nodiscard]] std::optional<CVec3> On( map::Point_t point, map::Point_t normal ) const;
	// On the first upward face under the point, at most flReach below it.
	[[nodiscard]] std::optional<CVec3> Below( map::Point_t point, double flReach = 1024.0 ) const;
	// Where a ray from a point along a direction first meets a lit face of the world, at most
	// flReach away (sky and unlit faces aren't met).
	[[nodiscard]] std::optional<map::Point_t> Hit( map::Point_t from, map::Point_t direction, double flReach ) const;

private:
	struct Triangle_t
	{
		std::array<std::uint32_t, 3> m_Corners;
		map::Point_t m_Normal;
		double m_flDistance = 0.0;
	};
	[[nodiscard]] std::optional<CVec3> LightAt( const Triangle_t &triangle, map::Point_t point ) const;
	[[nodiscard]] std::optional<std::array<double, 3>> Weights( const Triangle_t &triangle, map::Point_t point ) const;
	[[nodiscard]] static std::uint64_t Cell( int x, int y, int z );

	std::shared_ptr<const map::CMapMesh> m_pWorld;
	std::shared_ptr<const std::vector<map::LightVertex_t>> m_pLights;
	std::shared_ptr<const map::CLighting> m_pAtlas;
	std::vector<Triangle_t> m_Triangles;
	std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_Cells; // triangles by cell
};

// Where the entity lump puts a brush entity's model ("model" "*3").
struct ModelPlacement_t
{
	int m_nModel = 0;
	std::string m_szClass;
	map::Point_t m_Origin{};
	std::array<double, 3> m_Angles{}; // pitch, yaw, roll
};

struct CompiledMap_t
{
	std::vector<std::string> m_Materials; // by the meshes' material indices, as the map had them (MapMaterialName)
	std::vector<CompiledModel_t> m_Models;
	std::vector<ModelPlacement_t> m_Placements; // in the entity lump's order
	// The baked light: only its atlas (width, height, texels) is filled.
	std::shared_ptr<const map::CLighting> m_pLighting;
	std::shared_ptr<const CAmbientLighting> m_pAmbient; // the same compile's (HDR or not) as m_pLighting
	std::shared_ptr<const CVisibility> m_pVisibility;   // null for a map not vised
	bool m_bHdr = false;      // the light is the HDR compile's
	int m_nDisplacements = 0;
	int m_nHiddenDisplacements = 0; // nodraw ones, which aren't drawn
	int m_nLitFaces = 0;      // faces with a lightmap
	int m_nUnlitFaces = 0;    // drawn as they are (sky, tools, unlit materials)
	std::vector<std::string> m_Problems; // displacements drawn flat, lightmaps that didn't fit
};

struct CompiledOptions_t
{
	bool m_bHdr = true; // the HDR compile's light when the map has it
	int m_nAtlasWidth = 2048;
};

// nullopt (with the reason in pError) when the file isn't a map this reads, or its faces'
// lumps are missing or cut short.
[[nodiscard]] std::optional<CompiledMap_t> ReadCompiledMap( std::span<const std::byte> file, const CompiledOptions_t &options = {}, std::string *pError = nullptr );

} // namespace xv::bsp
