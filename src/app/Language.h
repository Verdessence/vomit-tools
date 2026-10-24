//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The editor's text (particles, materials, sound files), input kinds for particles and
//          the material parameters, read once from the files built into the app
//          (data/lang_en.txt, data/particle_inputs.txt, data/material_parameters.txt).
//
//===========================================================================//
#pragma once

#include "material/Parameters.h"
#include "particles/Names.h"

[[nodiscard]] const xv::particles::CNames &ParticleNames();
[[nodiscard]] const xv::material::CParameterTable &MaterialParameters();
