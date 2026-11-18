//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The particle systems a demo has going at a tick (H7): every info_particle_system
//          (CParticleSystem) switched on then, with its effect from the ParticleEffectNames
//          table and the tick it was switched on, and the particle effects dispatched lately
//          (CTEEffectDispatch of the "ParticleEffect" kind: the effect by name, a place and
//          angles, the entity it was for). Where each one is drawn, and its simulation, are
//          the viewer's.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "demo/Contents.h"
#include "util/Vec3.h"

namespace xv::demo
{

struct DemoParticles_t
{
	std::uint64_t m_nKey = 0;                // the same while it plays on (its entity's life or its dispatch, and its start)
	std::string_view m_szEffect;             // the system's name
	int m_nStartTick = 0;                    // when it was switched on, or dispatched
	const EntityLife_t *m_pLife = nullptr;   // an info_particle_system's; null for a dispatched one
	CVec3 m_vecOrigin;                       // a dispatched one's place
	CVec3 m_vecAngles;
	int m_nEntity = 0;                       // the entity a dispatched one was for; 0 for none
};

// Every system on at nTick, and the effects dispatched in the nDispatchTicks before it.
[[nodiscard]] std::vector<DemoParticles_t> ParticlesAt( const CContents &contents, int nTick, int nDispatchTicks );

} // namespace xv::demo
