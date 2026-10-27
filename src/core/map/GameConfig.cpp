//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See GameConfig.h.
//
//===========================================================================//
#include "GameConfig.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>

#include "kv1/Kv1.h"
#include "util/Numbers.h"

namespace xv::map
{

namespace fs = std::filesystem;

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string LowerText( std::string sz )
{
	for ( char &ch : sz )
	{
		if ( ch >= 'A' && ch <= 'Z' )
		{
			ch = static_cast<char>( ch - 'A' + 'a' );
		}
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: The rest of path's parts from index nPart, under current.
//-----------------------------------------------------------------------------
std::optional<fs::path> Resolve( const fs::path &current, const std::vector<fs::path> &parts, std::size_t nPart, int nBudget )
{
	std::error_code error;
	if ( nPart == parts.size() )
	{
		return fs::exists( current, error ) ? std::optional<fs::path>{ current } : std::nullopt;
	}
	if ( nBudget <= 0 )
	{
		return std::nullopt;
	}
	const std::string szWanted = LowerText( parts[ nPart ].string() );
	std::vector<fs::path> candidates;
	if ( fs::exists( current / parts[ nPart ], error ) )
	{
		candidates.push_back( current / parts[ nPart ] );
	}
	for ( auto it = fs::directory_iterator{ current, error }; !error && it != fs::directory_iterator{}; it.increment( error ) )
	{
		if ( LowerText( it->path().filename().string() ) == szWanted && it->path() != current / parts[ nPart ] )
		{
			candidates.push_back( it->path() );
		}
	}
	for ( const fs::path &candidate : candidates )
	{
		if ( auto found = Resolve( candidate, parts, nPart + 1, nBudget - 1 ) )
		{
			return found;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The FGDs a Hammer config file lists for the game in gameDir, or nothing.
//-----------------------------------------------------------------------------
std::vector<fs::path> FgdsFromConfig( const fs::path &config, const fs::path &gameDir, const fs::path &root, HammerDefaults_t &defaults )
{
	std::ifstream stream{ config, std::ios::binary };
	if ( !stream )
	{
		return {};
	}
	const std::string szText{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
	const kv1::CParseResult parsed = kv1::Parse( szText );
	const kv1::CNode *pConfigs = parsed.m_Document.Find( "Configs" );
	const kv1::CNode *pGames = pConfigs ? pConfigs->Find( "Games" ) : nullptr;
	if ( !pGames )
	{
		return {};
	}
	std::error_code error;
	const fs::path wanted = fs::weakly_canonical( gameDir, error );
	for ( const kv1::CNode &game : pGames->GetChildren() )
	{
		const kv1::CNode *pGameDir = game.IsBlock() ? game.Find( "GameDir" ) : nullptr;
		if ( !pGameDir )
		{
			continue;
		}
		const fs::path listed = fs::weakly_canonical( FindFileIgnoringCase( LocalPath( pGameDir->GetValue(), root ) ), error );
		if ( LowerText( listed.string() ) != LowerText( wanted.string() ) )
		{
			continue;
		}
		const kv1::CNode *pHammer = game.Find( "Hammer" );
		if ( !pHammer )
		{
			return {};
		}
		if ( const kv1::CNode *pPoint = pHammer->Find( "DefaultPointEntity" ); pPoint && !pPoint->GetValue().empty() )
		{
			defaults.m_szPointEntity = std::string{ pPoint->GetValue() };
		}
		if ( const kv1::CNode *pSolid = pHammer->Find( "DefaultSolidEntity" ); pSolid && !pSolid->GetValue().empty() )
		{
			defaults.m_szSolidEntity = std::string{ pSolid->GetValue() };
		}
		if ( const kv1::CNode *pScale = pHammer->Find( "DefaultTextureScale" ) )
		{
			defaults.m_flTextureScale = ParseNumber<double>( pScale->GetValue() ).value_or( defaults.m_flTextureScale );
		}
		if ( const kv1::CNode *pLightmap = pHammer->Find( "DefaultLightmapScale" ) )
		{
			defaults.m_nLightmapScale = ParseNumber<int>( pLightmap->GetValue() ).value_or( defaults.m_nLightmapScale );
		}
		std::vector<fs::path> files;
		for ( int i = 0;; i++ )
		{
			const kv1::CNode *pData = pHammer->Find( "GameData" + std::to_string( i ) );
			if ( !pData )
			{
				break;
			}
			files.push_back( FindFileIgnoringCase( LocalPath( pData->GetValue(), root ) ) );
		}
		return files;
	}
	return {};
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
fs::path FindFileIgnoringCase( const fs::path &path )
{
	std::error_code error;
	if ( fs::exists( path, error ) )
	{
		return path;
	}
	const fs::path relative = path.relative_path();
	const std::vector<fs::path> parts( relative.begin(), relative.end() );
	if ( auto found = Resolve( path.root_path().empty() ? fs::path{ "." } : path.root_path(), parts, 0, 64 ) )
	{
		return *found;
	}
	return path;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
fs::path LocalPath( std::string_view szConfigPath, const fs::path &root )
{
#if defined( _WIN32 )
	(void)root;
	return fs::path{ std::string{ szConfigPath } };
#else
	std::string sz{ szConfigPath };
	std::ranges::replace( sz, '\\', '/' );
	if ( sz.size() >= 2 && sz[ 1 ] == ':' )
	{
		if ( sz[ 0 ] == 'Z' || sz[ 0 ] == 'z' )
		{
			return fs::path{ sz.substr( 2 ) };
		}
		// Another Wine drive: keep what follows the game's folder name, if it is in the path.
		const std::string szFolder = LowerText( root.filename().string() ) + "/";
		const std::size_t nAt = LowerText( sz ).find( "/" + szFolder );
		if ( nAt != std::string::npos )
		{
			return root / sz.substr( nAt + 1 + szFolder.size() );
		}
	}
	return fs::path{ sz };
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
GameFgds_t FindGameFgds( const fs::path &gameDir )
{
	GameFgds_t found;
	const fs::path root = gameDir.parent_path();
	std::error_code error;

	const fs::path hammerPlusPlus = FindFileIgnoringCase( root / "bin" / "x64" / "hammerplusplus" / "hammerplusplus_gameconfig.txt" );
	if ( fs::exists( hammerPlusPlus, error ) )
	{
		found.m_Files = FgdsFromConfig( hammerPlusPlus, gameDir, root, found.m_Defaults );
		if ( !found.m_Files.empty() )
		{
			found.m_szSource = "Hammer++'s game configuration";
			// Hammer++ always reads its own FGD after the game's.
			const fs::path own = FindFileIgnoringCase( hammerPlusPlus.parent_path() / "hammerplusplus_fgd.fgd" );
			if ( fs::exists( own, error ) )
			{
				found.m_Files.push_back( own );
			}
			return found;
		}
	}
	const fs::path hammer = FindFileIgnoringCase( root / "bin" / "GameConfig.txt" );
	if ( fs::exists( hammer, error ) )
	{
		found.m_Files = FgdsFromConfig( hammer, gameDir, root, found.m_Defaults );
		if ( !found.m_Files.empty() )
		{
			found.m_szSource = "Hammer's game configuration";
			return found;
		}
	}
	for ( const std::string &szName : { gameDir.filename().string() + ".fgd", std::string{ "halflife2.fgd" }, std::string{ "base.fgd" } } )
	{
		const fs::path fgd = FindFileIgnoringCase( root / "bin" / szName );
		if ( fs::exists( fgd, error ) )
		{
			found.m_Files.push_back( fgd );
			found.m_szSource = "the game's bin folder";
			return found;
		}
	}
	return found;
}

} // namespace xv::map
