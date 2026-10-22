// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Finding installed Source games through Steam's library folders.
namespace xv::vfs {

struct SteamGame {
	std::string name;               // gameinfo.txt's "game"
	std::filesystem::path gameInfo; // .../steamapps/common/<Game>/<mod>/gameinfo.txt
};

// The library folders listed in <steamRoot>/steamapps/libraryfolders.vdf, starting with steamRoot.
[[nodiscard]] std::vector<std::filesystem::path> steamLibraries(const std::filesystem::path& steamRoot);

// Every gameinfo.txt one folder deep inside each game in steamapps/common, plus source mods.
[[nodiscard]] std::vector<SteamGame> findSourceGames(const std::vector<std::filesystem::path>& libraries);

} // namespace xv::vfs
