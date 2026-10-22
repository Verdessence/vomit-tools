// SPDX-License-Identifier: GPL-3.0-or-later
#include "Steam.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>

#include "../kv1/Kv1.h"
#include "Vfs.h"

namespace xv::vfs {

namespace fs = std::filesystem;

namespace {

// Directory entries, or none if the folder can't be read; never throws.
std::vector<fs::directory_entry> entriesOf(const fs::path& directory) {
	std::vector<fs::directory_entry> entries;
	std::error_code ec;
	for (fs::directory_iterator it{directory, ec}; !ec && it != fs::directory_iterator{}; it.increment(ec)) {
		entries.push_back(*it);
	}
	return entries;
}

fs::path normalized(const fs::path& path) {
	std::error_code ec;
	const fs::path canonical = fs::weakly_canonical(path, ec);
	return ec ? path.lexically_normal() : canonical;
}

} // namespace

std::vector<fs::path> steamLibraries(const fs::path& steamRoot) {
	std::vector<fs::path> libraries{normalized(steamRoot)};
	std::ifstream stream{steamRoot / "steamapps" / "libraryfolders.vdf", std::ios::binary};
	if (!stream) {
		return libraries;
	}
	const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	// Windows paths in it are written with escaped backslashes.
	const auto parsed = kv1::parse(text, {.escapeSequences = true});
	for (const kv1::Node& root : parsed.document.roots) {
		for (const kv1::Node& entry : root.children()) {
			std::string path;
			if (entry.isBlock()) {
				// Current format: "0" { "path" "..." ... }
				if (const kv1::Node* value = entry.find("path")) {
					path = value->value();
				}
			} else if (entry.isPair() && !entry.key().empty() && std::isdigit(static_cast<unsigned char>(entry.key().front()))) {
				// Older format: "1" "D:\\SteamLibrary"
				path = entry.value();
			}
			if (!path.empty()) {
				const fs::path library = normalized(fromUtf8(path));
				if (std::ranges::find(libraries, library) == libraries.end()) {
					libraries.push_back(library);
				}
			}
		}
	}
	return libraries;
}

std::vector<SteamGame> findSourceGames(const std::vector<fs::path>& libraries) {
	std::vector<SteamGame> games;
	std::set<fs::path> seen;
	const auto consider = [&](const fs::path& gameInfo) {
		std::error_code ec;
		if (!fs::is_regular_file(gameInfo, ec) || !seen.insert(normalized(gameInfo)).second) {
			return;
		}
		if (const auto info = GameInfo::load(gameInfo)) {
			games.push_back({info->game.empty() ? toUtf8(gameInfo.parent_path().filename()) : info->game, gameInfo});
		}
	};
	for (const fs::path& library : libraries) {
		for (const auto& game : entriesOf(library / "steamapps" / "common")) {
			for (const auto& mod : entriesOf(game.path())) {
				consider(mod.path() / "gameinfo.txt");
			}
		}
		for (const auto& mod : entriesOf(library / "steamapps" / "sourcemods")) {
			consider(mod.path() / "gameinfo.txt");
		}
	}
	std::ranges::sort(games, {}, &SteamGame::name);
	return games;
}

} // namespace xv::vfs
