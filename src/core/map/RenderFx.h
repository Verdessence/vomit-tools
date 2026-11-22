//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: How an entity's render keys change its look: rendercolor tints it, and renderamt is
//          its opacity in the render modes that use one, which renderfx changes over time
//          (pulses, strobes, flickers, fades). Worked out from what the keys are documented to
//          do, not from the engine's code.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>

#include "map/Map.h"

namespace xv::map
{

struct RenderLook_t
{
	std::array<std::uint8_t, 4> m_Tint{ 255, 255, 255, 255 }; // rgb times the texture; a: opacity
	bool m_bAnimated = false;                                  // its renderfx changes it over time
};

// The look at a moment (seconds, any start); nSeed staggers effects of the same kind between
// entities, as the game does, so they don't pulse together.
[[nodiscard]] RenderLook_t RenderLookOf( const Entity_t &entity, double flTime, std::uint32_t nSeed );

} // namespace xv::map
