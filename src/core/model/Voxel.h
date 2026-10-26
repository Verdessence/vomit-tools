//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Isolated voxel decomposition backend for open render meshes.
//===========================================================================//
#pragma once
#include "Vmf.h"
#include <array>
namespace xv::model
{
[[nodiscard]] std::vector<Convex_t> VoxelHulls( std::span<const std::array<double, 3>> points, std::span<const std::array<int, 3>> triangles, const VmfOptions_t &options );
}
