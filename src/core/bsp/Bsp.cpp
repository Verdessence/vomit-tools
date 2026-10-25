//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Bsp.h.
//
//===========================================================================//
#include "Bsp.h"

#include <cstring>
#include <fstream>

#include <lzma.h>

#include "vfs/Vfs.h"

namespace fs = std::filesystem;

namespace xv::bsp
{

namespace
{

constexpr std::uint32_t BSP_IDENT = 'V' | ( 'B' << 8 ) | ( 'S' << 16 ) | ( 'P' << 24 );
// Ident, version, 64 lumps of four 32-bit fields, map revision.
constexpr std::size_t HEADER_SIZE = 4 + 4 + LUMP_COUNT * 16 + 4;

constexpr std::array<std::string_view, LUMP_COUNT> LUMP_NAMES{
	"Entities", "Planes", "Texture Data", "Vertices", "Visibility", "Nodes", "Texture Info", "Faces",
	"Lighting", "Occlusion", "Leaves", "Face IDs", "Edges", "Surface Edges", "Models", "World Lights",
	"Leaf Faces", "Leaf Brushes", "Brushes", "Brush Sides", "Areas", "Area Portals", "Portals", "Clusters",
	"Portal Vertices", "Cluster Portals", "Displacement Info", "Original Faces", "Physics Displacements", "Physics Collision", "Vertex Normals", "Vertex Normal Indices",
	"Displacement Lightmap Alphas", "Displacement Vertices", "Displacement Lightmap Sample Positions", "Game Lump", "Leaf Water Data", "Primitives", "Primitive Vertices", "Primitive Indices",
	"Pakfile", "Clip Portal Vertices", "Cubemaps", "Texture Names", "Texture Name Table", "Overlays", "Leaf Distance to Water", "Face Macro Texture Info",
	"Displacement Triangles", "Physics Collision Surface", "Water Overlays", "Leaf Ambient Index (HDR)", "Leaf Ambient Index", "Lighting (HDR)", "World Lights (HDR)", "Leaf Ambient Lighting (HDR)",
	"Leaf Ambient Lighting", "Xbox Pakfile", "Faces (HDR)", "Map Flags", "Overlay Fades", "Overlay System Levels", "Physics Level", "Displacement Multiblend"
};

//-----------------------------------------------------------------------------
// Purpose: A little-endian 32-bit value at an offset the caller has checked.
//-----------------------------------------------------------------------------
std::uint32_t Read32( std::span<const std::byte> bytes, std::size_t nAt )
{
	std::uint32_t n = 0;
	std::memcpy( &n, bytes.data() + nAt, 4 );
	return n;
}

//-----------------------------------------------------------------------------
// Purpose: Reads the lump table with the fields in the given order; false when a lump lies
//          outside the file, which means the order is wrong (or the file is cut short).
//-----------------------------------------------------------------------------
bool ReadLumps( std::span<const std::byte> bytes, bool bVersionFirst, std::array<Lump_t, LUMP_COUNT> &lumps )
{
	for ( int i = 0; i < LUMP_COUNT; i++ )
	{
		const std::size_t nAt = 8 + static_cast<std::size_t>( i ) * 16;
		Lump_t &lump = lumps[ static_cast<std::size_t>( i ) ];
		lump.m_nIndex = i;
		lump.m_nOffset = Read32( bytes, nAt + ( bVersionFirst ? 4 : 0 ) );
		lump.m_nLength = Read32( bytes, nAt + ( bVersionFirst ? 8 : 4 ) );
		lump.m_nVersion = Read32( bytes, nAt + ( bVersionFirst ? 0 : 8 ) );
		lump.m_nUncompressed = Read32( bytes, nAt + 12 );
		if ( lump.m_nLength != 0 && ( lump.m_nOffset < HEADER_SIZE || static_cast<std::uint64_t>( lump.m_nOffset ) + lump.m_nLength > bytes.size() ) )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A compressed lump: "LZMA", its size unpacked, its size packed and the 5 LZMA property
//          bytes, then the raw stream. Rewritten with the .lzma file header (properties, then
//          the unpacked size as 64 bits) that liblzma's decoder reads.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> Decompress( std::span<const std::byte> lump )
{
	constexpr std::size_t VALVE_HEADER = 17;
	constexpr std::uint32_t LZMA_IDENT = 'L' | ( 'Z' << 8 ) | ( 'M' << 16 ) | ( 'A' << 24 );
	if ( lump.size() < VALVE_HEADER || Read32( lump, 0 ) != LZMA_IDENT )
	{
		return std::nullopt;
	}
	return UnpackLzma( lump.subspan( 12, 5 ), lump.subspan( VALVE_HEADER ), Read32( lump, 4 ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Valve's streams often have no end marker, so the unpacked size is what stops the
//          decoder; it goes in the .lzma file header (properties, then that size as 64 bits)
//          that liblzma's decoder reads.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> UnpackLzma( std::span<const std::byte> properties, std::span<const std::byte> stream, std::uint64_t nUnpacked )
{
	if ( properties.size() != 5 )
	{
		return std::nullopt;
	}
	std::vector<std::uint8_t> input( 13 );
	std::memcpy( input.data(), properties.data(), 5 );
	std::memcpy( input.data() + 5, &nUnpacked, 8 );
	input.insert( input.end(), reinterpret_cast<const std::uint8_t *>( stream.data() ), reinterpret_cast<const std::uint8_t *>( stream.data() ) + stream.size() );

	std::vector<std::byte> out( nUnpacked );
	lzma_stream decoder = LZMA_STREAM_INIT;
	if ( lzma_alone_decoder( &decoder, UINT64_MAX ) != LZMA_OK )
	{
		return std::nullopt;
	}
	decoder.next_in = input.data();
	decoder.avail_in = input.size();
	decoder.next_out = reinterpret_cast<std::uint8_t *>( out.data() );
	decoder.avail_out = out.size();
	const lzma_ret nResult = lzma_code( &decoder, LZMA_FINISH );
	const bool bWhole = ( nResult == LZMA_STREAM_END || nResult == LZMA_OK ) && decoder.avail_out == 0;
	lzma_end( &decoder );
	return bWhole ? std::optional( std::move( out ) ) : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view Entity_t::Get( std::string_view szKey ) const
{
	for ( const auto &[ key, value ] : m_KeyValues )
	{
		if ( key == szKey )
		{
			return value;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string LumpName( int nIndex )
{
	if ( nIndex >= 0 && nIndex < LUMP_COUNT )
	{
		return std::string( LUMP_NAMES[ static_cast<std::size_t>( nIndex ) ] );
	}
	return "Lump " + std::to_string( nIndex );
}

//-----------------------------------------------------------------------------
// Purpose: Blocks of quoted key and value pairs, ended by a zero byte. Values have no escapes:
//          outputs separate their parts with commas or an escape character instead.
//-----------------------------------------------------------------------------
std::vector<Entity_t> ParseEntities( std::string_view szText )
{
	std::vector<Entity_t> entities;
	Entity_t *pCurrent = nullptr;
	std::string szKey;
	bool bHaveKey = false;
	for ( std::size_t i = 0; i < szText.size() && szText[ i ] != '\0'; i++ )
	{
		const char ch = szText[ i ];
		if ( ch == '{' )
		{
			pCurrent = &entities.emplace_back();
			bHaveKey = false;
		}
		else if ( ch == '}' )
		{
			pCurrent = nullptr;
		}
		else if ( ch == '"' )
		{
			const std::size_t nEnd = szText.find( '"', i + 1 );
			if ( nEnd == std::string_view::npos )
			{
				break;
			}
			std::string szToken( szText.substr( i + 1, nEnd - i - 1 ) );
			i = nEnd;
			if ( !pCurrent )
			{
				continue;
			}
			if ( !bHaveKey )
			{
				szKey = std::move( szToken );
				bHaveKey = true;
			}
			else
			{
				pCurrent->m_KeyValues.emplace_back( std::move( szKey ), std::move( szToken ) );
				bHaveKey = false;
			}
		}
	}
	return entities;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CBsp> CBsp::Load( const fs::path &file, std::string *pError )
{
	std::ifstream in( file, std::ios::binary );
	if ( !in )
	{
		if ( pError )
		{
			*pError = "could not open " + vfs::ToUtf8( file );
		}
		return std::nullopt;
	}
	std::vector<char> data( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
	return Parse( std::as_bytes( std::span( data ) ), pError );
}

//-----------------------------------------------------------------------------
// Purpose: Left 4 Dead 2's version 21 maps put each lump's version before its offset; the order
//          that keeps every lump inside the file is the one used.
//-----------------------------------------------------------------------------
std::optional<CBsp> CBsp::Parse( std::span<const std::byte> bytes, std::string *pError )
{
	const auto fail = [ & ]( const char *pszWhy ) -> std::optional<CBsp> {
		if ( pError )
		{
			*pError = pszWhy;
		}
		return std::nullopt;
	};
	if ( bytes.size() < HEADER_SIZE || Read32( bytes, 0 ) != BSP_IDENT )
	{
		return fail( "not a Source map (no VBSP header)" );
	}
	CBsp bsp;
	bsp.m_nVersion = static_cast<int>( Read32( bytes, 4 ) );
	bsp.m_nRevision = static_cast<int>( Read32( bytes, HEADER_SIZE - 4 ) );
	if ( !ReadLumps( bytes, false, bsp.m_Lumps ) && !ReadLumps( bytes, true, bsp.m_Lumps ) )
	{
		return fail( "the map's lump table points outside the file; it may be cut short" );
	}

	// A lump's bytes, unpacked when compressed; nullopt when that fails.
	const auto lumpBytes = [ & ]( int nLump ) -> std::optional<std::vector<std::byte>> {
		const Lump_t &lump = bsp.m_Lumps[ static_cast<std::size_t>( nLump ) ];
		const auto stored = bytes.subspan( lump.m_nOffset, lump.m_nLength );
		if ( lump.m_nUncompressed != 0 )
		{
			return Decompress( stored );
		}
		return std::vector<std::byte>( stored.begin(), stored.end() );
	};
	if ( const auto text = lumpBytes( LUMP_ENTITIES ) )
	{
		bsp.m_Entities = ParseEntities( std::string_view( reinterpret_cast<const char *>( text->data() ), text->size() ) );
		bsp.m_bEntitiesRead = true;
	}
	if ( auto zip = lumpBytes( LUMP_PAKFILE ) )
	{
		bsp.m_Pakfile = std::move( *zip );
	}
	return bsp;
}

} // namespace xv::bsp
