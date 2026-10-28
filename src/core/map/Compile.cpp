//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Compile.h.
//
//===========================================================================//
#include "Compile.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>

#include "kv1/Kv1.h"
#include "util/Numbers.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The value of one of Hammer's variables, or nullopt for a name that isn't one.
//-----------------------------------------------------------------------------
std::optional<std::string_view> VariableValue( std::string_view szName, const CompileVariables_t &variables )
{
	std::string szLower{ szName };
	std::ranges::transform( szLower, szLower.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	if ( szLower == "file" )
	{
		return variables.m_szFile;
	}
	if ( szLower == "path" )
	{
		return variables.m_szPath;
	}
	if ( szLower == "bspdir" )
	{
		return variables.m_szBspDir;
	}
	if ( szLower == "gamedir" )
	{
		return variables.m_szGameDir;
	}
	if ( szLower == "exedir" )
	{
		return variables.m_szExeDir;
	}
	if ( szLower == "bsp_exe" )
	{
		return variables.m_szBspExe;
	}
	if ( szLower == "vis_exe" )
	{
		return variables.m_szVisExe;
	}
	if ( szLower == "light_exe" )
	{
		return variables.m_szLightExe;
	}
	if ( szLower == "game_exe" )
	{
		return variables.m_szGameExe;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: A command as Hammer++'s file (and Hammer's Normal tab) would write it.
//-----------------------------------------------------------------------------
CompileCommand_t Command( std::string_view szRun, std::string_view szParms, int nSpecial = COMPILE_SPECIAL_RUN, bool bWait = true )
{
	CompileCommand_t command;
	command.m_nSpecial = nSpecial;
	command.m_szRun = std::string{ szRun };
	command.m_szParms = std::string{ szParms };
	command.m_bWait = bWait;
	return command;
}

//-----------------------------------------------------------------------------
// Purpose: "(x y z)" or "(x, y, z)" anywhere in a line.
//-----------------------------------------------------------------------------
std::optional<Point_t> PointIn( const std::string &szLine )
{
	static const std::regex pattern( R"(\(\s*(-?[0-9.]+)[\s,]+(-?[0-9.]+)[\s,]+(-?[0-9.]+)\s*\))" );
	std::smatch match;
	if ( !std::regex_search( szLine, match, pattern ) )
	{
		return std::nullopt;
	}
	Point_t p{};
	for ( std::size_t a = 0; a < 3; a++ )
	{
		p[ a ] = ParseNumber<double>( match[ a + 1 ].str() ).value_or( 0.0 );
	}
	return p;
}

//-----------------------------------------------------------------------------
// Purpose: Lower case, for matching messages whatever their case.
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string sz{ szText };
	std::ranges::transform( sz, sz.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: A fixed-size, zero-padded string from a binary file.
//-----------------------------------------------------------------------------
std::string FixedString( std::span<const std::byte> bytes, std::size_t nAt, std::size_t nLength )
{
	std::string sz;
	for ( std::size_t i = nAt; i < nAt + nLength && i < bytes.size(); i++ )
	{
		const char ch = static_cast<char>( bytes[ i ] );
		if ( ch == '\0' )
		{
			break;
		}
		sz += ch;
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::int32_t Int32( std::span<const std::byte> bytes, std::size_t nAt )
{
	if ( nAt + 4 > bytes.size() )
	{
		return 0;
	}
	std::uint32_t n = 0;
	for ( std::size_t i = 0; i < 4; i++ )
	{
		n |= static_cast<std::uint32_t>( static_cast<unsigned char>( bytes[ nAt + i ] ) ) << ( 8 * i );
	}
	return static_cast<std::int32_t>( n );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Token by token (at unquoted spaces): a token that took in a value with a space is
//          quoted whole, so "$path\$file.bsp" stays one argument. Hammer's backslashes become
//          the system's separators, as its paths came from Windows.
//-----------------------------------------------------------------------------
std::string ExpandParameters( std::string_view szText, const CompileVariables_t &variables )
{
	std::string szOut;
	std::string szToken;
	bool bQuote = false;
	bool bInQuotes = false;
	auto flush = [ & ] {
		if ( szToken.empty() )
		{
			return;
		}
		if ( !szOut.empty() )
		{
			szOut += ' ';
		}
		szOut += bQuote && szToken.front() != '"' ? '"' + szToken + '"' : szToken;
		szToken.clear();
		bQuote = false;
	};
	for ( std::size_t i = 0; i < szText.size(); i++ )
	{
		const char ch = szText[ i ];
		if ( ch == '"' )
		{
			bInQuotes = !bInQuotes;
			szToken += ch;
			continue;
		}
		if ( ( ch == ' ' || ch == '\t' ) && !bInQuotes )
		{
			flush();
			continue;
		}
		if ( ch == '$' )
		{
			if ( i + 1 < szText.size() && szText[ i + 1 ] == '$' )
			{
				szToken += '$';
				i++;
				continue;
			}
			std::size_t nEnd = i + 1;
			while ( nEnd < szText.size() && ( std::isalnum( static_cast<unsigned char>( szText[ nEnd ] ) ) || szText[ nEnd ] == '_' ) )
			{
				nEnd++;
			}
			if ( const auto value = VariableValue( szText.substr( i + 1, nEnd - i - 1 ), variables ) )
			{
				szToken += *value;
				bQuote = bQuote || value->find( ' ' ) != std::string_view::npos;
				i = nEnd - 1;
				continue;
			}
		}
#ifndef _WIN32
		if ( ch == '\\' )
		{
			szToken += '/';
			continue;
		}
#endif
		szToken += ch;
	}
	flush();
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> SplitArguments( std::string_view szText )
{
	std::vector<std::string> arguments;
	std::string szToken;
	bool bInQuotes = false;
	bool bAny = false;
	for ( const char ch : szText )
	{
		if ( ch == '"' )
		{
			bInQuotes = !bInQuotes;
			bAny = true;
			continue;
		}
		if ( ( ch == ' ' || ch == '\t' ) && !bInQuotes )
		{
			if ( bAny )
			{
				arguments.push_back( szToken );
			}
			szToken.clear();
			bAny = false;
			continue;
		}
		szToken += ch;
		bAny = true;
	}
	if ( bAny )
	{
		arguments.push_back( szToken );
	}
	return arguments;
}

//-----------------------------------------------------------------------------
// Purpose: What Hammer's Normal tab runs: each compiler it was told to, the copy into the
//          game's maps folder, then the game (which it doesn't wait for).
//-----------------------------------------------------------------------------
CompileSequence_t SequenceFromNormal( const NormalOptions_t &options )
{
	CompileSequence_t sequence;
	sequence.m_szName = "Normal";
	if ( options.m_nBsp != NormalOptions_t::Bsp_t::BSP_NO )
	{
		sequence.m_Commands.push_back( Command( "$bsp_exe", std::string{ "-game $gamedir" } + ( options.m_nBsp == NormalOptions_t::Bsp_t::BSP_ONLY_ENTITIES ? " -onlyents" : "" ) + " $path\\$file" ) );
	}
	if ( options.m_nVis != NormalOptions_t::Vis_t::VIS_NO )
	{
		sequence.m_Commands.push_back( Command( "$vis_exe", std::string{ "-game $gamedir" } + ( options.m_nVis == NormalOptions_t::Vis_t::VIS_FAST ? " -fast" : "" ) + " $path\\$file" ) );
	}
	if ( options.m_nRad != NormalOptions_t::Rad_t::RAD_NO )
	{
		sequence.m_Commands.push_back( Command( "$light_exe", std::string{ "-game $gamedir" } + ( options.m_bHdr ? " -both" : "" ) + ( options.m_nRad == NormalOptions_t::Rad_t::RAD_FAST ? " -fast" : "" ) + " $path\\$file" ) );
	}
	sequence.m_Commands.push_back( Command( "", "$path\\$file.bsp $bspdir\\$file.bsp", COMPILE_SPECIAL_COPY ) );
	if ( options.m_bRun )
	{
		sequence.m_Commands.push_back( Command( "$game_exe", std::string{ "-dev -console -game $gamedir +map $file" } + ( options.m_szGameParameters.empty() ? "" : " " + options.m_szGameParameters ), COMPILE_SPECIAL_RUN, false ) );
	}
	return sequence;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer++'s own list, as its hammerplusplus_sequences_default.cfg has it.
//-----------------------------------------------------------------------------
std::vector<CompileSequence_t> BuiltinSequences()
{
	auto make = []( std::string_view szName, std::string_view szVis, std::string_view szLight, bool bOnlyEntities, std::string_view szGame ) {
		CompileSequence_t sequence;
		sequence.m_szName = std::string{ szName };
		sequence.m_Commands.push_back( Command( "$bsp_exe", std::string{ "-game $gamedir" } + ( bOnlyEntities ? " -onlyents" : "" ) + " $path\\$file" ) );
		if ( !bOnlyEntities )
		{
			sequence.m_Commands.push_back( Command( "$vis_exe", std::string{ "-game $gamedir" } + std::string{ szVis } + " $path\\$file" ) );
			sequence.m_Commands.push_back( Command( "$light_exe", std::string{ "-game $gamedir" } + std::string{ szLight } + " $path\\$file" ) );
		}
		sequence.m_Commands.push_back( Command( "", "$path\\$file.bsp $bspdir\\$file.bsp", COMPILE_SPECIAL_COPY ) );
		sequence.m_Commands.push_back( Command( "$game_exe", std::string{ "-dev -console -allowdebug -hijack -game $gamedir +map $file" } + std::string{ szGame }, COMPILE_SPECIAL_RUN, false ) );
		return sequence;
	};
	std::vector<CompileSequence_t> sequences{
		make( "Default", "", "", false, "" ),
		make( "Fast", " -fast", " -bounce 2 -noextra", false, "" ),
		make( "LDR + HDR Full Compile", "", " -both", false, "" ),
		make( "LDR + HDR Full Compile -final (slow!)", "", " -both -final", false, "" ),
		make( "Only Entities", "", "", true, "" ),
	};
	for ( const auto &[ szName, szSwitch ] : { std::pair{ "Run Map in Engine Fullscreen", " -fullscreen" }, std::pair{ "Run Map in Engine Windowed", " -sw" } } )
	{
		CompileSequence_t sequence;
		sequence.m_szName = szName;
		sequence.m_Commands.push_back( Command( "$game_exe", std::string{ "-dev -console -allowdebug -hijack -game $gamedir +map $file" } + szSwitch, COMPILE_SPECIAL_RUN, false ) );
		sequences.push_back( std::move( sequence ) );
	}
	return sequences;
}

//-----------------------------------------------------------------------------
// Purpose: "Command Sequences" { "Name" { "1" { enable specialcmd run parms ... } } }.
//-----------------------------------------------------------------------------
std::vector<CompileSequence_t> ReadSequencesText( std::string_view szText )
{
	std::vector<CompileSequence_t> sequences;
	const kv1::CParseResult parsed = kv1::Parse( szText );
	for ( const kv1::CNode &root : parsed.m_Document.m_Roots )
	{
		if ( !root.IsBlock() )
		{
			continue;
		}
		for ( const kv1::CNode &node : root.GetChildren() )
		{
			if ( !node.IsBlock() )
			{
				continue;
			}
			CompileSequence_t sequence;
			sequence.m_szName = node.GetKey();
			std::vector<std::pair<long long, CompileCommand_t>> numbered;
			for ( const kv1::CNode &entry : node.GetChildren() )
			{
				if ( !entry.IsBlock() )
				{
					continue;
				}
				CompileCommand_t command;
				auto value = [ & ]( const char *pszKey ) {
					const kv1::CNode *pChild = entry.Find( pszKey );
					return pChild ? pChild->GetValue() : std::string{};
				};
				command.m_bEnabled = value( "enable" ) != "0";
				command.m_nSpecial = static_cast<int>( ParseNumber<long long>( value( "specialcmd" ) ).value_or( 0 ) );
				command.m_szRun = value( "run" );
				command.m_szParms = value( "parms" );
				command.m_bEnsureFile = value( "ensurecheck" ) == "1";
				command.m_szEnsure = value( "ensurefn" );
				command.m_bWait = value( "nowait" ) != "1";
				numbered.emplace_back( ParseNumber<long long>( entry.GetKey() ).value_or( static_cast<long long>( numbered.size() ) ), std::move( command ) );
			}
			std::ranges::stable_sort( numbered, {}, &std::pair<long long, CompileCommand_t>::first );
			for ( auto &[ n, command ] : numbered )
			{
				sequence.m_Commands.push_back( std::move( command ) );
			}
			sequences.push_back( std::move( sequence ) );
		}
	}
	return sequences;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string WriteSequencesText( const std::vector<CompileSequence_t> &sequences )
{
	kv1::CDocument document;
	kv1::CNode &root = document.AppendRoot( kv1::CNode::MakeBlock( "Command Sequences" ) );
	for ( const CompileSequence_t &sequence : sequences )
	{
		kv1::CNode &node = root.AppendChild( kv1::CNode::MakeBlock( sequence.m_szName ) );
		for ( std::size_t i = 0; i < sequence.m_Commands.size(); i++ )
		{
			const CompileCommand_t &command = sequence.m_Commands[ i ];
			kv1::CNode &entry = node.AppendChild( kv1::CNode::MakeBlock( std::to_string( i + 1 ) ) );
			entry.AppendChild( kv1::CNode::MakePair( "enable", command.m_bEnabled ? "1" : "0" ) );
			entry.AppendChild( kv1::CNode::MakePair( "specialcmd", std::to_string( command.m_nSpecial ) ) );
			if ( command.m_nSpecial == COMPILE_SPECIAL_RUN )
			{
				entry.AppendChild( kv1::CNode::MakePair( "run", command.m_szRun ) );
			}
			entry.AppendChild( kv1::CNode::MakePair( "parms", command.m_szParms ) );
			if ( command.m_bEnsureFile )
			{
				entry.AppendChild( kv1::CNode::MakePair( "ensurecheck", "1" ) );
				entry.AppendChild( kv1::CNode::MakePair( "ensurefn", command.m_szEnsure ) );
			}
			if ( !command.m_bWait )
			{
				entry.AppendChild( kv1::CNode::MakePair( "nowait", "1" ) );
			}
		}
	}
	return kv1::Write( document );
}

//-----------------------------------------------------------------------------
// Purpose: The binary layout the Command Sequences article gives: a 31-byte header, a float
//          version, a count, then each sequence's 128-byte name and its commands (260-byte
//          strings; version 0.2 adds a "no wait" flag).
//-----------------------------------------------------------------------------
std::vector<CompileSequence_t> ReadCmdSeq( std::span<const std::byte> bytes )
{
	std::vector<CompileSequence_t> sequences;
	constexpr std::string_view HEADER = "Worldcraft Command Sequences\r\n\x1a";
	if ( bytes.size() < HEADER.size() + 8 || std::memcmp( bytes.data(), HEADER.data(), HEADER.size() ) != 0 )
	{
		return sequences;
	}
	std::size_t nAt = HEADER.size();
	float flVersion = 0.0f;
	std::memcpy( &flVersion, bytes.data() + nAt, 4 );
	nAt += 4;
	const bool bWaitFlag = flVersion > 0.15f;
	const std::int32_t nSequences = Int32( bytes, nAt );
	nAt += 4;
	for ( std::int32_t s = 0; s < nSequences && nAt + 132 <= bytes.size(); s++ )
	{
		CompileSequence_t sequence;
		sequence.m_szName = FixedString( bytes, nAt, 128 );
		nAt += 128;
		const std::int32_t nCommands = Int32( bytes, nAt );
		nAt += 4;
		for ( std::int32_t c = 0; c < nCommands; c++ )
		{
			const std::size_t nSize = 4 + 4 + 260 + 260 + 4 + 4 + 260 + 4 + ( bWaitFlag ? 4 : 0 );
			if ( nAt + nSize > bytes.size() )
			{
				return sequences;
			}
			CompileCommand_t command;
			command.m_bEnabled = Int32( bytes, nAt ) != 0;
			command.m_nSpecial = Int32( bytes, nAt + 4 );
			command.m_szRun = FixedString( bytes, nAt + 8, 260 );
			command.m_szParms = FixedString( bytes, nAt + 268, 260 );
			command.m_bEnsureFile = Int32( bytes, nAt + 532 ) != 0;
			command.m_szEnsure = FixedString( bytes, nAt + 536, 260 );
			command.m_bWait = !bWaitFlag || Int32( bytes, nAt + 800 ) == 0;
			nAt += nSize;
			sequence.m_Commands.push_back( std::move( command ) );
		}
		sequences.push_back( std::move( sequence ) );
	}
	return sequences;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<ResolvedCommand_t> ResolveSequence( const CompileSequence_t &sequence, const CompileVariables_t &variables )
{
	std::vector<ResolvedCommand_t> resolved;
	for ( const CompileCommand_t &command : sequence.m_Commands )
	{
		if ( !command.m_bEnabled )
		{
			continue;
		}
		ResolvedCommand_t out;
		out.m_nSpecial = command.m_nSpecial;
		out.m_bWait = command.m_bWait;
		out.m_szEnsure = command.m_bEnsureFile ? ExpandParameters( command.m_szEnsure, variables ) : std::string{};
		const std::string szParms = ExpandParameters( command.m_szParms, variables );
		const std::vector<std::string> arguments = SplitArguments( szParms );
		switch ( command.m_nSpecial )
		{
			case COMPILE_SPECIAL_RUN:
			{
				const std::vector<std::string> program = SplitArguments( ExpandParameters( command.m_szRun, variables ) );
				out.m_szProgram = program.empty() ? std::string{} : program.front();
				out.m_Arguments = arguments;
				out.m_szShown = ExpandParameters( command.m_szRun, variables ) + ( szParms.empty() ? "" : " " + szParms );
				break;
			}
			case COMPILE_SPECIAL_COPY:
			case COMPILE_SPECIAL_RENAME:
				out.m_szFrom = arguments.size() > 0 ? arguments[ 0 ] : std::string{};
				out.m_szTo = arguments.size() > 1 ? arguments[ 1 ] : std::string{};
				out.m_szShown = std::string{ command.m_nSpecial == COMPILE_SPECIAL_COPY ? "copy " : "rename " } + szParms;
				break;
			default:
				out.m_szFrom = arguments.empty() ? szParms : arguments[ 0 ];
				out.m_szShown = std::string{ command.m_nSpecial == COMPILE_SPECIAL_DELETE ? "delete " : "cd " } + szParms;
				break;
		}
		resolved.push_back( std::move( out ) );
	}
	return resolved;
}

//-----------------------------------------------------------------------------
// Purpose: The tools' banners say which is talking; the lines the Compile Errors article lists
//          are picked out, with a brush number or a point where they give one.
//-----------------------------------------------------------------------------
CompileReport_t ParseCompileOutput( std::string_view szLog )
{
	CompileReport_t report;
	std::string szTool;
	static const std::regex brushPattern( R"([Bb]rush (\d+))" );
	static const std::regex leakPattern( R"(Entity (\S+) \(([-0-9.]+) ([-0-9.]+) ([-0-9.]+)\) leaked!)" );
	std::size_t nStart = 0;
	while ( nStart <= szLog.size() )
	{
		std::size_t nEnd = szLog.find( '\n', nStart );
		if ( nEnd == std::string_view::npos )
		{
			nEnd = szLog.size();
		}
		std::string szLine{ szLog.substr( nStart, nEnd - nStart ) };
		nStart = nEnd + 1;
		while ( !szLine.empty() && ( szLine.back() == '\r' || szLine.back() == ' ' ) )
		{
			szLine.pop_back();
		}
		if ( szLine.empty() )
		{
			continue;
		}
		const std::string szLower = Lower( szLine );
		// Which tool: its banner ("Valve Software - vbsp.exe", "VBSP++ ...") or the command
		// line the runner logged.
		for ( const char *pszTool : { "vbsp", "vvis", "vrad" } )
		{
			if ( szLower.find( pszTool ) != std::string::npos && ( szLower.find( "valve software" ) != std::string::npos || szLower.find( "++" ) != std::string::npos || szLower.starts_with( "running" ) || szLower.starts_with( "> " ) ) )
			{
				szTool = pszTool;
			}
		}
		auto add = [ & ]( bool bError ) {
			CompileProblem_t problem;
			problem.m_szTool = szTool;
			problem.m_bError = bError;
			problem.m_szMessage = szLine;
			std::smatch match;
			if ( std::regex_search( szLine, match, brushPattern ) )
			{
				problem.m_nBrushId = ParseNumber<int>( match[ 1 ].str() );
			}
			problem.m_Point = PointIn( szLine );
			report.m_Problems.push_back( std::move( problem ) );
		};
		std::smatch leak;
		if ( std::regex_search( szLine, leak, leakPattern ) )
		{
			report.m_bLeaked = true;
			report.m_szLeakEntity = leak[ 1 ].str();
			report.m_LeakAt = Point_t{ ParseNumber<double>( leak[ 2 ].str() ).value_or( 0 ), ParseNumber<double>( leak[ 3 ].str() ).value_or( 0 ), ParseNumber<double>( leak[ 4 ].str() ).value_or( 0 ) };
			add( true );
			continue;
		}
		if ( szLower.find( "**** leaked ****" ) != std::string::npos || szLower.find( "--- map leaked ---" ) != std::string::npos || szLower.find( "--- areaportal leaked ---" ) != std::string::npos )
		{
			report.m_bLeaked = true;
			add( true );
			continue;
		}
		if ( szLower.find( "couldn't read" ) != std::string::npos && szLower.find( ".prt" ) != std::string::npos )
		{
			report.m_bNoPortalFile = true;
			add( true );
			continue;
		}
		const bool bWarning = szLower.find( "warning" ) != std::string::npos || szLower.find( "not found" ) != std::string::npos || szLower.find( "texture axis perpendicular" ) != std::string::npos || szLower.find( "too many light styles" ) != std::string::npos;
		const bool bError = szLower.starts_with( "error" ) || szLower.find( "bad normal" ) != std::string::npos || szLower.find( "too many t-junctions" ) != std::string::npos || szLower.find( "error opening" ) != std::string::npos || szLower.find( "cannot find the file" ) != std::string::npos;
		if ( bError )
		{
			add( true );
		}
		else if ( bWarning )
		{
			add( false );
		}
	}
	return report;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Point_t> ReadPointfile( std::string_view szText )
{
	std::vector<Point_t> points;
	std::size_t nStart = 0;
	while ( nStart < szText.size() )
	{
		std::size_t nEnd = szText.find( '\n', nStart );
		if ( nEnd == std::string_view::npos )
		{
			nEnd = szText.size();
		}
		std::string_view szLine = szText.substr( nStart, nEnd - nStart );
		nStart = nEnd + 1;
		if ( szLine.ends_with( '\r' ) )
		{
			szLine.remove_suffix( 1 );
		}
		const std::vector<std::string> numbers = SplitArguments( szLine );
		if ( numbers.size() < 3 )
		{
			continue;
		}
		Point_t p{};
		bool bOk = true;
		for ( std::size_t a = 0; a < 3; a++ )
		{
			const auto fl = ParseNumber<double>( numbers[ a ] );
			bOk = bOk && fl.has_value();
			p[ a ] = fl.value_or( 0.0 );
		}
		if ( bOk )
		{
			points.push_back( p );
		}
	}
	return points;
}

//-----------------------------------------------------------------------------
// Purpose: "PRT1", the leaf count, the portal count, then "<points> <leaf> <leaf> (x y z)..."
//          a line.
//-----------------------------------------------------------------------------
std::vector<Portal_t> ReadPortalFile( std::string_view szText )
{
	std::vector<Portal_t> portals;
	if ( !szText.starts_with( "PRT1" ) )
	{
		return portals;
	}
	std::size_t nStart = 0;
	int nLine = 0;
	while ( nStart < szText.size() )
	{
		std::size_t nEnd = szText.find( '\n', nStart );
		if ( nEnd == std::string_view::npos )
		{
			nEnd = szText.size();
		}
		std::string szLine{ szText.substr( nStart, nEnd - nStart ) };
		nStart = nEnd + 1;
		if ( nLine++ < 3 )
		{
			continue;
		}
		std::ranges::replace( szLine, '\r', ' ' );
		std::ranges::replace( szLine, '(', ' ' );
		std::ranges::replace( szLine, ')', ' ' );
		const std::vector<std::string> numbers = SplitArguments( szLine );
		if ( numbers.size() < 3 )
		{
			continue;
		}
		Portal_t portal;
		const int nPoints = ParseNumber<int>( numbers[ 0 ] ).value_or( 0 );
		portal.m_nLeafA = ParseNumber<int>( numbers[ 1 ] ).value_or( 0 );
		portal.m_nLeafB = ParseNumber<int>( numbers[ 2 ] ).value_or( 0 );
		for ( int i = 0; i < nPoints && 3 + static_cast<std::size_t>( i ) * 3 + 2 < numbers.size(); i++ )
		{
			const std::size_t n = 3 + static_cast<std::size_t>( i ) * 3;
			portal.m_Points.push_back( { ParseNumber<double>( numbers[ n ] ).value_or( 0 ), ParseNumber<double>( numbers[ n + 1 ] ).value_or( 0 ), ParseNumber<double>( numbers[ n + 2 ] ).value_or( 0 ) } );
		}
		portals.push_back( std::move( portal ) );
	}
	return portals;
}

} // namespace xv::map
