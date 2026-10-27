//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The 3D skybox: the room around the map's sky_camera, drawn behind the world as the
//          game draws it. The sky_camera stands for the map's origin, and the room is seen from
//          there scaled down by its scale (the Sky_camera article), so a unit in the room looks
//          that many units across.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "map/Geometry.h"
#include "map/ViewCamera.h"

namespace xv::map
{

class CScene;

struct Skybox3D_t
{
	Point_t m_Camera{};       // the sky_camera (in an instance, where the instance puts it)
	double m_flScale = 16.0;
	Box_t m_Room;             // around the walls nearest the sky_camera
	std::vector<std::uint32_t> m_Objects; // sorted: everything inside the room
};

// The scene's 3D skybox: its first sky_camera, the room around it (bounded by what is nearest
// along each axis) and what lies inside that room. nullopt without a sky_camera.
[[nodiscard]] std::optional<Skybox3D_t> FindSkybox3D( const CScene &scene );

// Where the room is seen from while the world is seen from camera: the sky_camera plus the eye
// scaled down, looking the same way.
[[nodiscard]] CViewCamera SkyboxCamera( const CViewCamera &camera, const Skybox3D_t &sky );

} // namespace xv::map
