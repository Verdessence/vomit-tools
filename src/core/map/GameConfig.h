//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Finds the game data files (.fgd) Hammer would use for a game: from Hammer++'s or
//          Hammer's game configuration when it lists the game, else from the game's bin folder.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <optional>
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

// One game's entry in Hammer++'s or Hammer's game configuration (Configs > Games), as local
// paths: what Tools > Set Up Hammer++ shows and writes.
struct HammerSetup_t
{
	std::string m_szName;                   // the entry's name, which Hammer lists
	std::filesystem::path m_GameDir;
	std::vector<std::filesystem::path> m_Fgds; // GameData0, GameData1...
	std::filesystem::path m_GameExe;
	std::filesystem::path m_Bsp;
	std::filesystem::path m_Vis;
	std::filesystem::path m_Light;
	std::filesystem::path m_Mdl;
	std::filesystem::path m_GameExeDir;
	std::filesystem::path m_MapDir;
	std::filesystem::path m_BspDir;
	std::filesystem::path m_PrefabDir;
	std::string m_szCordonTexture = "tools\\toolsskybox";
	HammerDefaults_t m_Defaults;
};

enum class HammerKind_t
{
	HAMMER_KIND_HAMMER_PLUS_PLUS, // bin/x64/hammerplusplus/hammerplusplus_gameconfig.txt
	HAMMER_KIND_HAMMER,           // bin/GameConfig.txt
};

// Where that Hammer keeps its configuration for the game in gameDir (it may not exist yet), and
// its program (nothing when it isn't installed).
[[nodiscard]] std::filesystem::path HammerConfigFile( const std::filesystem::path &gameDir, HammerKind_t nKind );
[[nodiscard]] std::optional<std::filesystem::path> HammerProgram( const std::filesystem::path &gameDir, HammerKind_t nKind );
// The entry for the game in gameDir in a configuration's text, if it has one.
[[nodiscard]] std::optional<HammerSetup_t> ReadHammerSetup( std::string_view szText, const std::filesystem::path &gameDir );
// An entry for the game from what is in its folders: its FGDs, the compilers and Hammer's
// prefabs beside it, the game's program, its maps folder.
[[nodiscard]] HammerSetup_t SuggestHammerSetup( const std::filesystem::path &gameDir, std::string_view szName );
// The configuration's text with the game's entry (found by its GameDir, else added) holding
// setup's values; every other entry and key, and the file's layout, kept. szExisting may be empty.
[[nodiscard]] std::string WriteHammerSetup( std::string_view szExisting, const HammerSetup_t &setup );
// A local path as a Windows Hammer reads it: itself on Windows; Wine's Z: drive elsewhere.
[[nodiscard]] std::string ConfigPath( const std::filesystem::path &path );

} // namespace xv::map
