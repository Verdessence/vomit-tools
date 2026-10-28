//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Runs a program from the command line tool, without Qt: its output (both streams)
//          handed over as it comes, its exit code returned; or started and left to itself.
//
//===========================================================================//
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace xv::cli
{

// Runs program with arguments in directory, calling pfnOutput with each piece of output; the
// exit code, or -1 when it couldn't be started (the reason goes to pfnOutput).
[[nodiscard]] int RunProcess( const std::string &szProgram, const std::vector<std::string> &arguments, const std::string &szDirectory, const std::function<void( std::string_view )> &pfnOutput );
// Starts program and doesn't wait (the game); false when it couldn't be started.
[[nodiscard]] bool StartProcess( const std::string &szProgram, const std::vector<std::string> &arguments, const std::string &szDirectory );

} // namespace xv::cli
