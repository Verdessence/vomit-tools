//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Compiled maps (.bsp): the header's lump table, the entities, and the pakfile lump (the
//          zip of files packed into the map).
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xv::bsp
{

constexpr int LUMP_COUNT = 64;
constexpr int LUMP_ENTITIES = 0;
constexpr int LUMP_PAKFILE = 40;

struct Lump_t
{
	int m_nIndex = 0;
	std::uint32_t m_nOffset = 0;
	std::uint32_t m_nLength = 0;
	std::uint32_t m_nVersion = 0;
	// Nonzero when the lump is LZMA-compressed: its size once uncompressed.
	std::uint32_t m_nUncompressed = 0;
};

struct Entity_t
{
	std::vector<std::pair<std::string, std::string>> m_KeyValues; // in file order; keys may repeat (outputs)

	// The first value of a key; empty when it's missing.
	[[nodiscard]] std::string_view Get( std::string_view szKey ) const;
};

class CBsp
{
public:
	// nullopt (with the reason in pError) when the file isn't a map this can read.
	[[nodiscard]] static std::optional<CBsp> Load( const std::filesystem::path &file, std::string *pError = nullptr );
	[[nodiscard]] static std::optional<CBsp> Parse( std::span<const std::byte> bytes, std::string *pError = nullptr );

	[[nodiscard]] int GetVersion() const { return m_nVersion; }
	[[nodiscard]] int GetRevision() const { return m_nRevision; }
	[[nodiscard]] const std::array<Lump_t, LUMP_COUNT> &GetLumps() const { return m_Lumps; }
	[[nodiscard]] const std::vector<Entity_t> &GetEntities() const { return m_Entities; }
	// False when the entity lump couldn't be read (it's compressed and wouldn't unpack).
	[[nodiscard]] bool HasEntities() const { return m_bEntitiesRead; }
	// The pakfile lump's zip (unpacked when compressed); empty when the map packs nothing.
	[[nodiscard]] const std::vector<std::byte> &GetPakfile() const { return m_Pakfile; }

private:
	int m_nVersion = 0;
	int m_nRevision = 0;
	std::array<Lump_t, LUMP_COUNT> m_Lumps{};
	std::vector<Entity_t> m_Entities;
	bool m_bEntitiesRead = false;
	std::vector<std::byte> m_Pakfile;
};

// The lump's name ("Entities", "Pakfile"...), by index; numbered for unknown ones.
[[nodiscard]] std::string LumpName( int nIndex );
// Raw LZMA data with its 5 property bytes, unpacked to its known size (compressed lumps, and
// the LZMA entries of a map's pakfile). nullopt when it doesn't unpack to exactly that size.
[[nodiscard]] std::optional<std::vector<std::byte>> UnpackLzma( std::span<const std::byte> properties, std::span<const std::byte> stream, std::uint64_t nUnpacked );
// The entity lump's text as entities. Unterminated text gives what was read up to there.
[[nodiscard]] std::vector<Entity_t> ParseEntities( std::string_view szText );

} // namespace xv::bsp
