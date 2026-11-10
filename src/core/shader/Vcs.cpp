//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Vcs.h. The layout was read from the files themselves and the SDK's header of
//          their structures (shader_vcs_version.h).
//
//===========================================================================//
#include "Vcs.h"

#include <algorithm>
#include <cstring>
#include <format>

#include "bsp/Bsp.h"

namespace xv::shader
{

namespace
{

constexpr std::size_t HEADER_SIZE = 28;
constexpr std::uint32_t SENTINEL = 0xFFFFFFFF;
// A block's word: its size, and whether it is LZMA packed or stored as it is.
constexpr std::uint32_t BLOCK_LZMA = 0x40000000;
constexpr std::uint32_t BLOCK_STORED = 0x80000000;
constexpr std::uint32_t BLOCK_SIZE_MASK = 0x3FFFFFFF;
constexpr std::uint32_t LZMA_IDENT = 'L' | ( 'Z' << 8 ) | ( 'M' << 16 ) | ( 'A' << 24 );
// No combo's bytecode is bigger than this; past it the file is broken.
constexpr std::size_t MOST_BYTECODE = 1u << 20;
constexpr std::size_t MOST_COMBOS = 1u << 20;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> Read32( std::span<const std::byte> bytes, std::size_t nAt )
{
	if ( nAt > bytes.size() || bytes.size() - nAt < 4 )
		return std::nullopt;
	std::uint32_t n = 0;
	std::memcpy( &n, bytes.data() + nAt, 4 );
	return n;
}

//-----------------------------------------------------------------------------
// Purpose: Bytes as the tokens they hold (a whole number of them).
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> TokensOf( std::span<const std::byte> bytes )
{
	std::vector<std::uint32_t> tokens( bytes.size() / 4 );
	std::memcpy( tokens.data(), bytes.data(), tokens.size() * 4 );
	return tokens;
}

//-----------------------------------------------------------------------------
// Purpose: A static combo's blocks one after another, each unpacked, up to the sentinel.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> ReadBlocks( std::span<const std::byte> bytes, std::size_t nAt, std::string &szError )
{
	std::vector<std::byte> out;
	for ( ;; )
	{
		const auto word = Read32( bytes, nAt );
		if ( !word )
		{
			szError = "a block runs past the end";
			return std::nullopt;
		}
		nAt += 4;
		if ( *word == SENTINEL )
			return out;
		const std::size_t nSize = *word & BLOCK_SIZE_MASK;
		if ( nSize > bytes.size() - nAt )
		{
			szError = "a block runs past the end";
			return std::nullopt;
		}
		const std::span<const std::byte> block = bytes.subspan( nAt, nSize );
		nAt += nSize;
		if ( ( *word & BLOCK_STORED ) != 0 )
		{
			out.insert( out.end(), block.begin(), block.end() );
			continue;
		}
		if ( ( *word & BLOCK_LZMA ) == 0 )
		{
			szError = std::format( "a block packed in a way this doesn't read ({:#010x})", *word );
			return std::nullopt;
		}
		const auto ident = Read32( block, 0 );
		const auto unpacked = Read32( block, 4 );
		if ( !ident || *ident != LZMA_IDENT || !unpacked || block.size() < 17 || *unpacked > MOST_BYTECODE * 4 )
		{
			szError = "an LZMA block without its header";
			return std::nullopt;
		}
		const auto unpackedBytes = bsp::UnpackLzma( block.subspan( 12, 5 ), block.subspan( 17 ), *unpacked );
		if ( !unpackedBytes )
		{
			szError = "an LZMA block that doesn't unpack";
			return std::nullopt;
		}
		out.insert( out.end(), unpackedBytes->begin(), unpackedBytes->end() );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const VcsCombo_t *VcsFile_t::Find( std::uint32_t nStatic, std::uint32_t nDynamic ) const
{
	for ( const auto &[ nThis, nSource ] : m_Aliases )
	{
		if ( nThis == nStatic )
		{
			nStatic = nSource;
			break;
		}
	}
	const auto found = std::ranges::find_if( m_Combos, [ & ]( const VcsCombo_t &combo ) { return combo.m_nStatic == nStatic && combo.m_nDynamic == nDynamic; } );
	return found != m_Combos.end() ? &*found : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The header, then (version 5 and 6) the static combos' places and their aliases, each
//          static combo's blocks holding its dynamic combos as (number, size, bytecode); or
//          (version 4) a dictionary of every combo's place and size.
//-----------------------------------------------------------------------------
std::optional<VcsFile_t> ReadVcs( std::span<const std::byte> bytes, std::string *pError )
{
	std::string szError;
	const auto fail = [ & ]( std::string szWhy ) -> std::optional<VcsFile_t> {
		if ( pError )
			*pError = std::move( szWhy );
		return std::nullopt;
	};
	if ( bytes.size() < HEADER_SIZE )
		return fail( "too short for a .vcs header" );
	VcsFile_t file;
	file.m_nVersion = static_cast<int>( *Read32( bytes, 0 ) );
	file.m_nTotalCombos = static_cast<int>( *Read32( bytes, 4 ) );
	file.m_nDynamicCombos = static_cast<int>( *Read32( bytes, 8 ) );
	file.m_nFlags = *Read32( bytes, 12 );
	file.m_nCentroidMask = *Read32( bytes, 16 );
	file.m_nSourceCrc = *Read32( bytes, 24 );
	if ( file.m_nVersion == 4 )
	{
		// Every combo's place and size, in combo order (dynamic numbers first).
		// The header counts every combo the options make (most never built); only the dictionary's
		// are read.
		const std::size_t nCombos = static_cast<std::uint32_t>( file.m_nTotalCombos );
		if ( nCombos > ( bytes.size() - HEADER_SIZE ) / 8 )
			return fail( "the dictionary runs past the end" );
		const std::uint32_t nDynamic = static_cast<std::uint32_t>( std::max( 1, file.m_nDynamicCombos ) );
		for ( std::size_t i = 0; i < nCombos; i++ )
		{
			const std::uint32_t nOffset = *Read32( bytes, HEADER_SIZE + i * 8 );
			const std::uint32_t nSize = *Read32( bytes, HEADER_SIZE + i * 8 + 4 );
			if ( nOffset == SENTINEL || nSize == 0 )
				continue;
			if ( nOffset > bytes.size() || nSize > bytes.size() - nOffset || nSize > MOST_BYTECODE )
				return fail( std::format( "combo {} runs past the end", i ) );
			file.m_Combos.push_back( { static_cast<std::uint32_t>( i ) / nDynamic, static_cast<std::uint32_t>( i ) % nDynamic, TokensOf( bytes.subspan( nOffset, nSize ) ) } );
		}
		return file;
	}
	if ( file.m_nVersion != 5 && file.m_nVersion != 6 )
		return fail( std::format( "version {} isn't one this reads (4, 5 or 6)", file.m_nVersion ) );
	const std::uint32_t nStatics = *Read32( bytes, 20 );
	if ( nStatics > MOST_COMBOS || nStatics > ( bytes.size() - HEADER_SIZE ) / 8 )
		return fail( "the static combos run past the end" );
	std::vector<std::pair<std::uint32_t, std::uint32_t>> statics;
	for ( std::uint32_t i = 0; i < nStatics; i++ )
		statics.emplace_back( *Read32( bytes, HEADER_SIZE + i * 8 ), *Read32( bytes, HEADER_SIZE + i * 8 + 4 ) );
	// Version 6 lists the static combos that are another's after them; 5 has no such list.
	std::size_t nAt = HEADER_SIZE + nStatics * 8;
	if ( file.m_nVersion == 6 )
	{
		const auto nAliases = Read32( bytes, nAt );
		if ( !nAliases || *nAliases > MOST_COMBOS || *nAliases > ( bytes.size() - nAt - 4 ) / 8 )
			return fail( "the aliases run past the end" );
		nAt += 4;
		for ( std::uint32_t i = 0; i < *nAliases; i++ )
			file.m_Aliases.emplace_back( *Read32( bytes, nAt + i * 8 ), *Read32( bytes, nAt + i * 8 + 4 ) );
	}
	for ( const auto &[ nStatic, nOffset ] : statics )
	{
		if ( nStatic == SENTINEL )
			continue; // the end of the list (its offset is the file's end)
		if ( nOffset >= bytes.size() )
			return fail( std::format( "static combo {} is past the end", nStatic ) );
		const auto stream = ReadBlocks( bytes, nOffset, szError );
		if ( !stream )
			return fail( std::format( "static combo {}: {}", nStatic, szError ) );
		const std::span<const std::byte> records( *stream );
		std::size_t nRecord = 0;
		while ( nRecord + 8 <= records.size() )
		{
			const std::uint32_t nDynamic = *Read32( records, nRecord );
			if ( nDynamic == SENTINEL )
				break;
			const std::uint32_t nSize = *Read32( records, nRecord + 4 );
			nRecord += 8;
			if ( nSize > records.size() - nRecord || nSize > MOST_BYTECODE )
				return fail( std::format( "static combo {}, dynamic combo {}, runs past its blocks", nStatic, nDynamic ) );
			file.m_Combos.push_back( { nStatic, nDynamic, TokensOf( records.subspan( nRecord, nSize ) ) } );
			nRecord += nSize;
		}
	}
	return file;
}

} // namespace xv::shader
