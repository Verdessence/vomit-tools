//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The editor's text (particles, materials, sound files) and input kinds for particles,
//          read once from the files built into the app (data/lang_en.txt,
//          data/particle_inputs.txt).
//
//===========================================================================//
#pragma once

#include "particles/Names.h"

[[nodiscard]] const xv::particles::CNames &ParticleNames();
