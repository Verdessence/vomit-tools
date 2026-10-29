//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: info_overlay and infodecal as drawn on the map's faces: an overlay's four corners
//          (or a decal's square) cut to each face it covers, displacements included, and lifted
//          a little off the face so it shows over it, as the game's float over theirs.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "map/Geometry.h"
#include "map/Mesh.h"

namespace xv::map
{

class CScene;
class CMap;
struct MapEdit_t;
struct Entity_t;
struct Transform_t;

// Carry an overlay's projection frame and corners through the same transform as its faces.
void TransformOverlay( const CMap &map, Entity_t &entity, const Transform_t &transform );
// Overlay entity changes implied by solid edits: transformed projections and surviving face IDs.
// Explicit overlay edits are respected, so selecting a face and its overlay never moves it twice.
[[nodiscard]] std::vector<std::pair<std::uint32_t, Entity_t>> FollowOverlayFaces( const CScene &scene, const MapEdit_t &edit );

// Four editing handles in world space, independent of how the projection is tessellated.
[[nodiscard]] std::optional<std::array<Point_t, 4>> OverlayCorners( const Entity_t &entity );
// Nearest point on the overlay's assigned faces (including their displacement triangles).
[[nodiscard]] std::optional<Point_t> OverlaySurfacePoint( const CScene &scene, const Entity_t &entity, Point_t p );
// A corner dragged to its surface; the projection frame stays put and its local uv key changes.
[[nodiscard]] Entity_t MoveOverlayCorner( const CScene &scene, const Entity_t &entity, int nCorner, Point_t p );
// Replace, add or toggle a face assignment, preserving the other side list of a transition.
void AssignOverlayFace( const CMap &map, Entity_t &entity, std::string_view szKey, int nSide, bool bToggle, bool bReplace );

// How far pieces sit off their faces, so they show over them without fighting for depth.
inline constexpr double OVERLAY_LIFT = 0.25;

// What an overlay's or a decal's material is in the map's table, and how big its texture is.
struct OverlayMaterial_t
{
	std::uint32_t m_nMaterial = 0;
	std::array<int, 2> m_TextureSize{ 128, 128 };
	double m_flDecalScale = 1.0; // $decalscale: units a texel of a decal covers
};

// The materials the scene's overlays and decals name (their "material" and "texture" keys),
// each once and as the map's own are named.
[[nodiscard]] std::vector<std::string> OverlayMaterialNames( const CScene &scene );

// Every overlay and decal in pieces on the faces they cover; materials lines up with
// OverlayMaterialNames. Each overlay's corners are outlined, for the wireframe.
// nObject limits rebuilding to one entity while retaining the scene's receiving faces.
[[nodiscard]] CMapMesh BuildOverlayMesh( const CScene &scene, std::span<const OverlayMaterial_t> materials, std::optional<std::uint32_t> nObject = std::nullopt );

// Rebuild several entity parts with one lookup of the scene's receiving triangles.
[[nodiscard]] std::map<std::uint32_t, CMapMesh> BuildOverlayMeshes( const CScene &scene, std::span<const OverlayMaterial_t> materials, std::span<const std::uint32_t> objects );

// Where p lies in the quad c0..c3 as (s, t), where c0 is (0, 0), c1 (0, 1), c2 (1, 1) and
// c3 (1, 0) and the edges between are straight: the inverse of bilinear interpolation.
[[nodiscard]] std::array<double, 2> InverseBilinear( std::array<double, 2> p, const std::array<std::array<double, 2>, 4> &corners );

} // namespace xv::map
