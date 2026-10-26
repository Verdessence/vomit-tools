//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The mesh of a displacement: a grid spread over its four-cornered face, each point
//          moved along its normal by its distance plus its offset, as vbsp places them.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "map/Brushes.h"
#include "map/Map.h"

namespace xv::map
{

struct DispMesh_t
{
	int m_nSize = 0;                   // points along a side
	std::array<Point_t, 4> m_Corners{}; // the face's corners, from the one nearest the start
	std::vector<Point_t> m_Points;     // where each point ends up, row by row
	std::vector<Point_t> m_Flat;       // where it starts on the face; textures follow these
	std::vector<Point_t> m_Normals;    // for shading, from the triangles around each point
	std::vector<float> m_Alphas;       // 0-1: how much of the second texture shows
	std::vector<std::uint32_t> m_Indices; // triangles, wound like the face's polygon

	[[nodiscard]] Box_t GetBounds() const;
};

// nullopt (with the reason) when the displacement can't be built: its face hasn't four
// corners, or its power or rows didn't read. The face is then drawn flat.
[[nodiscard]] std::optional<DispMesh_t> BuildDisplacement( const DispInfo_t &disp, const FaceGeometry_t &face, std::string *pError = nullptr );

} // namespace xv::map
