//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: One archive opened on its own, apart from any mounted game: a VPK (its _dir file or any
//          of its numbered parts), a map's packed files (.bsp), or a GCF, GMA, PAK, WAD, XZP or
//          ZIP. Lists, reads and extracts.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vpkpp
{
class PackFile;
}

namespace xv::bsp
{
class CBsp;
}

namespace xv::vfs
{

struct PackageEntry_t
{
	std::string m_szPath;    // as stored in the archive, with forward slashes
	std::uint64_t m_nSize = 0;
	std::uint32_t m_nCrc = 0;
	// The numbered VPK part that holds the data (pak01_003.vpk is 3); -1 when it sits in the
	// archive itself.
	int m_nArchive = -1;
	// Another entry has the same path. Repacked maps can hold a file twice with different data;
	// each entry still reads its own.
	bool m_bDuplicate = false;
	std::size_t m_nStored = 0; // the archive's own index for it (a map's zip entry)
};

class CPackage
{
public:
	~CPackage();

	// Opens the archive at file; nullptr (with the reason in pError) when it can't be read.
	[[nodiscard]] static std::unique_ptr<CPackage> Open( const std::filesystem::path &file, std::string *pError = nullptr );
	// Whether the file's extension is one of the archives Open reads.
	[[nodiscard]] static bool IsPackageFile( const std::filesystem::path &file );

	// The file that was opened: for a VPK, its _dir file even when a numbered part was picked.
	[[nodiscard]] const std::filesystem::path &GetFile() const { return m_File; }
	// The map, when a .bsp was opened; null otherwise.
	[[nodiscard]] const bsp::CBsp *GetBsp() const { return m_pBsp.get(); }
	// "VPK 2", "BSP 20", "GCF", "ZIP"...
	[[nodiscard]] const std::string &GetFormat() const { return m_szFormat; }
	// Every file, sorted by path.
	[[nodiscard]] const std::vector<PackageEntry_t> &GetEntries() const { return m_Entries; }
	[[nodiscard]] std::uint64_t GetTotalSize() const { return m_nTotalSize; }
	// The entry at a path in any case and with either slash; null when there's none.
	[[nodiscard]] const PackageEntry_t *Find( std::string_view szPath ) const;

	// Paths are as GetEntries gives them, but any case and either slash work.
	// With duplicate paths, the first in the archive's order is read; Read( entry ) reads any.
	[[nodiscard]] std::optional<std::vector<std::byte>> Read( std::string_view szPath ) const;
	[[nodiscard]] std::optional<std::vector<std::byte>> Read( const PackageEntry_t &entry ) const;
	// Writes one file out to target (its folders are made as needed).
	bool Extract( std::string_view szPath, const std::filesystem::path &target, std::string *pError = nullptr ) const;
	// Writes each file under folder, keeping the archive's folders below stripPrefix ("" keeps
	// all of them). pfnProgress, when set, is told after each file and stops the run by returning
	// false. Returns how many files were written; the failures go in pFailed.
	std::size_t ExtractAll( const std::vector<std::string> &paths, const std::filesystem::path &folder, std::string_view szStripPrefix = {},
		const std::function<bool( std::size_t nDone )> &pfnProgress = {}, std::vector<std::string> *pFailed = nullptr ) const;

private:
	struct Zip_t;

	CPackage();
	[[nodiscard]] static std::unique_ptr<CPackage> OpenMap( const std::filesystem::path &file, std::string *pError );
	void BuildIndex();

	std::unique_ptr<vpkpp::PackFile> m_pPack; // or, for a map:
	std::unique_ptr<Zip_t> m_pZip;
	std::unique_ptr<bsp::CBsp> m_pBsp;
	std::filesystem::path m_File;
	std::string m_szFormat;
	std::vector<PackageEntry_t> m_Entries;
	std::unordered_map<std::string, std::size_t> m_Index; // NormalizePath -> m_Entries index
	std::uint64_t m_nTotalSize = 0;
};

} // namespace xv::vfs
