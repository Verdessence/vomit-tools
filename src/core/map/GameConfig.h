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

// What new objects get, from the game's Hammer configuration (its own defaults without one).
struct HammerDefaults_t
{
	std::string m_szPointEntity = "info_player_start"; // the entity tool's class
	std::string m_szSolidEntity = "func_detail";        // Tie to Entity's
	double m_flTextureScale = 0.25;
	int m_nLightmapScale = 16;
};

// Where the game's Hammer configuration says its compilers, game and compiled maps are: hints
// only (Wine paths from a Windows install; a file named may not be there).
struct HammerCompilePaths_t
{
	std::filesystem::path m_Bsp;
	std::filesystem::path m_Vis;
	std::filesystem::path m_Light;
	std::filesystem::path m_GameExe;
	std::filesystem::path m_BspDir; // where compiled maps go (the game's maps folder)
	std::filesystem::path m_MapDir; // where the VMFs are kept
};

struct GameFgds_t
{
	std::vector<std::filesystem::path> m_Files; // in the order to read them
	std::string m_szSource;                     // where the list came from, for the user
	HammerDefaults_t m_Defaults;
	HammerCompilePaths_t m_Compile;
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
