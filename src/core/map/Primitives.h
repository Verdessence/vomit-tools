//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: New objects as Hammer makes them, ready to add to a map with AddObjects (which gives
//          them ids): the block tool's primitives (block, wedge, cylinder, spike, sphere) and
//          the entity tool's point entities.
//
//===========================================================================//
#pragma once

#include <string_view>
#include <vector>

#include "map/Fgd.h"
#include "map/Geometry.h"
#include "map/Map.h"

namespace xv::map
{

// What a new solid's faces get.
struct FaceDefaults_t
{
	std::string_view m_szMaterial = "DEV/DEV_MEASUREGENERIC01";
	double m_flTextureScale = 0.25;
	int m_nLightmapScale = 16;
	Color_t m_Color{ 0, 128, 255 }; // the editor colour Hammer gives each new solid
};

// A box filling box, its textures aligned to the world as Hammer aligns a new block's: floors
// and ceilings along x and -y, walls along their horizontal axis and -z. Its text is kept in
// map's store.
[[nodiscard]] Solid_t MakeBlock( const CMap &map, const Box_t &box, const FaceDefaults_t &faces );

// Hammer's other single-brush primitives, filling box (Hammer's "reorient" off: a cylinder's
// round side shows in the top view, a wedge slopes down toward +x). nSides is Hammer's Faces
// box: a cylinder's and spike's sides, a sphere's slices round (half as many bands up).
enum class Primitive_t
{
	PRIMITIVE_BLOCK,
	PRIMITIVE_WEDGE,
	PRIMITIVE_CYLINDER,
	PRIMITIVE_SPIKE,
	PRIMITIVE_SPHERE,
};
[[nodiscard]] Solid_t MakePrimitive( const CMap &map, Primitive_t nKind, const Box_t &box, const FaceDefaults_t &faces, int nSides );
// A convex solid from its faces (each wound either way): planes through them, the sides facing
// out, textures world-aligned.
[[nodiscard]] Solid_t SolidFromFaces( const CMap &map, const std::vector<std::vector<Point_t>> &polygons, const FaceDefaults_t &faces );

// A point entity of szClass at origin, with the keys Hammer writes for a new one: the classname,
// then every key the game data gives a default (spawnflags: the flags ticked by default) in name
// order, then the origin; in the editor, the class's colour. Without the class in pFgd, only the
// classname and origin. Its text is kept in map's store.
[[nodiscard]] Entity_t MakePointEntity( const CMap &map, const CFgd *pFgd, std::string_view szClass, const Point_t &origin );

// A surface decal at the picked point, using the texture toolbar's current material.
[[nodiscard]] Entity_t MakeDecal( const CMap &map, Point_t origin, std::string_view szMaterial );

} // namespace xv::map
