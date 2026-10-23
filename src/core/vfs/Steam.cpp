//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Steam.h.
//
//===========================================================================//
#include "Steam.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>

#include "../kv1/Kv1.h"
#include "Vfs.h"

namespace xv::vfs
{

namespace fs = std::filesystem;

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Directory entries, or none if the folder can't be read; never throws.
//-----------------------------------------------------------------------------
std::vector<fs::directory_entry> EntriesOf( const fs::path &directory )
{
	std::vector<fs::directory_entry> entries;
	std::error_code ec;
	for ( fs::directory_iterator it{ directory, ec }; !ec && it != fs::directory_iterator{}; it.increment( ec ) )
	{
		entries.push_back( *it );
	}
	return entries;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
fs::path Normalized( const fs::path &path )
{
	std::error_code ec;
	const fs::path canonical = fs::weakly_canonical( path, ec );
	return ec ? path.lexically_normal() : canonical;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The library folders listed in <steamRoot>/steamapps/libraryfolders.vdf, starting with
//          steamRoot.
//-----------------------------------------------------------------------------
std::vector<fs::path> SteamLibraries( const fs::path &steamRoot )
{
	std::vector<fs::path> libraries{ Normalized( steamRoot ) };
	std::ifstream stream{ steamRoot / "steamapps" / "libraryfolders.vdf", std::ios::binary };
	if ( !stream )
	{
		return libraries;
	}
	const std::string szText{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
	// Windows paths in it are written with escaped backslashes.
	const auto parsed = kv1::Parse( szText, { .m_bEscapeSequences = true } );
	for ( const kv1::CNode &root : parsed.m_Document.m_Roots )
	{
		for ( const kv1::CNode &entry : root.GetChildren() )
		{
			std::string szPath;
			if ( entry.IsBlock() )
			{
				// Current format: "0" { "path" "..." ... }
				if ( const kv1::CNode *pValue = entry.Find( "path" ) )
				{
					szPath = pValue->GetValue();
				}
			}
			else if ( entry.IsPair() && !entry.GetKey().empty() && std::isdigit( static_cast<unsigned char>( entry.GetKey().front() ) ) )
			{
				// Older format: "1" "D:\\SteamLibrary"
				szPath = entry.GetValue();
			}
			if ( !szPath.empty() )
			{
				const fs::path library = Normalized( FromUtf8( szPath ) );
				if ( std::ranges::find( libraries, library ) == libraries.end() )
				{
					libraries.push_back( library );
				}
			}
		}
	}
	return libraries;
}

//-----------------------------------------------------------------------------
// Purpose: Every gameinfo.txt one folder deep inside each game in steamapps/common, plus source
//          mods.
//-----------------------------------------------------------------------------
std::vector<SteamGame_t> FindSourceGames( const std::vector<fs::path> &libraries )
{
	std::vector<SteamGame_t> games;
	std::set<fs::path> seen;
	const auto consider = [ & ]( const fs::path &gameInfo ) {
		std::error_code ec;
		if ( !fs::is_regular_file( gameInfo, ec ) || !seen.insert( Normalized( gameInfo ) ).second )
		{
			return;
		}
		if ( const auto info = CGameInfo::Load( gameInfo ) )
		{
			games.push_back( { info->m_szGame.empty() ? ToUtf8( gameInfo.parent_path().filename() ) : info->m_szGame, gameInfo } );
		}
	};
	for ( const fs::path &library : libraries )
	{
		for ( const auto &game : EntriesOf( library / "steamapps" / "common" ) )
		{
			for ( const auto &mod : EntriesOf( game.path() ) )
			{
				consider( mod.path() / "gameinfo.txt" );
			}
		}
		for ( const auto &mod : EntriesOf( library / "steamapps" / "sourcemods" ) )
		{
			consider( mod.path() / "gameinfo.txt" );
		}
	}
	std::ranges::sort( games, {}, &SteamGame_t::m_szName );
	return games;
}

} // namespace xv::vfs
