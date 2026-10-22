// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The engine's view of game content: gameinfo.txt search paths mounted in order, with the
// first mount that has a file winning. Paths are case-insensitive, as on Windows.
namespace xv::vfs {

// "Materials\\Skybox/Foo.VMT" -> "materials/skybox/foo.vmt"
[[nodiscard]] std::string normalizePath(std::string_view path);

// Filesystem paths as UTF-8, the encoding used for every std::string path in vomit-tools. (On Windows,
// std::filesystem::path::string() would use the ANSI code page instead.)
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path fromUtf8(std::string_view utf8);

struct SearchPath {
	std::vector<std::string> ids; // lowercase path IDs: game, mod, platform, gamebin, ...
	std::string path;             // as written in gameinfo.txt
};

struct GameInfo {
	std::filesystem::path file;    // .../cstrike/gameinfo.txt
	std::filesystem::path gameDir; // |gameinfo_path|
	std::filesystem::path baseDir; // |all_source_engine_paths|, the folder holding the game executable
	std::string game;
	std::string title;
	std::vector<SearchPath> searchPaths;

	// baseDir defaults to the parent of the gameinfo folder, which holds for Valve's own games.
	// Source mods running on an SDK base need it passed in.
	[[nodiscard]] static std::optional<GameInfo> load(const std::filesystem::path& file, std::string* error = nullptr, std::filesystem::path baseDir = {});
	[[nodiscard]] static std::optional<GameInfo> parse(std::string_view text, const std::filesystem::path& file, std::string* error = nullptr, std::filesystem::path baseDir = {});
};

class Mount {
public:
	enum class Kind { Directory, Vpk };

	Mount(Kind kind, std::filesystem::path location, std::vector<std::string> ids);
	virtual ~Mount() = default;

	[[nodiscard]] Kind kind() const { return kind_; }
	[[nodiscard]] const std::filesystem::path& location() const { return location_; }
	[[nodiscard]] const std::vector<std::string>& ids() const { return ids_; }
	[[nodiscard]] bool hasId(std::string_view id) const;

	// Paths passed in must already be normalized.
	[[nodiscard]] virtual bool contains(const std::string& path) const = 0;
	[[nodiscard]] virtual std::optional<std::vector<std::byte>> read(const std::string& path) const = 0;
	virtual void forEachFile(const std::function<void(const std::string& path)>& callback) const = 0;
	[[nodiscard]] virtual std::size_t fileCount() const = 0;

	// The real file behind a path, for folder mounts; nullopt for files inside a VPK.
	[[nodiscard]] virtual std::optional<std::filesystem::path> diskPath(const std::string& path) const;

private:
	Kind kind_;
	std::filesystem::path location_;
	std::vector<std::string> ids_;
};

// One per search path after wildcard expansion, kept even when mounting failed so the UI can show why.
struct MountRecord {
	enum class Status { Mounted, Missing, Failed };

	std::string searchPath;          // the gameinfo.txt entry this came from
	std::filesystem::path location;
	std::vector<std::string> ids;
	Status status = Status::Mounted;
	std::string message;
	const Mount* mount = nullptr;
};

class FileSystem {
public:
	// Replaces whatever was mounted before.
	void mount(const GameInfo& info);
	void clear();

	const Mount* addDirectory(const std::filesystem::path& directory, std::vector<std::string> ids, std::string* error = nullptr);
	const Mount* addVpk(const std::filesystem::path& file, std::vector<std::string> ids, std::string* error = nullptr);

	[[nodiscard]] const std::vector<MountRecord>& records() const { return records_; }

	[[nodiscard]] const Mount* resolve(std::string_view path, std::string_view pathId = "game") const;
	[[nodiscard]] std::optional<std::vector<std::byte>> read(std::string_view path, std::string_view pathId = "game") const;
	[[nodiscard]] std::optional<std::string> readText(std::string_view path, std::string_view pathId = "game") const;

	// Every file visible under pathId, with the mount it is read from.
	[[nodiscard]] std::map<std::string, const Mount*> files(std::string_view pathId = "game") const;

	// Visible files under a directory with the given extension ("" for all), e.g. ("scripts", ".txt").
	[[nodiscard]] std::vector<std::string> find(std::string_view directory, std::string_view extension, bool recursive = false, std::string_view pathId = "game") const;

private:
	void mountSearchPath(const GameInfo& info, const SearchPath& searchPath);
	void record(const SearchPath& searchPath, const std::filesystem::path& location, MountRecord::Status status, std::string message, const Mount* mount);

	std::vector<std::unique_ptr<Mount>> mounts_;
	std::vector<MountRecord> records_;
};

// Resolve a path whose components may differ in case from what is on disk (gameinfo.txt
// is written for case-insensitive Windows). Returns the input unchanged if nothing matches.
[[nodiscard]] std::filesystem::path findCaseInsensitive(const std::filesystem::path& path);

} // namespace xv::vfs
