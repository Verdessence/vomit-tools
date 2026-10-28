//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's cordon: a box (Hammer++: several) that, while active, is all the compiler
//          sees. Objects whose bounds don't reach into it are left out of the compile and hidden
//          in the views, and the box is sealed with walls of the cordon texture so the rest can't
//          leak. The cordon is saved in the VMF's "cordons" block.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "map/Edit.h"
#include "map/Geometry.h"
#include "map/Primitives.h"
#include "map/Scene.h"

namespace xv::map
{

// The cordon as it is to be: one box (none clears the cordons) and whether it is active.
[[nodiscard]] MapEdit_t SetCordon( const CScene &scene, const std::optional<Box_t> &box, bool bActive );
// The boxes of the active cordons; empty when the cordon is off.
[[nodiscard]] std::vector<Box_t> ActiveCordonBoxes( const CMap &map );
// An object's bounds: a solid's, a point entity's box, a brush entity's solids together.
[[nodiscard]] Box_t ObjectBounds( const CScene &scene, std::uint32_t nObject );
// The objects the active cordons leave out (none when the cordon is off): those whose bounds
// touch no box. A brush entity's solids go with it.
[[nodiscard]] std::vector<std::uint32_t> ObjectsOutsideCordon( const CScene &scene );
// The six walls that seal a box from outside, flThickness thick, of szMaterial.
[[nodiscard]] std::vector<Solid_t> CordonWalls( const CMap &map, const Box_t &box, double flThickness, const FaceDefaults_t &faces );
// The map as the compiler should see it with the cordon on: the objects outside gone, the walls
// added. The scene's own text when the cordon is off.
[[nodiscard]] std::string CordonedMapText( const CScene &scene, std::string_view szMaterial );

} // namespace xv::map
