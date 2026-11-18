//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The sky and fog a demo shows at a tick (H1): the fog of the env_fog_controller a
//          player sees (its m_PlayerFog's), as the controller is networked then (turned on and
//          off, recoloured by the map's inputs), and the 3D skybox's camera the player was
//          given (the sky_camera's place, scale and fog). The controller goes to everyone; the
//          skybox camera only with a player's own data, which SourceTV demos have on some
//          servers and not others.
//
//===========================================================================//
#pragma once

#include <optional>

#include "demo/Contents.h"
#include "map/Lighting.h"
#include "util/Vec3.h"

namespace xv::demo
{

struct DemoSkyCamera_t
{
	CVec3 m_vecOrigin;
	float m_flScale = 16.0f;
	int m_nArea = 0;
	std::optional<map::Fog_t> m_Fog; // the skybox's own fog; nullopt: off
};

struct DemoSky_t
{
	bool m_bFogKnown = false;        // the demo has a fog controller; else only the map can say
	std::optional<map::Fog_t> m_Fog; // the world's fog then; nullopt: none (turned off)
	std::optional<DemoSkyCamera_t> m_Camera;
};

// At nTick, seen as nPlayer sees it (its fog controller and skybox camera); 0, or a player
// without them, takes the first player's that has them, then the first fog controller.
[[nodiscard]] DemoSky_t SkyAt( const CContents &contents, int nTick, int nPlayer );

} // namespace xv::demo
