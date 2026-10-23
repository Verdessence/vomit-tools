//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Finding installed Source games through Steam's library folders.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace xv::vfs
{

struct SteamGame_t
{
	std::string m_szName;               // gameinfo.txt's "game"
	std::filesystem::path m_GameInfo; // .../steamapps/common/<Game>/<mod>/gameinfo.txt
};

// The library folders listed in <steamRoot>/steamapps/libraryfolders.vdf, starting with steamRoot.
[[nodiscard]] std::vector<std::filesystem::path> SteamLibraries( const std::filesystem::path &steamRoot );

// Every gameinfo.txt one folder deep inside each game in steamapps/common, plus source mods.
[[nodiscard]] std::vector<SteamGame_t> FindSourceGames( const std::vector<std::filesystem::path> &libraries );

} // namespace xv::vfs
