//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The engine's view of game content: gameinfo.txt search paths mounted in order, with the
//          first mount that has a file winning. Paths are case-insensitive, as on Windows.
//
//===========================================================================//
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

namespace xv::vfs
{

// "Materials\\Skybox/Foo.VMT" -> "materials/skybox/foo.vmt"
[[nodiscard]] std::string NormalizePath( std::string_view szPath );

// Filesystem paths as UTF-8, the encoding used for every std::string path in vomit-tools. (On Windows,
// std::filesystem::path::string() would use the ANSI code page instead.)
[[nodiscard]] std::string ToUtf8( const std::filesystem::path &path );
[[nodiscard]] std::filesystem::path FromUtf8( std::string_view szUtf8 );

struct SearchPath_t
{
	std::vector<std::string> m_Ids; // lowercase path IDs: game, mod, platform, gamebin, ...
	std::string m_szPath;             // as written in gameinfo.txt
};

struct CGameInfo
{
	std::filesystem::path m_File;    // .../cstrike/gameinfo.txt
	std::filesystem::path m_GameDir; // |gameinfo_path|
	std::filesystem::path m_BaseDir; // |all_source_engine_paths|, the folder holding the game executable
	std::string m_szGame;
	std::string m_szTitle;
	std::vector<SearchPath_t> m_SearchPaths;

	// baseDir defaults to the parent of the gameinfo folder, which holds for Valve's own games.
	// Source mods running on an SDK base need it passed in.
	[[nodiscard]] static std::optional<CGameInfo> Load( const std::filesystem::path &file, std::string *pError = nullptr, std::filesystem::path baseDir = {} );
	[[nodiscard]] static std::optional<CGameInfo> Parse( std::string_view szText, const std::filesystem::path &file, std::string *pError = nullptr, std::filesystem::path baseDir = {} );
};

class CMount
{
public:
	enum class Kind_t
	{
		KIND_DIRECTORY,
		KIND_VPK
	};

	CMount( Kind_t nKind, std::filesystem::path location, std::vector<std::string> ids );
	virtual ~CMount() = default;

	[[nodiscard]] Kind_t GetKind() const { return m_nKind; }
	[[nodiscard]] const std::filesystem::path &GetLocation() const { return m_Location; }
	[[nodiscard]] const std::vector<std::string> &GetIds() const { return m_Ids; }
	[[nodiscard]] bool HasId( std::string_view szId ) const;

	// Paths passed in must already be normalized.
	[[nodiscard]] virtual bool Contains( const std::string &szPath ) const = 0;
	[[nodiscard]] virtual std::optional<std::vector<std::byte>> Read( const std::string &szPath ) const = 0;
	virtual void ForEachFile( const std::function<void( const std::string &szPath )> &pfnCallback ) const = 0;
	[[nodiscard]] virtual std::size_t GetFileCount() const = 0;

	// The real file behind a path, for folder mounts; nullopt for files inside a VPK.
	[[nodiscard]] virtual std::optional<std::filesystem::path> DiskPath( const std::string &szPath ) const;

private:
	Kind_t m_nKind;
	std::filesystem::path m_Location;
	std::vector<std::string> m_Ids;
};

// One per search path after wildcard expansion, kept even when mounting failed so the UI can show why.
struct MountRecord_t
{
	enum class Status_t
	{
		STATUS_MOUNTED,
		STATUS_MISSING,
		STATUS_FAILED
	};

	std::string m_szSearchPath;          // the gameinfo.txt entry this came from
	std::filesystem::path m_Location;
	std::vector<std::string> m_Ids;
	Status_t m_nStatus = Status_t::STATUS_MOUNTED;
	std::string m_szMessage;
	const CMount *m_pMount = nullptr;
};

class CFileSystem
{
public:
	// Replaces whatever was mounted before.
	void Mount( const CGameInfo &info );
	void Clear();

	const CMount *AddDirectory( const std::filesystem::path &directory, std::vector<std::string> ids, std::string *pError = nullptr );
	const CMount *AddVpk( const std::filesystem::path &file, std::vector<std::string> ids, std::string *pError = nullptr );

	[[nodiscard]] const std::vector<MountRecord_t> &GetRecords() const { return m_Records; }

	[[nodiscard]] const CMount *Resolve( std::string_view szPath, std::string_view szPathId = "game" ) const;
	[[nodiscard]] std::optional<std::vector<std::byte>> Read( std::string_view szPath, std::string_view szPathId = "game" ) const;
	[[nodiscard]] std::optional<std::string> ReadText( std::string_view szPath, std::string_view szPathId = "game" ) const;

	// Every file visible under pathId, with the mount it is read from.
	[[nodiscard]] std::map<std::string, const CMount *> Files( std::string_view szPathId = "game" ) const;

	// Visible files under a directory with the given extension ("" for all), e.g. ("scripts", ".txt").
	[[nodiscard]] std::vector<std::string> Find( std::string_view szDirectory, std::string_view szExtension, bool bRecursive = false, std::string_view szPathId = "game" ) const;

private:
	void MountSearchPath( const CGameInfo &info, const SearchPath_t &searchPath );
	void Record( const SearchPath_t &searchPath, const std::filesystem::path &location, MountRecord_t::Status_t nStatus, std::string szMessage, const CMount *pMount );

	std::vector<std::unique_ptr<CMount>> m_Mounts;
	std::vector<MountRecord_t> m_Records;
};

// Resolve a path whose components may differ in case from what is on disk (gameinfo.txt
// is written for case-insensitive Windows). Returns the input unchanged if nothing matches.
[[nodiscard]] std::filesystem::path FindCaseInsensitive( const std::filesystem::path &path );

} // namespace xv::vfs
