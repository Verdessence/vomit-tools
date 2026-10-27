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
#include <span>
#include <string>
#include <vector>

#include "map/Geometry.h"
#include "map/Mesh.h"

namespace xv::map
{

class CScene;

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
[[nodiscard]] CMapMesh BuildOverlayMesh( const CScene &scene, std::span<const OverlayMaterial_t> materials );

// Where p lies in the quad c0..c3 as (s, t), where c0 is (0, 0), c1 (0, 1), c2 (1, 1) and
// c3 (1, 0) and the edges between are straight: the inverse of bilinear interpolation.
[[nodiscard]] std::array<double, 2> InverseBilinear( std::array<double, 2> p, const std::array<std::array<double, 2>, 4> &corners );

} // namespace xv::map
