//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The building generator: sets of brushes made from a box and a few numbers (stairs,
//          spiral stairs, Hammer's arch, a room with doorways, a wall with windows, a railing),
//          ready for AddObjects.
//
//===========================================================================//
#pragma once

#include <array>
#include <string>
#include <vector>

#include "map/Geometry.h"
#include "map/Map.h"
#include "map/Primitives.h"

namespace xv::map
{

enum class BuildingKind_t
{
	BUILDING_KIND_STAIRS,
	BUILDING_KIND_SPIRAL_STAIRS,
	BUILDING_KIND_ARCH,
	BUILDING_KIND_ROOM,
	BUILDING_KIND_WINDOW_WALL,
	BUILDING_KIND_RAILING,
};

// Which way stairs climb (and a railing or a wall runs: along the axis, either sign).
enum class BuildingDirection_t
{
	BUILDING_DIRECTION_POSITIVE_X,
	BUILDING_DIRECTION_NEGATIVE_X,
	BUILDING_DIRECTION_POSITIVE_Y,
	BUILDING_DIRECTION_NEGATIVE_Y,
};

// Every template's numbers; each reads the ones it needs.
struct BuildingParams_t
{
	// Stairs and spiral stairs.
	int m_nSteps = 8;
	BuildingDirection_t m_nDirection = BuildingDirection_t::BUILDING_DIRECTION_POSITIVE_X;
	bool m_bSolidUnder = true;        // each step reaches the floor (else it is one step thick)
	double m_flDegreesPerStep = 22.5; // spiral: how far round each step turns
	double m_flInnerRadius = 16.0;    // spiral: the open middle (0: steps meet at a post)
	// Hammer's arch: the box's ellipse cut in pieces, each this wide, over the arc from the start
	// angle, each piece raised by the add height over the one before.
	double m_flWallWidth = 32.0;
	int m_nSides = 8;
	double m_flArc = 180.0;
	double m_flStartAngle = 0.0;
	double m_flAddHeight = 0.0;
	// Room: walls, floor and ceiling this thick; a doorway in each wall ticked.
	double m_flThickness = 16.0;
	std::array<bool, 4> m_Doors{ true, false, false, false }; // the +X, -X, +Y, -Y walls
	double m_flDoorWidth = 64.0;
	double m_flDoorHeight = 112.0;
	// Wall with windows: across its length and up its height; the windows' size and how high the
	// lowest starts.
	int m_nColumns = 3;
	int m_nRows = 1;
	double m_flWindowWidth = 48.0;
	double m_flWindowHeight = 64.0;
	double m_flSill = 48.0;
	// Railing: posts this thick every spacing, a rail this tall along the top.
	double m_flPostSize = 4.0;
	double m_flPostSpacing = 64.0;
	double m_flRailHeight = 4.0;
};

// The template's brushes filling box (which must have some size each way), faces as the block
// tool gives them. Empty with pError when the numbers don't make brushes (a window wider than
// the wall, say).
[[nodiscard]] std::vector<Solid_t> GenerateBuilding( const CMap &map, BuildingKind_t nKind, const Box_t &box, const BuildingParams_t &params, const FaceDefaults_t &faces, std::string *pError = nullptr );

} // namespace xv::map
