//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Finds the game data files (.fgd) Hammer would use for a game: from Hammer++'s or
//          Hammer's game configuration when it lists the game, else from the game's bin folder.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xv::map
{

struct GameFgds_t
{
	std::vector<std::filesystem::path> m_Files; // in the order to read them
	std::string m_szSource;                     // where the list came from, for the user
};

// gameDir is the folder that holds gameinfo.txt (".../Counter-Strike Source/cstrike").
[[nodiscard]] GameFgds_t FindGameFgds( const std::filesystem::path &gameDir );

// A path from a Hammer configuration file as a local path. On Linux the configs come from Wine,
// so "Z:\home\x" is "/home/x"; other drives are looked for under szRoot by the part after the
// game's own folder name.
[[nodiscard]] std::filesystem::path LocalPath( std::string_view szConfigPath, const std::filesystem::path &root );

// path, matching each part without regard to case and trying every match when two differ only
// in case (a game can have both "bin" and "BIN"). The path itself when nothing matches.
[[nodiscard]] std::filesystem::path FindFileIgnoringCase( const std::filesystem::path &path );

} // namespace xv::map
