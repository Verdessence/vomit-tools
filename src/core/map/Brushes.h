//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The shape of each solid: one polygon per side, cut from the side's plane by all the
//          others, as Hammer and vbsp build them. Solids that don't close up are kept and
//          flagged, so they can be shown as broken.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <vector>

#include "map/Geometry.h"
#include "map/Map.h"

namespace xv::map
{

enum SolidProblem_t : std::uint32_t
{
	SOLID_PROBLEM_UNREADABLE_PLANE = 1u << 0, // a side's plane text didn't read
	SOLID_PROBLEM_DEGENERATE_PLANE = 1u << 1, // a side's three points lie on a line
	SOLID_PROBLEM_DUPLICATE_PLANE = 1u << 2,  // two sides on the same plane
	SOLID_PROBLEM_SIDE_WITHOUT_FACE = 1u << 3, // the other sides cut a side away entirely
	SOLID_PROBLEM_OPEN = 1u << 4,             // the sides don't enclose anything finite
	SOLID_PROBLEM_EMPTY = 1u << 5,            // nothing lies behind every side
	SOLID_PROBLEM_TOO_FEW_SIDES = 1u << 6,
	SOLID_PROBLEM_OUT_OF_BOUNDS = 1u << 7,    // beyond +/-16384 units (drawn, but vbsp refuses it)
};

struct FaceGeometry_t
{
	std::uint32_t m_nSide = 0;         // index into the solid's sides
	Plane_t m_Plane;
	std::vector<Point_t> m_Points;     // wound clockwise seen from outside
	bool m_bPlusVertices = false;      // the points are Hammer++'s own
};

struct SolidGeometry_t
{
	std::vector<FaceGeometry_t> m_Faces;
	Box_t m_Bounds;
	std::uint32_t m_nProblems = 0;     // SolidProblem_t bits

	// Hammer draws a solid that isn't valid in its invalid colour.
	[[nodiscard]] bool IsValid() const { return ( m_nProblems & ~SOLID_PROBLEM_OUT_OF_BOUNDS ) == 0; }
	[[nodiscard]] const FaceGeometry_t *FindFace( std::uint32_t nSide ) const;
};

struct BrushOptions_t
{
	// How far a corner may stray from a plane and still count as on it. Hammer writes plane
	// points to a few decimals, so exactly zero would split faces into slivers.
	double m_flEpsilon = 0.01;
	// How far Hammer++'s vertices may stray from the planes and still be drawn instead of the
	// corners the planes make.
	double m_flPlusTolerance = 0.5;
};

[[nodiscard]] SolidGeometry_t BuildSolid( const Solid_t &solid, const BrushOptions_t &options = {} );
// Every solid of the map, built on all cores; the result lines up with map.m_Solids.
[[nodiscard]] std::vector<SolidGeometry_t> BuildSolids( const CMap &map, const BrushOptions_t &options = {} );

} // namespace xv::map
