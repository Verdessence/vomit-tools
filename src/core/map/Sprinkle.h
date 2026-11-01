//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Sprinkle tool (CS:GO's Hammer, Hammer++): entities scattered on a grid over the
//          surfaces under a brush, from the definition files in scripts/hammer/sprinkle and
//          Hammer++'s own scripts/sprinkle.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "map/Edit.h"

namespace xv::map
{

using SprinkleKeys_t = std::vector<std::pair<std::string, std::string>>;

// One numbered block of a definition: its keys, picked with a chance of its weight over the sum.
struct SprinkleCase_t
{
	double m_flWeight = 1.0;
	SprinkleKeys_t m_Keys;
};

// One definition: what the Type box lists.
struct SprinkleType_t
{
	std::string m_szName;
	std::filesystem::path m_File;
	SprinkleKeys_t m_Base;                 // every entity's keys ("grid" taken out)
	std::array<double, 2> m_Grid{ 64, 64 }; // the definition's spacing
	std::vector<SprinkleCase_t> m_Cases;

	[[nodiscard]] std::string_view GetClassname() const;
};

// The definitions in one file (a file may hold several). Problems go to pErrors, one a line.
[[nodiscard]] std::vector<SprinkleType_t> ParseSprinkleTypes( std::string_view szText, const std::filesystem::path &file, std::vector<std::string> *pErrors = nullptr );
// The game's scripts/hammer/sprinkle, then Hammer++'s beside the game's bin folders.
[[nodiscard]] std::vector<std::filesystem::path> SprinkleFolders( const std::filesystem::path &gameDir );
// Every definition in the folders' .txt files, by name; a name an earlier folder gives wins.
[[nodiscard]] std::vector<SprinkleType_t> FindSprinkleTypes( std::span<const std::filesystem::path> folders, std::vector<std::string> *pErrors = nullptr );

// Whether entity could have come from type: its class and every key of one of its cases. The
// tool only takes away or replaces what it could have made.
[[nodiscard]] bool MatchesSprinkleType( const Entity_t &entity, const SprinkleType_t &type );

// The Mode box.
enum class SprinkleMode_t
{
	SPRINKLE_MODE_ADDITIVE,    // new ones in the grid's cells, each taking the place of what is there
	SPRINKLE_MODE_SUBTRACTIVE, // what the tool made in the radius taken away
	SPRINKLE_MODE_REPLACE,     // the radius cleared, then filled
	SPRINKLE_MODE_OVERWRITE,   // only cells that already hold one get a new one
};

struct SprinkleOptions_t
{
	SprinkleMode_t m_nMode = SprinkleMode_t::SPRINKLE_MODE_ADDITIVE;
	double m_flDensity = 1.0;             // 0 to 1: each cell's chance
	std::optional<std::array<double, 2>> m_Grid; // the definition's when empty
	bool m_bRandomYaw = true;
	double m_flOffset = 0.0;              // out from the surface (Garry's Mod's Item Offset)
	std::uint32_t m_nSeed = 0;
};

// What the brush's rays hit: the point and the surface's normal. Nothing for a miss.
using SprinkleTrace_t = std::function<std::optional<std::pair<Point_t, Point_t>>( Point_t from, Point_t dir, double flMax )>;

// One stroke of the tool, from the press to the release: dabs gather in it, against the scene as
// it was when the stroke began, and it gives the stroke's one edit. The grid is the world's, on
// the plane the surface faces most, so strokes over the same place find the same cells.
class CSprinkleStroke
{
public:
	CSprinkleStroke( std::shared_ptr<const CScene> pScene, SprinkleType_t type, SprinkleOptions_t options, SprinkleTrace_t pfnTrace );

	// A dab at the point under the cursor, on a surface facing normal. nMode overrides the
	// options' (Ctrl held: subtractive). True if it changed what the stroke does.
	bool Dab( Point_t at, Point_t normal, double flRadius, std::optional<SprinkleMode_t> nMode = std::nullopt );
	// The stroke's edit against the scene it began on: what it takes away, then what it adds.
	[[nodiscard]] MapEdit_t GetEdit( std::vector<std::uint32_t> *pAdded = nullptr ) const;
	[[nodiscard]] bool IsEmpty() const { return m_Cells.empty(); }
	[[nodiscard]] const CScene &GetScene() const { return *m_pScene; }
	[[nodiscard]] std::size_t GetAddedCount() const;
	[[nodiscard]] std::size_t GetRemovedCount() const;

private:
	using Cell_t = std::tuple<int, long long, long long>; // facing axis, then the cell along the other two

	struct Planned_t
	{
		std::vector<std::uint32_t> m_Removed; // entities of the scene
		std::optional<Entity_t> m_Added;
	};

	[[nodiscard]] Entity_t MakeEntity( const Cell_t &cell, Point_t origin, Point_t normal ) const;
	[[nodiscard]] std::array<double, 2> GetGrid() const;

	std::shared_ptr<const CScene> m_pScene;
	SprinkleType_t m_Type;
	SprinkleOptions_t m_Options;
	SprinkleTrace_t m_pfnTrace;
	std::array<std::map<std::pair<long long, long long>, std::vector<std::uint32_t>>, 3> m_Existing; // by facing axis
	std::map<Cell_t, Planned_t> m_Cells;
};

} // namespace xv::map
