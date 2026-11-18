//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Players' sprays in a demo: each CTEPlayerDecal where it was put (one sent at 16384
//          on every axis takes the player's spray off), the image the player's custom logo
//          (its CRC in the player's userinfo), which the game keeps as a VTF under
//          download/user_custom/. A player has one spray up at a time: a new one replaces it.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "demo/Contents.h"
#include "demo/Players.h"

namespace xv::demo
{

struct DemoSpray_t
{
	int m_nPlayer = 0; // the player's entity
	int m_nTick = 0;
	CVec3 m_vecOrigin; // on the surface sprayed
	int m_nEntity = 0; // the entity sprayed on (0: the world)
	std::uint32_t m_nLogo = 0; // the logo's CRC; 0 when the player's userinfo has none
};

class CSprays
{
public:
	CSprays() = default;
	// Every spray the demo has, in time order.
	CSprays( const CContents &contents, const CPlayers &players );
	[[nodiscard]] const std::vector<DemoSpray_t> &GetAll() const { return m_Sprays; }
	// Those up at a tick: each player's last, unless taken off.
	[[nodiscard]] std::vector<DemoSpray_t> At( int nTick ) const;

private:
	std::vector<DemoSpray_t> m_Sprays; // a removal has no logo and the origin at 16384
};

// Where the game keeps a logo's VTF, from the game's folder: "download/user_custom/5d/5dea45a1.dat".
[[nodiscard]] std::string SprayFile( std::uint32_t nLogo );

} // namespace xv::demo
