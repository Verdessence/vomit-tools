//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's map manifests (.vmm, from Left 4 Dead's Hammer on): a list of submaps (each a
//          VMF) that make one map. Read as a map of its own whose submaps are func_instances,
//          so they are drawn in place, opened with Edit Instance and merged with Collapse.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xv::map
{

struct ManifestMap_t
{
	std::string m_szName;          // as the manifest's list shows it
	std::string m_szFile;          // as written, from the manifest's folder
	std::filesystem::path m_Path;  // found on disk (empty when it isn't there)
	int m_nId = 0;
	bool m_bTopLevel = false;      // the one whose world keys the map takes
};

// The submaps a manifest lists (Maps > VMF blocks). Files are looked for from the manifest's own
// folder, then from a folder beside it named as it is; problems go to pErrors.
[[nodiscard]] std::vector<ManifestMap_t> ReadManifest( std::string_view szText, const std::filesystem::path &manifest, std::vector<std::string> *pErrors = nullptr );
// A VMF holding a func_instance for each submap that was found (at the origin, names not fixed
// up), with the top-level submap's world keys (the first's when none is marked). Its "file"
// keys start from the manifest's folder.
[[nodiscard]] std::string ManifestToVmf( const std::vector<ManifestMap_t> &maps, const std::filesystem::path &manifest );

} // namespace xv::map
