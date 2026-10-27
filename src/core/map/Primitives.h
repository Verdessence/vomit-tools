//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: New objects as Hammer makes them, ready to add to a map with AddObjects (which gives
//          them ids): the block tool's shapes (Hammer's block first; its other primitives come
//          later) and the entity tool's point entities.
//
//===========================================================================//
#pragma once

#include <string_view>

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

// A point entity of szClass at origin, with the keys Hammer writes for a new one: the classname,
// then every key the game data gives a default (spawnflags: the flags ticked by default) in name
// order, then the origin; in the editor, the class's colour. Without the class in pFgd, only the
// classname and origin. Its text is kept in map's store.
[[nodiscard]] Entity_t MakePointEntity( const CMap &map, const CFgd *pFgd, std::string_view szClass, const Point_t &origin );

} // namespace xv::map
