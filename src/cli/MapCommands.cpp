//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: vomit-tools-cli map: compiling a map as Run Map does, from the command line (for
//          scripts and build servers): the compilers found beside the game or named, Hammer's
//          Normal choices as switches, or a named sequence from Hammer++'s or Hammer's own
//          sequence files; the output streamed, the problems summed up, the exit code telling.
//
//===========================================================================//
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Process.h"
#include "bsp/Decompile.h"
#include "bsp/Faces.h"
#include "map/Compile.h"
#include "map/GameConfig.h"
#include "vfs/Vfs.h"

int MapCommand( int argc, char **argv );

namespace
{

namespace fs = std::filesystem;

const char *const MAP_USAGE = R"(vomit-tools-cli map: Hammer maps

  vomit-tools-cli map compile <map.vmf> --game <folder with gameinfo.txt> [options]
      Compiles the map with vbsp, vvis and vrad as Hammer's Run Map does, copies it into the
      game's maps folder and (unless --no-run) starts the game on it.
      --vbsp, --vvis, --vrad, --game-exe <path>   the programs (else found beside the game:
                                                   vbsp++, vbspplusplus, vbsp...)
      --bspdir <folder>      where the compiled map goes (the game's maps folder)
      --onlyents             vbsp -onlyents            --no-vis / --fast-vis
      --no-rad / --fast-rad  --hdr (vrad -both)        --final (vrad -final)
      --no-run               don't start the game      --game-parameters "<text>"
      --sequence <name> [--sequences <file>]
                             run a named sequence from a hammerplusplus_sequences.cfg or
                             CmdSeq.wc (else the game's own Hammer++ file, else the built-in
                             ones: Default, Fast, LDR + HDR Full Compile...)
      --go-on                keep going after a leak (Hammer's way; Hammer++ stops)
  vomit-tools-cli map sequences [--sequences <file>] [--game <folder>]
      Lists the sequences and their commands.
  vomit-tools-cli map decompile <map.bsp> [<out.vmf>]
      Writes a compiled map back as a Hammer map (beside the .bsp, as <map>_d.vmf, unless
      named): its brushes, displacements, entities, static props, overlays and cubemaps.
  vomit-tools-cli map faces <map.bsp> [--ldr]
      Reads a compiled map's faces as the game draws them, with its baked light (the HDR
      compile's unless --ldr), and counts what it found.

Exit code 0 when everything ran, 2 when the map leaked, 1 otherwise.
)";

//-----------------------------------------------------------------------------
// Purpose: An executable file under one of the game's tool folders, the ++ names first.
//-----------------------------------------------------------------------------
std::string FindTool( std::string_view szBase, const xv::vfs::CGameInfo &game )
{
	std::vector<fs::path> folders;
	for ( const fs::path &root : { game.m_GameDir, game.m_BaseDir } )
	{
		for ( const char *pszSuffix : { "bin/linux64", "bin/linux32", "bin/win64", "bin/x64", "bin", "BIN/x64", "BIN", "" } )
		{
			folders.push_back( root / pszSuffix );
		}
	}
	for ( const fs::path &folder : folders )
	{
		for ( const std::string &szName : { std::string{ szBase } + "++", std::string{ szBase } + "plusplus", std::string{ szBase } } )
		{
			for ( const char *pszExtension : { "", ".exe" } )
			{
				const fs::path file = folder / ( szName + pszExtension );
				std::error_code error;
				if ( fs::is_regular_file( file, error ) )
				{
					return file.string();
				}
			}
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The game's own launcher beside its folder: cstrike.sh, hl2.sh, hl2.exe.
//-----------------------------------------------------------------------------
std::string FindGameExe( const xv::vfs::CGameInfo &game )
{
	const std::string szGame = game.m_GameDir.filename().string();
	for ( const std::string &szName : { szGame + ".sh", std::string{ "hl2.sh" }, std::string{ "hl2_linux" }, std::string{ "hl2.exe" }, szGame + ".exe" } )
	{
		std::error_code error;
		if ( fs::is_regular_file( game.m_BaseDir / szName, error ) )
		{
			return ( game.m_BaseDir / szName ).string();
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string> ReadFile( const fs::path &path )
{
	std::ifstream stream{ path, std::ios::binary };
	if ( !stream )
	{
		return std::nullopt;
	}
	return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

//-----------------------------------------------------------------------------
// Purpose: A sequence file of either kind, by its contents.
//-----------------------------------------------------------------------------
std::vector<xv::map::CompileSequence_t> ReadSequenceFile( const fs::path &path )
{
	const std::optional<std::string> szText = ReadFile( path );
	if ( !szText )
	{
		return {};
	}
	if ( szText->starts_with( "Worldcraft" ) )
	{
		return xv::map::ReadCmdSeq( std::span<const std::byte>( reinterpret_cast<const std::byte *>( szText->data() ), szText->size() ) );
	}
	return xv::map::ReadSequencesText( *szText );
}

//-----------------------------------------------------------------------------
// Purpose: The sequences named, else the game's Hammer++ file, else the built-in ones.
//-----------------------------------------------------------------------------
std::vector<xv::map::CompileSequence_t> Sequences( const std::string &szFile, const xv::vfs::CGameInfo *pGame )
{
	if ( !szFile.empty() )
	{
		return ReadSequenceFile( szFile );
	}
	if ( pGame )
	{
		for ( const char *pszFile : { "bin/x64/hammerplusplus/hammerplusplus_sequences.cfg", "BIN/x64/hammerplusplus/hammerplusplus_sequences.cfg", "bin/CmdSeq.wc", "BIN/CmdSeq.wc" } )
		{
			std::vector<xv::map::CompileSequence_t> sequences = ReadSequenceFile( pGame->m_BaseDir / pszFile );
			if ( !sequences.empty() )
			{
				return sequences;
			}
		}
	}
	return xv::map::BuiltinSequences();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::vfs::CGameInfo> LoadGame( const std::string &szGame, std::string *pError )
{
	if ( szGame.empty() )
	{
		*pError = "--game names no folder";
		return std::nullopt;
	}
	const fs::path path = xv::vfs::FromUtf8( szGame );
	return xv::vfs::CGameInfo::Load( fs::is_directory( path ) ? path / "gameinfo.txt" : path, pError );
}

//-----------------------------------------------------------------------------
// Purpose: The sequence run from the map's folder, as the app's runner does it.
//-----------------------------------------------------------------------------
int Compile( const std::vector<std::string> &inputs, const std::map<std::string, std::string> &options, const std::set<std::string> &switches )
{
	if ( inputs.empty() )
	{
		std::fprintf( stderr, "map compile: which map?\n\n%s", MAP_USAGE );
		return 1;
	}
	auto option = [ & ]( const char *pszName ) {
		const auto it = options.find( pszName );
		return it == options.end() ? std::string{} : it->second;
	};
	std::string szError;
	const std::optional<xv::vfs::CGameInfo> game = LoadGame( option( "--game" ), &szError );
	if ( !game )
	{
		std::fprintf( stderr, "map compile: %s\n", szError.c_str() );
		return 1;
	}
	const fs::path map = fs::absolute( xv::vfs::FromUtf8( inputs.front() ) );
	std::error_code error;
	if ( !fs::is_regular_file( map, error ) )
	{
		std::fprintf( stderr, "map compile: %s isn't a file\n", map.string().c_str() );
		return 1;
	}
	xv::map::CompileVariables_t variables;
	variables.m_szFile = map.stem().string();
	variables.m_szPath = map.parent_path().string();
	variables.m_szGameDir = game->m_GameDir.string();
	variables.m_szExeDir = game->m_BaseDir.string();
	variables.m_szBspDir = option( "--bspdir" ).empty() ? ( game->m_GameDir / "maps" ).string() : fs::absolute( xv::vfs::FromUtf8( option( "--bspdir" ) ) ).string();
	variables.m_szBspExe = option( "--vbsp" ).empty() ? FindTool( "vbsp", *game ) : option( "--vbsp" );
	variables.m_szVisExe = option( "--vvis" ).empty() ? FindTool( "vvis", *game ) : option( "--vvis" );
	variables.m_szLightExe = option( "--vrad" ).empty() ? FindTool( "vrad", *game ) : option( "--vrad" );
	variables.m_szGameExe = option( "--game-exe" ).empty() ? FindGameExe( *game ) : option( "--game-exe" );

	xv::map::CompileSequence_t sequence;
	if ( const std::string szName = option( "--sequence" ); !szName.empty() )
	{
		const std::vector<xv::map::CompileSequence_t> sequences = Sequences( option( "--sequences" ), &*game );
		const auto it = std::ranges::find_if( sequences, [ & ]( const xv::map::CompileSequence_t &s ) { return s.m_szName == szName; } );
		if ( it == sequences.end() )
		{
			std::fprintf( stderr, "map compile: no sequence is called \"%s\" (map sequences lists them)\n", szName.c_str() );
			return 1;
		}
		sequence = *it;
	}
	else
	{
		xv::map::NormalOptions_t normal;
		normal.m_nBsp = switches.contains( "--onlyents" ) ? xv::map::NormalOptions_t::Bsp_t::BSP_ONLY_ENTITIES : xv::map::NormalOptions_t::Bsp_t::BSP_NORMAL;
		normal.m_nVis = switches.contains( "--no-vis" ) ? xv::map::NormalOptions_t::Vis_t::VIS_NO : switches.contains( "--fast-vis" ) ? xv::map::NormalOptions_t::Vis_t::VIS_FAST
																																	  : xv::map::NormalOptions_t::Vis_t::VIS_NORMAL;
		normal.m_nRad = switches.contains( "--no-rad" ) ? xv::map::NormalOptions_t::Rad_t::RAD_NO : switches.contains( "--fast-rad" ) ? xv::map::NormalOptions_t::Rad_t::RAD_FAST
																																	  : xv::map::NormalOptions_t::Rad_t::RAD_NORMAL;
		normal.m_bHdr = switches.contains( "--hdr" );
		normal.m_bRun = !switches.contains( "--no-run" );
		normal.m_szGameParameters = option( "--game-parameters" );
		sequence = xv::map::SequenceFromNormal( normal );
		if ( switches.contains( "--final" ) )
		{
			for ( xv::map::CompileCommand_t &command : sequence.m_Commands )
			{
				if ( command.m_szRun == "$light_exe" )
				{
					command.m_szParms += " -final";
				}
			}
		}
	}
	const std::vector<xv::map::ResolvedCommand_t> commands = xv::map::ResolveSequence( sequence, variables );
	for ( const xv::map::ResolvedCommand_t &command : commands )
	{
		if ( command.m_nSpecial == xv::map::COMPILE_SPECIAL_RUN && ( command.m_szProgram.empty() || !fs::is_regular_file( command.m_szProgram, error ) ) )
		{
			std::fprintf( stderr, "map compile: %s isn't there; name it with --vbsp, --vvis, --vrad or --game-exe\n", command.m_szProgram.empty() ? "a compiler" : command.m_szProgram.c_str() );
			return 1;
		}
	}
	std::string szLog;
	auto out = [ & ]( std::string_view szGiven ) {
		const std::string szText = xv::map::StripAnsi( szGiven );
		szLog.append( szText );
		std::fwrite( szText.data(), 1, szText.size(), stdout );
		std::fflush( stdout );
	};
	std::string szDirectory = variables.m_szPath;
	bool bLeaked = false;
	bool bFailed = false;
	for ( const xv::map::ResolvedCommand_t &command : commands )
	{
		out( "> " + command.m_szShown + "\n" );
		bool bOk = true;
		switch ( command.m_nSpecial )
		{
			case xv::map::COMPILE_SPECIAL_CHANGE_DIR:
				szDirectory = ( fs::path{ szDirectory } / command.m_szFrom ).string();
				bOk = fs::is_directory( szDirectory, error );
				break;
			case xv::map::COMPILE_SPECIAL_COPY:
			{
				const fs::path to = fs::path{ szDirectory } / command.m_szTo;
				fs::create_directories( to.parent_path(), error );
				bOk = fs::copy_file( fs::path{ szDirectory } / command.m_szFrom, to, fs::copy_options::overwrite_existing, error );
				if ( !bOk )
				{
					out( "The system cannot find the file specified: " + command.m_szFrom + "\n" );
				}
				break;
			}
			case xv::map::COMPILE_SPECIAL_DELETE:
				fs::remove( fs::path{ szDirectory } / command.m_szFrom, error );
				break;
			case xv::map::COMPILE_SPECIAL_RENAME:
				fs::rename( fs::path{ szDirectory } / command.m_szFrom, fs::path{ szDirectory } / command.m_szTo, error );
				bOk = !error;
				break;
			default:
				if ( !command.m_bWait )
				{
					bOk = xv::cli::StartProcess( command.m_szProgram, command.m_Arguments, fs::path{ command.m_szProgram }.parent_path().string() );
					if ( !bOk )
					{
						out( command.m_szProgram + " could not be started\n" );
					}
					break;
				}
				bOk = xv::cli::RunProcess( command.m_szProgram, command.m_Arguments, szDirectory, out ) == 0;
				// VBSP++ exits with an error on a leak, stock vbsp doesn't: the log says either way.
				if ( fs::path{ command.m_szProgram }.filename().string().find( "vbsp" ) != std::string::npos && xv::map::ParseCompileOutput( szLog ).m_bLeaked )
				{
					bLeaked = true;
					if ( !switches.contains( "--go-on" ) || !bOk )
					{
						out( "The map leaked: the rest of the sequence isn't run.\n" );
						bOk = false;
					}
				}
				break;
		}
		if ( bOk && !command.m_szEnsure.empty() && !fs::exists( fs::path{ szDirectory } / command.m_szEnsure, error ) )
		{
			out( command.m_szEnsure + " wasn't made\n" );
			bOk = false;
		}
		if ( !bOk )
		{
			bFailed = true;
			break;
		}
	}
	const xv::map::CompileReport_t report = xv::map::ParseCompileOutput( szLog );
	if ( report.m_bLeaked )
	{
		std::printf( "\nLEAKED: %s", report.m_szLeakEntity.c_str() );
		if ( report.m_LeakAt )
		{
			std::printf( " at (%g %g %g); the line is in %s.lin", ( *report.m_LeakAt )[ 0 ], ( *report.m_LeakAt )[ 1 ], ( *report.m_LeakAt )[ 2 ], ( fs::path{ variables.m_szPath } / variables.m_szFile ).string().c_str() );
		}
		std::printf( "\n" );
	}
	for ( const xv::map::CompileProblem_t &problem : report.m_Problems )
	{
		std::printf( "%s %s: %s\n", problem.m_bError ? "ERROR" : "warning", problem.m_szTool.empty() ? "-" : problem.m_szTool.c_str(), problem.m_szMessage.c_str() );
	}
	return bLeaked ? 2 : bFailed ? 1
								 : 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int ListSequences( const std::map<std::string, std::string> &options )
{
	auto option = [ & ]( const char *pszName ) {
		const auto it = options.find( pszName );
		return it == options.end() ? std::string{} : it->second;
	};
	std::string szError;
	const std::optional<xv::vfs::CGameInfo> game = option( "--game" ).empty() ? std::nullopt : LoadGame( option( "--game" ), &szError );
	for ( const xv::map::CompileSequence_t &sequence : Sequences( option( "--sequences" ), game ? &*game : nullptr ) )
	{
		std::printf( "%s\n", sequence.m_szName.c_str() );
		for ( const xv::map::CompileCommand_t &command : sequence.m_Commands )
		{
			std::printf( "  %s%s %s\n", command.m_bEnabled ? "" : "(off) ", command.m_nSpecial == xv::map::COMPILE_SPECIAL_RUN ? command.m_szRun.c_str() : command.m_nSpecial == xv::map::COMPILE_SPECIAL_COPY ? "copy"
					: command.m_nSpecial == xv::map::COMPILE_SPECIAL_DELETE                                                                                                                                    ? "delete"
					: command.m_nSpecial == xv::map::COMPILE_SPECIAL_RENAME                                                                                                                                    ? "rename"
																																																			   : "cd",
				command.m_szParms.c_str() );
		}
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: map decompile: the .bsp read whole, the VMF written, what was found counted.
//-----------------------------------------------------------------------------
int Decompile( const std::vector<std::string> &inputs )
{
	if ( inputs.empty() || inputs.size() > 2 )
	{
		std::fprintf( stderr, "map decompile: give a .bsp, and optionally the .vmf to write\n" );
		return 1;
	}
	const fs::path bsp( inputs[ 0 ] );
	fs::path vmf = inputs.size() > 1 ? fs::path( inputs[ 1 ] ) : bsp;
	if ( inputs.size() == 1 )
	{
		vmf.replace_filename( bsp.stem().string() + "_d.vmf" );
	}
	std::ifstream in( bsp, std::ios::binary );
	if ( !in )
	{
		std::fprintf( stderr, "map decompile: could not open %s\n", inputs[ 0 ].c_str() );
		return 1;
	}
	const std::vector<char> data( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
	xv::bsp::DecompileReport_t report;
	std::string szError;
	const auto text = xv::bsp::DecompileToVmf( std::as_bytes( std::span( data ) ), &report, &szError );
	if ( !text )
	{
		std::fprintf( stderr, "map decompile: %s\n", szError.c_str() );
		return 1;
	}
	std::ofstream out( vmf, std::ios::binary );
	out.write( text->data(), static_cast<std::streamsize>( text->size() ) );
	if ( !out )
	{
		std::fprintf( stderr, "map decompile: could not write %s\n", xv::vfs::ToUtf8( vmf ).c_str() );
		return 1;
	}
	std::printf( "%s: %d solids, %d entities, %d displacements, %d static props, %d overlays, %d cubemaps\n", xv::vfs::ToUtf8( vmf ).c_str(), report.m_nSolids, report.m_nEntities, report.m_nDisplacements, report.m_nStaticProps, report.m_nOverlays, report.m_nCubemaps );
	for ( const std::string &szWarning : report.m_Warnings )
	{
		std::printf( "warning: %s\n", szWarning.c_str() );
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: map faces: a compiled map's own faces and lightmaps read, and counted.
//-----------------------------------------------------------------------------
int Faces( const std::vector<std::string> &inputs, const std::set<std::string> &switches )
{
	if ( inputs.size() != 1 )
	{
		std::fprintf( stderr, "map faces: give a .bsp\n" );
		return 1;
	}
	const fs::path bsp( inputs[ 0 ] );
	std::ifstream in( bsp, std::ios::binary );
	if ( !in )
	{
		std::fprintf( stderr, "map faces: could not open %s\n", inputs[ 0 ].c_str() );
		return 1;
	}
	const std::vector<char> data( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
	xv::bsp::CompiledOptions_t options;
	options.m_bHdr = !switches.contains( "--ldr" );
	std::string szError;
	const auto compiled = xv::bsp::ReadCompiledMap( std::as_bytes( std::span( data ) ), options, &szError );
	if ( !compiled )
	{
		std::fprintf( stderr, "map faces: %s\n", szError.c_str() );
		return 1;
	}
	std::size_t nFaces = 0;
	std::size_t nTriangles = 0;
	std::size_t nVertices = 0;
	for ( const xv::bsp::CompiledModel_t &model : compiled->m_Models )
	{
		nFaces += static_cast<std::size_t>( model.m_nFaces );
		nTriangles += model.m_pMesh->m_Indices.size() / 3;
		nVertices += model.m_pMesh->m_Vertices.size();
	}
	std::printf( "%zu models, %zu faces (%d lit, %d drawn as they are, %d displacements), %zu triangles, %zu vertices, %zu materials\n", compiled->m_Models.size(), nFaces, compiled->m_nLitFaces, compiled->m_nUnlitFaces, compiled->m_nDisplacements, nTriangles, nVertices, compiled->m_Materials.size() );
	std::printf( "light: %s, atlas %d x %d\n", compiled->m_bHdr ? "HDR" : "LDR", compiled->m_pLighting->m_nWidth, compiled->m_pLighting->m_nHeight );
	if ( const auto &pAmbient = compiled->m_pAmbient; pAmbient && !pAmbient->IsEmpty() )
	{
		// Where the cubes are found: down a vertical line through the middle of the world.
		const xv::map::Box_t &bounds = compiled->m_Models[ 0 ].m_Bounds;
		int nFound = 0;
		xv::CVec3 vecSum;
		for ( int i = 0; i < 64; i++ )
		{
			for ( int j = 0; j < 64; j++ )
			{
				for ( int k = 0; k < 16; k++ )
				{
					const xv::map::Point_t at{ bounds.m_Mins[ 0 ] + ( bounds.m_Maxs[ 0 ] - bounds.m_Mins[ 0 ] ) * ( i + 0.5 ) / 64.0, bounds.m_Mins[ 1 ] + ( bounds.m_Maxs[ 1 ] - bounds.m_Mins[ 1 ] ) * ( j + 0.5 ) / 64.0, bounds.m_Mins[ 2 ] + ( bounds.m_Maxs[ 2 ] - bounds.m_Mins[ 2 ] ) * ( k + 0.5 ) / 16.0 };
					if ( const auto cube = pAmbient->CubeAt( at ) )
					{
						nFound++;
						vecSum += xv::bsp::CAmbientLighting::LightFor( *cube, { 0.0f, 0.0f, 1.0f } );
					}
				}
			}
		}
		std::printf( "ambient cubes: found at %d of %d points, mean light from above %.4f %.4f %.4f\n", nFound, 64 * 64 * 16, nFound ? vecSum.m_flX / nFound : 0.0, nFound ? vecSum.m_flY / nFound : 0.0, nFound ? vecSum.m_flZ / nFound : 0.0 );
	}
	for ( const std::string &szProblem : compiled->m_Problems )
	{
		std::printf( "problem: %s\n", szProblem.c_str() );
	}
	return 0;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: map <subcommand> [inputs] [--option value] [--switch].
//-----------------------------------------------------------------------------
int MapCommand( int argc, char **argv )
{
	if ( argc < 1 || std::string_view( argv[ 0 ] ) == "--help" || std::string_view( argv[ 0 ] ) == "-h" )
	{
		std::printf( "%s", MAP_USAGE );
		return argc < 1 ? 1 : 0;
	}
	const std::string szCommand = argv[ 0 ];
	std::vector<std::string> inputs;
	std::map<std::string, std::string> options;
	std::set<std::string> switches;
	static const std::set<std::string> VALUED = { "--game", "--vbsp", "--vvis", "--vrad", "--game-exe", "--bspdir", "--game-parameters", "--sequence", "--sequences" };
	for ( int i = 1; i < argc; i++ )
	{
		const std::string szArgument = argv[ i ];
		if ( VALUED.contains( szArgument ) )
		{
			if ( i + 1 >= argc )
			{
				std::fprintf( stderr, "map %s: %s needs a value\n", szCommand.c_str(), szArgument.c_str() );
				return 1;
			}
			options[ szArgument ] = argv[ ++i ];
		}
		else if ( szArgument.starts_with( "--" ) )
		{
			switches.insert( szArgument );
		}
		else
		{
			inputs.push_back( szArgument );
		}
	}
	if ( szCommand == "compile" )
	{
		return Compile( inputs, options, switches );
	}
	if ( szCommand == "sequences" )
	{
		return ListSequences( options );
	}
	if ( szCommand == "decompile" )
	{
		return Decompile( inputs );
	}
	if ( szCommand == "faces" )
	{
		return Faces( inputs, switches );
	}
	std::fprintf( stderr, "map: unknown command %s\n\n%s", szCommand.c_str(), MAP_USAGE );
	return 1;
}
