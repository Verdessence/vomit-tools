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
std::vector<fs::path> FgdsFromConfig( const fs::path &config, const fs::path &gameDir, const fs::path &root, HammerDefaults_t &defaults, HammerCompilePaths_t &compile )
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
		for ( const auto &[ pszKey, pPath ] : { std::pair{ "BSP", &compile.m_Bsp }, std::pair{ "Vis", &compile.m_Vis }, std::pair{ "Light", &compile.m_Light }, std::pair{ "GameExe", &compile.m_GameExe }, std::pair{ "BSPDir", &compile.m_BspDir }, std::pair{ "MapDir", &compile.m_MapDir } } )
		{
			if ( const kv1::CNode *pValue = pHammer->Find( pszKey ); pValue && !pValue->GetValue().empty() )
			{
				*pPath = FindFileIgnoringCase( LocalPath( pValue->GetValue(), root ) );
			}
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
		found.m_Files = FgdsFromConfig( hammerPlusPlus, gameDir, root, found.m_Defaults, found.m_Compile );
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
		found.m_Files = FgdsFromConfig( hammer, gameDir, root, found.m_Defaults, found.m_Compile );
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

//-----------------------------------------------------------------------------
// Purpose: Hammer++ keeps its configuration beside its program; Hammer in the bin folder.
//-----------------------------------------------------------------------------
fs::path HammerConfigFile( const fs::path &gameDir, HammerKind_t nKind )
{
	const fs::path root = gameDir.parent_path();
	if ( nKind == HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS )
		return FindFileIgnoringCase( root / "bin" / "x64" / "hammerplusplus" / "hammerplusplus_gameconfig.txt" );
	return FindFileIgnoringCase( root / "bin" / "GameConfig.txt" );
}

//-----------------------------------------------------------------------------
// Purpose: The folder holding hammerplusplus_fgd.fgd, which Hammer++ also reads models and
//          materials from.
//-----------------------------------------------------------------------------
std::optional<fs::path> HammerPlusPlusContent( const fs::path &gameDir )
{
	const fs::path root = gameDir.parent_path();
	std::error_code error;
	for ( const fs::path &candidate : { root / "bin" / "x64" / "hammerplusplus", root / "bin" / "hammerplusplus" } )
	{
		const fs::path found = FindFileIgnoringCase( candidate );
		if ( fs::is_directory( found, error ) && fs::exists( FindFileIgnoringCase( found / "hammerplusplus_fgd.fgd" ), error ) )
			return found;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer++ for the 64-bit games is bin/x64/hammerplusplus.exe; Hammer is in bin, or
//          bin/x64 for those games.
//-----------------------------------------------------------------------------
std::optional<fs::path> HammerProgram( const fs::path &gameDir, HammerKind_t nKind )
{
	const fs::path root = gameDir.parent_path();
	std::error_code error;
	const std::vector<fs::path> candidates = nKind == HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS
		? std::vector<fs::path>{ root / "bin" / "x64" / "hammerplusplus.exe", root / "bin" / "hammerplusplus.exe" }
		: std::vector<fs::path>{ root / "bin" / "x64" / "hammer.exe", root / "bin" / "hammer.exe" };
	for ( const fs::path &candidate : candidates )
	{
		const fs::path found = FindFileIgnoringCase( candidate );
		if ( fs::is_regular_file( found, error ) )
			return found;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string ConfigPath( const fs::path &path )
{
#if defined( _WIN32 )
	return path.string();
#else
	std::string sz = "Z:" + path.generic_string();
	std::ranges::replace( sz, '/', '\\' );
	return sz;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: The Games entry whose GameDir is the game's.
//-----------------------------------------------------------------------------
std::optional<HammerSetup_t> ReadHammerSetup( std::string_view szText, const fs::path &gameDir )
{
	const fs::path root = gameDir.parent_path();
	const kv1::CParseResult parsed = kv1::Parse( szText );
	const kv1::CNode *pConfigs = parsed.m_Document.Find( "Configs" );
	const kv1::CNode *pGames = pConfigs ? pConfigs->Find( "Games" ) : nullptr;
	if ( !pGames )
		return std::nullopt;
	std::error_code error;
	for ( const kv1::CNode &game : pGames->GetChildren() )
	{
		const kv1::CNode *pGameDir = game.IsBlock() ? game.Find( "GameDir" ) : nullptr;
		if ( !pGameDir || !fs::equivalent( LocalPath( pGameDir->GetValue(), root ), gameDir, error ) )
			continue;
		HammerSetup_t setup;
		setup.m_szName = game.GetKey();
		setup.m_GameDir = gameDir;
		const kv1::CNode *pHammer = game.Find( "Hammer" );
		if ( !pHammer )
			return setup;
		auto path = [ & ]( std::string_view szKey ) {
			const kv1::CNode *pNode = pHammer->Find( szKey );
			return pNode && !pNode->GetValue().empty() ? LocalPath( pNode->GetValue(), root ) : fs::path{};
		};
		for ( int n = 0;; n++ )
		{
			const fs::path fgd = path( "GameData" + std::to_string( n ) );
			if ( fgd.empty() )
				break;
			setup.m_Fgds.push_back( fgd );
		}
		setup.m_GameExe = path( "GameExe" );
		setup.m_Bsp = path( "BSP" );
		setup.m_Vis = path( "Vis" );
		setup.m_Light = path( "Light" );
		setup.m_Mdl = path( "MDL" );
		setup.m_GameExeDir = path( "GameExeDir" );
		setup.m_MapDir = path( "MapDir" );
		setup.m_BspDir = path( "BSPDir" );
		setup.m_PrefabDir = path( "PrefabDir" );
		if ( const kv1::CNode *pCordon = pHammer->Find( "CordonTexture" ) )
			setup.m_szCordonTexture = pCordon->GetValue();
		if ( const kv1::CNode *pPoint = pHammer->Find( "DefaultPointEntity" ); pPoint && !pPoint->GetValue().empty() )
			setup.m_Defaults.m_szPointEntity = pPoint->GetValue();
		if ( const kv1::CNode *pSolid = pHammer->Find( "DefaultSolidEntity" ); pSolid && !pSolid->GetValue().empty() )
			setup.m_Defaults.m_szSolidEntity = pSolid->GetValue();
		if ( const kv1::CNode *pScale = pHammer->Find( "DefaultTextureScale" ) )
			setup.m_Defaults.m_flTextureScale = ParseNumber<double>( pScale->GetValue() ).value_or( setup.m_Defaults.m_flTextureScale );
		if ( const kv1::CNode *pLightmap = pHammer->Find( "DefaultLightmapScale" ) )
			setup.m_Defaults.m_nLightmapScale = ParseNumber<int>( pLightmap->GetValue() ).value_or( setup.m_Defaults.m_nLightmapScale );
		return setup;
	}
	return std::nullopt;
}

namespace
{
//-----------------------------------------------------------------------------
// Purpose: The first of the names that is there (any case), else the first name.
//-----------------------------------------------------------------------------
fs::path FirstThere( std::initializer_list<fs::path> candidates )
{
	std::error_code error;
	for ( const fs::path &candidate : candidates )
	{
		const fs::path found = FindFileIgnoringCase( candidate );
		if ( fs::exists( found, error ) )
			return found;
	}
	return *candidates.begin();
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The 64-bit tools when the game has them, as Hammer++ for those games expects.
//-----------------------------------------------------------------------------
HammerSetup_t SuggestHammerSetup( const fs::path &gameDir, std::string_view szName )
{
	const fs::path root = gameDir.parent_path();
	const std::string szMod = gameDir.filename().string();
	HammerSetup_t setup;
	setup.m_szName = szName.empty() ? szMod : std::string{ szName };
	setup.m_GameDir = gameDir;
	const GameFgds_t fgds = FindGameFgds( gameDir );
	for ( const fs::path &fgd : fgds.m_Files )
		if ( LowerText( fgd.filename().string() ) != "hammerplusplus_fgd.fgd" )
			setup.m_Fgds.push_back( fgd );
	setup.m_Defaults = fgds.m_Defaults;
	auto tool = [ & ]( const char *pszName ) { return FirstThere( { root / "bin" / "x64" / pszName, root / "bin" / pszName } ); };
	setup.m_Bsp = tool( "vbsp.exe" );
	setup.m_Vis = tool( "vvis.exe" );
	setup.m_Light = tool( "vrad.exe" );
	setup.m_Mdl = tool( "studiomdl.exe" );
	setup.m_GameExe = FirstThere( { root / "hl2.exe", root / ( szMod + "_win64.exe" ), root / ( szMod + ".exe" ) } );
	setup.m_GameExeDir = root;
	setup.m_MapDir = FirstThere( { root / "sourcesdk_content" / szMod / "mapsrc", gameDir / "mapsrc" } );
	setup.m_BspDir = FirstThere( { gameDir / "maps" } );
	setup.m_PrefabDir = FirstThere( { root / "bin" / "x64" / "Prefabs", root / "bin" / "Prefabs" } );
	return setup;
}

//-----------------------------------------------------------------------------
// Purpose: Keys already there change in place; GameData past the new count go; the rest are
//          added after the last, in the order Hammer writes them.
//-----------------------------------------------------------------------------
std::string WriteHammerSetup( std::string_view szExisting, const HammerSetup_t &setup )
{
	kv1::CParseResult parsed = kv1::Parse( szExisting.empty() ? std::string_view{ "\"Configs\"\n{\n\t\"Games\"\n\t{\n\t}\n\t\"SDKVersion\"\t\t\"5\"\n}\n" } : szExisting );
	kv1::CDocument &document = parsed.m_Document;
	kv1::CNode *pConfigs = document.Find( "Configs" );
	if ( !pConfigs )
		pConfigs = &document.AppendRoot( kv1::CNode::MakeBlock( "Configs" ) );
	kv1::CNode *pGames = pConfigs->Find( "Games" );
	if ( !pGames )
		pGames = &pConfigs->InsertChild( 0, kv1::CNode::MakeBlock( "Games" ) );
	if ( !pConfigs->Find( "SDKVersion" ) )
		pConfigs->AppendChild( kv1::CNode::MakePair( "SDKVersion", "5" ) );
	const fs::path root = setup.m_GameDir.parent_path();
	std::error_code error;
	kv1::CNode *pGame = nullptr;
	for ( std::size_t n = 0; n < pGames->GetChildren().size(); n++ )
	{
		kv1::CNode &game = pGames->Child( n );
		const kv1::CNode *pGameDir = game.IsBlock() ? game.Find( "GameDir" ) : nullptr;
		if ( pGameDir && fs::equivalent( LocalPath( pGameDir->GetValue(), root ), setup.m_GameDir, error ) )
		{
			pGame = &game;
			break;
		}
	}
	if ( !pGame )
		pGame = &pGames->AppendChild( kv1::CNode::MakeBlock( setup.m_szName ) );
	pGame->SetKey( setup.m_szName );
	auto set = [ & ]( kv1::CNode &block, std::string_view szKey, std::string_view szValue ) {
		if ( kv1::CNode *pNode = block.Find( szKey ); pNode && pNode->IsPair() )
			pNode->SetValue( szValue );
		else
			block.AppendChild( kv1::CNode::MakePair( szKey, szValue ) );
	};
	set( *pGame, "GameDir", ConfigPath( setup.m_GameDir ) );
	kv1::CNode *pHammer = pGame->Find( "Hammer" );
	if ( !pHammer )
		pHammer = &pGame->AppendChild( kv1::CNode::MakeBlock( "Hammer" ) );
	// GameData keys first, in order, as Hammer writes them.
	std::size_t nAt = 0;
	for ( std::size_t n = 0; n < setup.m_Fgds.size(); n++ )
	{
		const std::string szKey = "GameData" + std::to_string( n );
		if ( kv1::CNode *pNode = pHammer->Find( szKey ) )
			pNode->SetValue( ConfigPath( setup.m_Fgds[ n ] ) );
		else
			pHammer->InsertChild( std::min( nAt, pHammer->GetChildren().size() ), kv1::CNode::MakePair( szKey, ConfigPath( setup.m_Fgds[ n ] ) ) );
		for ( std::size_t c = 0; c < pHammer->GetChildren().size(); c++ )
			if ( kv1::EqualsIgnoreCase( pHammer->Child( c ).GetKey(), szKey ) )
				nAt = c + 1;
	}
	for ( std::size_t c = pHammer->GetChildren().size(); c-- > 0; )
	{
		const std::string szKey = pHammer->Child( c ).GetKey();
		if ( szKey.size() > 8 && kv1::EqualsIgnoreCase( std::string_view{ szKey }.substr( 0, 8 ), "GameData" ) )
			if ( const auto n = ParseNumber<int>( std::string_view{ szKey }.substr( 8 ) ); n && *n >= static_cast<int>( setup.m_Fgds.size() ) )
				pHammer->RemoveChild( c );
	}
	auto setPath = [ & ]( std::string_view szKey, const fs::path &path ) {
		if ( !path.empty() )
			set( *pHammer, szKey, ConfigPath( path ) );
	};
	set( *pHammer, "TextureFormat", "5" );
	set( *pHammer, "MapFormat", "4" );
	set( *pHammer, "DefaultTextureScale", std::to_string( setup.m_Defaults.m_flTextureScale ) );
	set( *pHammer, "DefaultLightmapScale", std::to_string( setup.m_Defaults.m_nLightmapScale ) );
	setPath( "GameExe", setup.m_GameExe );
	set( *pHammer, "DefaultSolidEntity", setup.m_Defaults.m_szSolidEntity );
	set( *pHammer, "DefaultPointEntity", setup.m_Defaults.m_szPointEntity );
	setPath( "BSP", setup.m_Bsp );
	setPath( "Vis", setup.m_Vis );
	setPath( "Light", setup.m_Light );
	setPath( "MDL", setup.m_Mdl );
	setPath( "GameExeDir", setup.m_GameExeDir );
	setPath( "MapDir", setup.m_MapDir );
	setPath( "BSPDir", setup.m_BspDir );
	setPath( "PrefabDir", setup.m_PrefabDir );
	set( *pHammer, "CordonTexture", setup.m_szCordonTexture );
	if ( !pHammer->Find( "MaterialExcludeCount" ) )
		pHammer->AppendChild( kv1::CNode::MakePair( "MaterialExcludeCount", "0" ) );
	return kv1::Write( document );
}

} // namespace xv::map
