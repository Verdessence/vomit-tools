//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: func_instance: another map file drawn inside this one, moved and turned as the
//          entity says (the VDC's func_instance article). Each file is read once for each set
//          of $variable replacements; instances inside instances are followed up to 8 deep, and
//          a file that comes back into itself is stopped.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "map/Geometry.h"
#include "map/Mesh.h"

namespace xv::map
{

class CScene;
struct SceneOptions_t;

// A turn (with any scale or shear) and then a move, in doubles.
struct Transform_t
{
	std::array<double, 9> m_Rotation{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }; // three rows
	Point_t m_Offset{};
	// The point a turn, scale or shear is about (it stays where it is). Texture lock without
	// scale lock keeps the texel under it; a move has none.
	Point_t m_Pivot{};

	// Source's angles: pitch (positive looks down), yaw (about up, 0 facing +X) and roll.
	[[nodiscard]] static Transform_t FromAngles( Point_t origin, const std::array<double, 3> &angles, double flScale = 1.0 );
	[[nodiscard]] static Transform_t Translation( Point_t offset );
	// Each axis stretched by its factor about pivot (a negative one mirrors).
	[[nodiscard]] static Transform_t Scaling( Point_t pivot, Point_t factors );
	// Turned about the x, y or z axis (nAxis) through pivot, by the right-hand rule.
	[[nodiscard]] static Transform_t Rotation( Point_t pivot, int nAxis, double flDegrees );
	// Slid along axis nAlong by flRatio times how far a point is from pivot along nAxis.
	[[nodiscard]] static Transform_t Shear( Point_t pivot, int nAxis, int nAlong, double flRatio );
	[[nodiscard]] bool IsIdentity() const;
	[[nodiscard]] Point_t Apply( Point_t p ) const;
	[[nodiscard]] Point_t Rotate( Point_t v ) const;
	// This after other: other is applied first.
	[[nodiscard]] Transform_t operator*( const Transform_t &other ) const;
};

// A func_instance and its map: the scene built from the file, and how it sits in the scene that
// holds it. Instances inside it are in its scene's own list.
struct PlacedInstance_t
{
	std::uint32_t m_nObject = 0; // the func_instance, in the scene holding it
	std::shared_ptr<const CScene> m_pScene;
	Transform_t m_ToParent;
};

class CInstanceCache;

struct InstanceOptions_t
{
	std::filesystem::path m_MapFile;                  // its folder is where "file" keys start
	std::vector<std::filesystem::path> m_SearchPaths; // then these (gameinfo's InstancePath)
	int m_nMaxDepth = 8;
	std::size_t m_nMaxInstances = 4096;
	std::uintmax_t m_nMaxBytes = 512ull << 20; // all the instance files read, together
	// Shared by an instance's own instances; made by the first call.
	std::shared_ptr<CInstanceCache> m_pCache;
	int m_nDepth = 0;
};

// The file a func_instance's "file" key names: from the map's folder, then from the nearest
// folder above it called maps, then from each search path, with ".vmf" added when it is left
// out. Case is ignored, as Windows ignores it.
[[nodiscard]] std::optional<std::filesystem::path> FindInstanceFile( std::string_view szFile, const InstanceOptions_t &options );

// Calls pfn for the scene and for every instance in it (and in those), each with where its
// contents go in the scene and the object they draw as there: none for the scene's own, whose
// entities are their own objects; the outermost func_instance for an instance's.
using InstanceVisitor_t = std::function<void( const CScene &inner, const Transform_t &toScene, std::optional<std::uint32_t> nObject )>;
void ForEachInstance( const CScene &scene, const InstanceVisitor_t &pfnVisit );

// Copies in onto the end of out, placed by transform; every vertex becomes nObject when given.
void AppendPlaced( CMapMesh &out, const CMapMesh &in, const Transform_t &transform, std::optional<std::uint32_t> nObject );

// Builds the scene's func_instances (and theirs) into scene.m_Instances, adds the materials only
// they use to scene.m_Materials, and lays every instance's solids, placed, into
// scene.m_InstanceMesh as its func_instance's object. Problems go in scene.m_Problems.
void LoadInstances( CScene &scene, const SceneOptions_t &options );

} // namespace xv::map
