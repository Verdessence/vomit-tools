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
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Process.h"
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
	std::fprintf( stderr, "map: unknown command %s\n\n%s", szCommand.c_str(), MAP_USAGE );
	return 1;
}
