//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Source VMF prefab libraries and placement, retaining the prefab's origin as the
//          insertion reference and allocating fresh object, face and group IDs.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <span>

#include "map/Edit.h"

namespace xv::map
{
struct Prefab_t
{
	std::string m_szName;
	std::filesystem::path m_File;
};
struct PrefabLibrary_t
{
	std::string m_szName;
	std::vector<Prefab_t> m_Prefabs;
};

// The game's own folder takes precedence over the folders beside Hammer's bin/x64 installs.
[[nodiscard]] std::vector<std::filesystem::path> PrefabFolders( const std::filesystem::path &gameDir );
// One library per subfolder; same-name entries in later roots are shadowed by earlier ones.
[[nodiscard]] std::vector<PrefabLibrary_t> FindPrefabs( std::span<const std::filesystem::path> folders );
// Selection as a VMF marked as a prefab; its original map origin remains the insertion anchor.
[[nodiscard]] std::string SelectionToPrefab( const CScene &scene, const std::vector<std::uint32_t> &objects );
// A prefab keeps its size and ordinary names; explicit &i tokens share a free number per copy.
[[nodiscard]] MapEdit_t PlacePrefab( const CScene &scene, const std::filesystem::path &file, Point_t at,
	std::vector<std::uint32_t> *pAdded, std::string *pError = nullptr );

} // namespace xv::map
