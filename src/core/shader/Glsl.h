//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A pixel shader's bytecode (ps_2_0 to ps_3_0) written again as a GLSL 440 fragment
//          shader, for the graphics card to draw a screen effect with: the same instructions
//          the interpreter runs, as GLSL statements. What it reads:
//          - binding 0, a uniform block: vec4 c[224] (the constants, the program's own def
//            values among them), ivec4 i[16], vec4 b[16] (x non-zero for true), then uScreen
//            (the target's width and height, whether gl_FragCoord's y goes up, whether the
//            frame's texture's rows do), uFrameSampler (which of s0..s3 is the frame: 1),
//            uSrgb (which are read as sRGB: 1), uMode (the blend, linear write, copy alpha);
//          - bindings 1 to 4: s0 to s3; binding 5: the frame, to blend onto.
//          The result goes onto the frame as screenspace_general's blend says, in sRGB unless
//          the material writes linearly, its alpha 1.
//
//===========================================================================//
#pragma once

#include <optional>
#include <string>

#include "shader/Bytecode.h"

namespace xv::shader
{

constexpr int GLSL_CONSTANTS = 224;

// nullopt, with why, when the program can't be written so (a vertex shader, ps_1_x,
// subroutines).
[[nodiscard]] std::optional<std::string> TranslateToGlsl( const Program_t &program, std::string *pError = nullptr );

} // namespace xv::shader
