//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Cutting solids by planes, as Hammer's clipping tool, Carve and Make Hollow do: a
//          solid split into the parts in front of and behind a plane (each a convex solid with
//          the plane as a new side), a carver's shape taken out of the solids it meets (the
//          rest in convex pieces), and a solid made into walls round a hollow.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "map/Edit.h"
#include "map/Geometry.h"
#include "map/Map.h"
#include "map/Scene.h"

namespace xv::map
{

// A side on plane, as a VMF writes one: three points with (p0 - p1) x (p2 - p1) along the
// normal, the material and alignment taken from szLike (a side of the solid being cut).
[[nodiscard]] Side_t SideOnPlane( const CMap &map, const Plane_t &plane, const Side_t &like );

// The parts of solid in front of plane (where Distance is above zero) and behind it; nullopt
// where there is nothing. Sides the cut leaves without a face are dropped; Hammer++'s exact
// corners go, the planes stand. A solid with a displacement isn't cut.
struct ClipResult_t
{
	std::optional<Solid_t> m_Front;
	std::optional<Solid_t> m_Back;
};
[[nodiscard]] ClipResult_t ClipSolid( const CMap &map, const Solid_t &solid, const Plane_t &plane );

// Hammer's clipping tool: which part stays.
enum class ClipKeep_t
{
	CLIP_KEEP_FRONT,
	CLIP_KEEP_BACK,
	CLIP_KEEP_BOTH, // split: the back part becomes a solid of its own
};
// The objects' solids cut by plane; a solid the plane misses is left as it is. pAdded gets the
// new solids' object numbers (splitting).
[[nodiscard]] MapEdit_t ClipObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, const Plane_t &plane, ClipKeep_t nKeep, std::vector<std::uint32_t> *pAdded );

// Carve: the carvers' shapes taken out of every other solid they overlap (hidden ones too):
// each such solid is cut by the carver's planes, the parts outside kept as solids of their
// own (the first in the solid's place, the rest new), the part inside gone. The carvers
// themselves stay. pAdded as above.
[[nodiscard]] MapEdit_t CarveObjects( const CScene &scene, const std::vector<std::uint32_t> &carvers, std::vector<std::uint32_t> *pAdded );

// Make Hollow: each selected solid replaced by walls flWallWidth thick inside its faces (a
// negative width builds them outside), one solid a face, grouped; pAdded as above (the first
// wall takes the solid's place).
[[nodiscard]] MapEdit_t HollowObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, double flWallWidth, std::vector<std::uint32_t> *pAdded );

} // namespace xv::map
