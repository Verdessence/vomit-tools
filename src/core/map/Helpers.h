//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the FGD's helpers draw for an entity besides its box, model or sprite: lines to
//          the entities its keys name (line()), a spot light's cones (lightcone()), a box from
//          two keys (wirebox()), an arrow along its angles (direction()), a projected texture's
//          frustum (frustum()), the faces a side list names (sidelist()), and its radii
//          (sphere()) as rings, as segments the 2D and 3D views draw for selected entities.
//          Besides the FGD's: where moving brushes go (doors, buttons, func_movelinear) and the
//          way along a train's path, which way rotating doors and func_rotating turn, where
//          a trigger_teleport sends things, and every path's links, selected or not.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <vector>

#include "map/Geometry.h"
#include "map/Map.h"

namespace xv::map
{

class CScene;

struct HelperLine_t
{
	Point_t m_From{};
	Point_t m_To{};
	Color_t m_Color{};
};

struct EntityHelpers_t
{
	std::vector<HelperLine_t> m_Lines;
	std::vector<HelperLine_t> m_Rings;  // the sphere()s' great circles: the 2D views draw a circle instead
	std::vector<int> m_Sides;           // side ids a sidelist() names
};

// The entity's helpers, by its index in the scene's map; lines to other entities find them by
// the key the helper names (targetname, most often), ignoring case.
[[nodiscard]] EntityHelpers_t DescribeHelpers( const CScene &scene, std::uint32_t nEntity );

// A path_track's or path_corner's line to its target (entities by index), with its arrowhead
// where the path goes. Drawn whether or not either is selected, as Hammer draws paths.
struct PathLink_t
{
	std::uint32_t m_nFrom = 0;
	std::uint32_t m_nTo = 0;
	std::vector<HelperLine_t> m_Lines;
};
[[nodiscard]] std::vector<PathLink_t> DescribePathLinks( const CScene &scene );

} // namespace xv::map
