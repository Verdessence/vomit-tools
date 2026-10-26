//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Reads Hammer's map files (.vmf) into map::CMap. Broken parts are reported and kept,
//          never dropped: a map that Hammer can't open still shows what it holds.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "map/Map.h"

namespace xv::map
{

struct ReadOptions_t
{
	std::size_t m_nMaxDiagnostics = 1000;
};

// nullopt only when the text can't be a map at all (too big to index, or no blocks in it).
[[nodiscard]] std::optional<CMap> ReadVmf( std::vector<char> text, const ReadOptions_t &options = {}, std::string *pError = nullptr );
[[nodiscard]] std::optional<CMap> ReadVmfFile( const std::filesystem::path &path, const ReadOptions_t &options = {}, std::string *pError = nullptr );

// The name a material is filed under: lower case, '/' between folders.
[[nodiscard]] std::string NormalizeMaterial( std::string_view szName );

} // namespace xv::map
