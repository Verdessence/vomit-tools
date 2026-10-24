//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Where a detail type's props land on a square of ground, the way the map compiler
//          scatters them: as many as the density gives for the area, each given a group by the
//          blend alpha under it (between the two groups whose alphas bracket it, by how near it
//          is to each), then an entry by the amounts (what is left over places nothing), a random
//          turn and, for sprites, a random scale.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <vector>

#include "detail/Detail.h"
#include "util/Vec3.h"

namespace xv::detail
{

struct Placed_t
{
	NodePath_t m_Entry;  // the entry it is
	CVec3 m_vecOrigin;   // on the ground, z = 0
	float m_flYaw = 0;   // degrees
	float m_flScale = 1; // from spriterandomscale
};

struct ScatterOptions_t
{
	float m_flSize = 512.0f; // the square's side, centred on the origin
	std::uint32_t m_nSeed = 1;
	// The blend alpha under the ground, from m_flAlphaLow at -x to m_flAlphaHigh at +x, as a
	// blended displacement has it.
	float m_flAlphaLow = 0.0f;
	float m_flAlphaHigh = 1.0f;
	std::size_t m_nMaxProps = 20000;
};

// The blend alpha at a point of the ground.
[[nodiscard]] float BlendAlphaAt( float x, const ScatterOptions_t &options );
// The props of one detail type on the square. The same seed places them the same way.
[[nodiscard]] std::vector<Placed_t> Scatter( const kv1::CDocument &document, const Type_t &type, const ScatterOptions_t &options );

} // namespace xv::detail
