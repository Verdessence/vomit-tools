//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What a ray through the 3D view hits, for picking: every object it passes through,
//          nearest first, so clicking picks the front one and Page Down the next behind it.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <vector>

#include "map/Raster.h"

namespace xv::map
{

struct TraceHit_t
{
	std::uint32_t m_nObject = 0;
	std::uint32_t m_nMesh = 0;     // which of the scene's meshes
	std::uint32_t m_nTriangle = 0; // of that mesh, as it was hit first
	double m_flDistance = 0.0;
};

// The objects along the ray (dir a unit vector) within flMax, each once at its nearest hit,
// nearest first. Hidden objects, and tool textures when they're hidden, are passed through.
[[nodiscard]] std::vector<TraceHit_t> TraceScene( const RasterScene_t &scene, bool bToolTextures, Point_t from, Point_t dir, double flMax );

} // namespace xv::map
