//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: vomit-tools-cli shader: a compiled shader file (.vcs) read, its combos listed and one
//          disassembled.
//
//===========================================================================//
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "shader/Bytecode.h"
#include "shader/Vcs.h"

int ShaderCommand( int argc, char **argv );

namespace
{

constexpr const char *SHADER_USAGE = "vomit-tools-cli shader <file.vcs> [static dynamic]   the header and a combo disassembled (the first, else static dynamic)\n"
									 "vomit-tools-cli shader <file.vcs> --list             every combo, with its profile and size\n";

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int ShaderCommand( int argc, char **argv )
{
	if ( argc < 1 || std::string_view( argv[ 0 ] ) == "--help" )
	{
		std::printf( "%s", SHADER_USAGE );
		return argc < 1 ? 1 : 0;
	}
	std::ifstream in( argv[ 0 ], std::ios::binary );
	if ( !in )
	{
		std::fprintf( stderr, "can't open %s\n", argv[ 0 ] );
		return 1;
	}
	const std::string szBytes{ std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
	const std::vector<std::byte> bytes( reinterpret_cast<const std::byte *>( szBytes.data() ), reinterpret_cast<const std::byte *>( szBytes.data() ) + szBytes.size() );
	std::string szError;
	const auto file = xv::shader::ReadVcs( bytes, &szError );
	if ( !file )
	{
		std::fprintf( stderr, "%s: %s\n", argv[ 0 ], szError.c_str() );
		return 1;
	}
	std::printf( "version %d, %zu combos built (%d dynamic a static), source CRC %08x\n", file->m_nVersion, file->m_Combos.size(), file->m_nDynamicCombos, file->m_nSourceCrc );
	if ( file->m_Combos.empty() )
		return 0;
	if ( argc > 1 && std::string_view( argv[ 1 ] ) == "--list" )
	{
		for ( const xv::shader::VcsCombo_t &combo : file->m_Combos )
		{
			const auto program = xv::shader::ReadBytecode( combo.m_Tokens );
			std::printf( "%u %u: %s, %zu instructions\n", combo.m_nStatic, combo.m_nDynamic, program ? program->Profile().c_str() : "unreadable", program ? program->m_Instructions.size() : 0 );
		}
		return 0;
	}
	const xv::shader::VcsCombo_t *pCombo = argc > 2 ? file->Find( static_cast<std::uint32_t>( std::strtoul( argv[ 1 ], nullptr, 10 ) ), static_cast<std::uint32_t>( std::strtoul( argv[ 2 ], nullptr, 10 ) ) ) : &file->m_Combos.front();
	if ( !pCombo )
	{
		std::fprintf( stderr, "no combo %s %s\n", argv[ 1 ], argv[ 2 ] );
		return 1;
	}
	const auto program = xv::shader::ReadBytecode( pCombo->m_Tokens, &szError );
	if ( !program )
	{
		std::fprintf( stderr, "combo %u %u: %s\n", pCombo->m_nStatic, pCombo->m_nDynamic, szError.c_str() );
		return 1;
	}
	std::printf( "combo %u %u\n%s", pCombo->m_nStatic, pCombo->m_nDynamic, xv::shader::Disassemble( *program ).c_str() );
	return 0;
}
