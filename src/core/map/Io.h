//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Entity outputs as Hammer writes them: "target,input,parameter,delay,times", with commas
//          before Left 4 Dead 2's tools and the escape character (0x1B) after.
//
//===========================================================================//
#pragma once

#include <string_view>

#include "map/Map.h"

namespace xv::map
{

// Splits an output's value. The parameter is everything between the input and the delay, so
// parameters that contain the separator themselves (AddOutput's) survive. With four fields the
// times are left out, which Hammer reads as once.
[[nodiscard]] Connection_t ParseConnection( std::string_view szOutput, std::string_view szValue );

} // namespace xv::map
