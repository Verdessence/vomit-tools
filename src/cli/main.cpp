//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: vomit-tools-cli: the texture tools from the command line. Make VTFs from pictures,
//          change them, save their pictures, print what they hold, and make file-manager
//          thumbnails. Uses the core library only (no Qt).
//
//===========================================================================//
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "sky/Sky.h"
#include "texture/Build.h"
#include "texture/Image.h"
#include "texture/Sheet.h"
#include "texture/Vtf.h"

int ModelCommand( int argc, char **argv );
int MapCommand( int argc, char **argv );

namespace fs = std::filesystem;
using namespace xv::texture;

namespace
{

const char *const USAGE = R"(vomit-tools-cli: Valve texture (VTF) tools

  vomit-tools-cli create  <pictures or folders...> [-o out] [options]
  vomit-tools-cli edit    <vtf> [-o out] [options]
  vomit-tools-cli extract <vtfs or folders...> [-o folder] [--type png] [--mip N]
  vomit-tools-cli info    <vtfs...> [--kv]
  vomit-tools-cli thumbnail <vtf> <out.png> [size]
  vomit-tools-cli model --help
  vomit-tools-cli map --help

create:
  --format F            DXT1, DXT5, BGRA8888... (default: DXT5 with alpha, else DXT1)
  --alpha-format F      format for pictures with see-through parts
  --version 7.N         1 to 5 (default 7.2; 7.3+ for sheets)
  --no-mips             no mipmaps
  --filter F            box, triangle, mitchell, kaiser (default kaiser)
  --resize R            keep, nearest, up, down, 4, or an exact size such as 300x200
                        (default nearest power of two)
  --max-size N          largest side
  --keep-aspect         keep the shape when resizing
  --flag NAME           set a flag (CLAMPS, NORMAL, ...); repeatable
  --spray               make a spray: under 512 KB, at most 4096 on a side, no mipmaps
                        (smaller, then fewer frames, until it fits; other choices ignored)
  --no-thumbnail        no low-res copy
  --frames | --cube | --volume   all inputs make one texture, as frames, cube faces
                        (rt lf bk ft up dn) or volume slices
  --panorama SIZE       the input is an equirectangular panorama: make a cube map, SIZE a face
  --yaw DEGREES         turn the panorama
  --normal-from-height SCALE   make a normal map from height
  --green-up            OpenGL-style normal map
  --distance-alpha SPREAD      $distancealpha from the alpha
  --strip-alpha         make every picture solid
  --sheet FILE.mks      pack a sprite sheet (pictures named in the .mks, beside it)
  --watch               keep running, and make it again when an input changes
  --no-recurse          don't look in sub-folders

edit (pictures untouched unless a format or mipmaps are asked for):
  --set-version 7.N  --set-flag NAME  --clear-flag NAME  --first-frame N  --bump-scale X
  --reflectivity auto  --thumbnail keep|remove|make  --remove-resource TAG  --kvd FILE
  --set-format F        write it again in another format
  --recompute-mips [F]  make the mipmaps again (with filter F)

extract:  --type png|tga|bmp|jpg|hdr|qoi|pfm   --mip N (default 0)
info:     --kv  prints KeyValues instead of plain text
)";

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string szText )
{
	std::ranges::transform( szText, szText.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> ReadFile( const fs::path &path )
{
	std::ifstream file( path, std::ios::binary );
	if ( !file )
	{
		return std::nullopt;
	}
	std::vector<char> data( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
	return std::vector<std::byte>( reinterpret_cast<const std::byte *>( data.data() ), reinterpret_cast<const std::byte *>( data.data() ) + data.size() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WriteFile( const fs::path &path, const std::vector<std::byte> &bytes )
{
	if ( bytes.empty() )
	{
		return false;
	}
	std::error_code ec;
	if ( path.has_parent_path() )
	{
		fs::create_directories( path.parent_path(), ec );
	}
	std::ofstream file( path, std::ios::binary );
	file.write( reinterpret_cast<const char *>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
	return static_cast<bool>( file );
}

//-----------------------------------------------------------------------------
// Purpose: A format by name, ignoring case.
//-----------------------------------------------------------------------------
std::optional<VtfFormat_t> FormatByName( const std::string &szName )
{
	for ( int n = 0; n <= static_cast<int>( VtfFormat_t::RGBA32323232F ); n++ )
	{
		if ( Lower( std::string{ FormatName( static_cast<VtfFormat_t>( n ) ) } ) == Lower( szName ) )
		{
			return static_cast<VtfFormat_t>( n );
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: A flag by name ("CLAMPS"), ignoring case.
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> FlagByName( const std::string &szName )
{
	for ( int i = 0; i < 32; i++ )
	{
		const std::uint32_t nBit = 1u << i;
		const auto names = FlagNames( nBit );
		if ( !names.empty() && Lower( names.front() ) == Lower( szName ) )
		{
			return nBit;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<MipFilter_t> FilterByName( const std::string &szName )
{
	const std::string szLower = Lower( szName );
	if ( szLower == "box" )
		return MipFilter_t::MIP_FILTER_BOX;
	if ( szLower == "triangle" )
		return MipFilter_t::MIP_FILTER_TRIANGLE;
	if ( szLower == "mitchell" )
		return MipFilter_t::MIP_FILTER_MITCHELL;
	if ( szLower == "kaiser" )
		return MipFilter_t::MIP_FILTER_KAISER;
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: A picture file (or a VTF's full-size images) as sources.
//-----------------------------------------------------------------------------
std::vector<SourceImage_t> ReadPictures( const fs::path &path, std::string *pError )
{
	const auto data = ReadFile( path );
	if ( !data )
	{
		*pError = "can't read it";
		return {};
	}
	if ( Lower( path.extension().string() ) == ".vtf" )
	{
		const auto vtf = ReadVtf( *data, pError );
		return vtf ? SourcesOf( *vtf ) : std::vector<SourceImage_t>{};
	}
	std::vector<SourceImage_t> sources;
	for ( CImage &frame : LoadImageFrames( *data, pError ) )
	{
		sources.push_back( { std::move( frame ), {} } );
	}
	if ( !sources.empty() )
	{
		if ( auto light = LoadFloatImage( *data ) )
		{
			sources.front().m_Light = std::move( *light );
		}
	}
	return sources;
}

//-----------------------------------------------------------------------------
// Purpose: The files given, with folders opened (sub-folders too unless bRecurse is off).
//-----------------------------------------------------------------------------
std::vector<fs::path> Expand( const std::vector<std::string> &inputs, bool bRecurse, bool bVtf )
{
	std::vector<fs::path> files;
	for ( const std::string &szInput : inputs )
	{
		const fs::path path{ szInput };
		std::error_code ec;
		if ( !fs::is_directory( path, ec ) )
		{
			files.push_back( path );
			continue;
		}
		const auto add = [ & ]( const fs::directory_entry &entry ) {
			if ( !entry.is_regular_file() )
			{
				return;
			}
			const std::string szExtension = Lower( entry.path().extension().string() );
			const bool bIsVtf = szExtension == ".vtf";
			if ( bVtf ? bIsVtf : !bIsVtf && szExtension.size() > 1 && IsImageExtension( szExtension.substr( 1 ) ) )
			{
				files.push_back( entry.path() );
			}
		};
		if ( bRecurse )
		{
			for ( const auto &entry : fs::recursive_directory_iterator( path, ec ) )
			{
				add( entry );
			}
		}
		else
		{
			for ( const auto &entry : fs::directory_iterator( path, ec ) )
			{
				add( entry );
			}
		}
	}
	std::ranges::sort( files );
	return files;
}

struct Arguments_t
{
	std::string m_szMode;
	std::vector<std::string> m_Inputs;
	std::map<std::string, std::vector<std::string>> m_Options; // "--format" -> values
	std::vector<std::string> m_Switches;

	[[nodiscard]] bool Has( const std::string &szName ) const { return std::ranges::find( m_Switches, szName ) != m_Switches.end() || m_Options.contains( szName ); }
	[[nodiscard]] std::optional<std::string> Value( const std::string &szName ) const
	{
		const auto it = m_Options.find( szName );
		return it == m_Options.end() || it->second.empty() ? std::nullopt : std::optional{ it->second.back() };
	}
};

// Options that take a value after them.
const std::vector<std::string> VALUED{ "-o", "--output", "--format", "--alpha-format", "--version", "--filter", "--resize", "--max-size", "--flag", "--panorama", "--yaw", "--normal-from-height",
	"--distance-alpha", "--sheet", "--set-version", "--set-flag", "--clear-flag", "--first-frame", "--bump-scale", "--reflectivity", "--thumbnail", "--remove-resource", "--kvd", "--set-format",
	"--type", "--mip" };

//-----------------------------------------------------------------------------
// Purpose: The command line: a mode, inputs, options with values and switches.
//-----------------------------------------------------------------------------
Arguments_t Parse( int argc, char **argv )
{
	Arguments_t arguments;
	for ( int i = 1; i < argc; i++ )
	{
		const std::string szArgument = argv[ i ];
		if ( arguments.m_szMode.empty() && !szArgument.starts_with( '-' ) )
		{
			arguments.m_szMode = Lower( szArgument );
		}
		else if ( std::ranges::find( VALUED, szArgument ) != VALUED.end() && i + 1 < argc )
		{
			arguments.m_Options[ szArgument == "--output" ? "-o" : szArgument ].push_back( argv[ ++i ] );
		}
		else if ( szArgument == "--recompute-mips" )
		{
			arguments.m_Switches.push_back( szArgument );
			if ( i + 1 < argc && FilterByName( argv[ i + 1 ] ) )
			{
				arguments.m_Options[ "--recompute-mips" ].push_back( argv[ ++i ] );
			}
		}
		else if ( szArgument.starts_with( "--" ) || szArgument == "-h" )
		{
			arguments.m_Switches.push_back( szArgument );
		}
		else
		{
			arguments.m_Inputs.push_back( szArgument );
		}
	}
	return arguments;
}

//-----------------------------------------------------------------------------
// Purpose: The making options from the command line; nullopt (after saying why) when one is wrong.
//-----------------------------------------------------------------------------
std::optional<BuildOptions_t> BuildOptionsFrom( const Arguments_t &arguments, bool bAlpha )
{
	BuildOptions_t options;
	options.m_Write.m_nFormat = bAlpha ? VtfFormat_t::DXT5 : VtfFormat_t::DXT1;
	options.m_Write.m_nMipFilter = MipFilter_t::MIP_FILTER_KAISER;
	options.m_Write.m_nVersionMinor = 2;
	const auto fail = []( const std::string &szWhy ) -> std::optional<BuildOptions_t> {
		std::fprintf( stderr, "%s\n", szWhy.c_str() );
		return std::nullopt;
	};
	if ( const auto format = arguments.Value( "--format" ) )
	{
		const auto nFormat = FormatByName( *format );
		if ( !nFormat || !CanEncode( *nFormat ) )
		{
			return fail( "unknown format " + *format );
		}
		options.m_Write.m_nFormat = *nFormat;
	}
	if ( const auto format = arguments.Value( "--alpha-format" ) )
	{
		const auto nFormat = FormatByName( *format );
		if ( !nFormat || !CanEncode( *nFormat ) )
		{
			return fail( "unknown format " + *format );
		}
		options.m_nAlphaFormat = *nFormat;
	}
	if ( const auto version = arguments.Value( "--version" ) )
	{
		options.m_Write.m_nVersionMinor = std::clamp( std::atoi( version->substr( version->find( '.' ) == std::string::npos ? 0 : version->find( '.' ) + 1 ).c_str() ), 1, 5 );
	}
	options.m_Write.m_bMipmaps = !arguments.Has( "--no-mips" );
	options.m_Write.m_bLowRes = !arguments.Has( "--no-thumbnail" );
	if ( const auto filter = arguments.Value( "--filter" ) )
	{
		const auto nFilter = FilterByName( *filter );
		if ( !nFilter )
		{
			return fail( "unknown filter " + *filter );
		}
		options.m_Write.m_nMipFilter = *nFilter;
	}
	if ( const auto resize = arguments.Value( "--resize" ) )
	{
		const std::string szResize = Lower( *resize );
		if ( const auto nX = szResize.find( 'x' ); nX != std::string::npos )
		{
			options.m_nResize = ResizeMode_t::RESIZE_EXACT;
			options.m_nExactWidth = std::atoi( szResize.substr( 0, nX ).c_str() );
			options.m_nExactHeight = std::atoi( szResize.substr( nX + 1 ).c_str() );
			if ( options.m_nExactWidth < 1 || options.m_nExactHeight < 1 || options.m_nExactWidth > 65535 || options.m_nExactHeight > 65535 )
			{
				return fail( "bad size " + *resize );
			}
		}
		else
		{
			options.m_nResize = szResize == "keep" ? ResizeMode_t::RESIZE_KEEP : szResize == "up" ? ResizeMode_t::RESIZE_UP
				: szResize == "down"                                                              ? ResizeMode_t::RESIZE_DOWN
				: szResize == "4"                                                                 ? ResizeMode_t::RESIZE_MULTIPLE_OF_4
																								  : ResizeMode_t::RESIZE_NEAREST;
		}
	}
	if ( const auto size = arguments.Value( "--max-size" ) )
	{
		options.m_nMaxSize = std::atoi( size->c_str() );
	}
	options.m_bKeepAspect = arguments.Has( "--keep-aspect" );
	if ( const auto it = arguments.m_Options.find( "--flag" ); it != arguments.m_Options.end() )
	{
		for ( const std::string &szFlag : it->second )
		{
			const auto nFlag = FlagByName( szFlag );
			if ( !nFlag )
			{
				return fail( "unknown flag " + szFlag );
			}
			options.m_Write.m_nFlags |= *nFlag;
		}
	}
	if ( const auto scale = arguments.Value( "--normal-from-height" ) )
	{
		options.m_bHeightToNormal = true;
		options.m_flNormalScale = static_cast<float>( std::atof( scale->c_str() ) );
	}
	options.m_bGreenUp = arguments.Has( "--green-up" );
	if ( const auto spread = arguments.Value( "--distance-alpha" ) )
	{
		options.m_bDistanceAlpha = true;
		options.m_flDistanceSpread = static_cast<float>( std::atof( spread->c_str() ) );
	}
	options.m_bStripAlpha = arguments.Has( "--strip-alpha" );
	options.m_nFaces = arguments.Has( "--cube" ) || arguments.Has( "--panorama" ) ? 6 : 1;
	return options;
}

//-----------------------------------------------------------------------------
// Purpose: create: one texture per picture, or one from them all; a sheet from an .mks.
//-----------------------------------------------------------------------------
int Create( const Arguments_t &arguments )
{
	const bool bTogether = arguments.Has( "--frames" ) || arguments.Has( "--cube" ) || arguments.Has( "--volume" );
	const std::vector<fs::path> files = Expand( arguments.m_Inputs, !arguments.Has( "--no-recurse" ), false );
	const auto output = arguments.Value( "-o" );
	int nFailed = 0;

	if ( const auto mks = arguments.Value( "--sheet" ) )
	{
		const auto text = ReadFile( *mks );
		if ( !text )
		{
			std::fprintf( stderr, "%s: can't read it\n", mks->c_str() );
			return 1;
		}
		const CMks parsed = ParseMks( std::string_view{ reinterpret_cast<const char *>( text->data() ), text->size() } );
		for ( const std::string &szProblem : parsed.m_Problems )
		{
			std::fprintf( stderr, "%s: %s\n", mks->c_str(), szProblem.c_str() );
		}
		std::map<std::string, CImage> pictures;
		for ( const MksSequence_t &sequence : parsed.m_Sequences )
		{
			for ( const MksFrame_t &frame : sequence.m_Frames )
			{
				for ( const std::string &szName : frame.m_Files )
				{
					std::string szError;
					const auto read = ReadPictures( fs::path{ *mks }.parent_path() / szName, &szError );
					if ( !read.empty() )
					{
						pictures[ szName ] = read.front().m_Image;
					}
				}
			}
		}
		std::string szError;
		const auto packed = PackSheet( parsed, pictures, &szError );
		auto options = BuildOptionsFrom( arguments, true );
		if ( !packed || !options )
		{
			std::fprintf( stderr, "%s: %s\n", mks->c_str(), szError.c_str() );
			return 1;
		}
		options->m_nResize = ResizeMode_t::RESIZE_KEEP;
		options->m_Write.m_nVersionMinor = std::max( 3, options->m_Write.m_nVersionMinor );
		options->m_Write.m_Resources.push_back( { SHEET_RESOURCE, 0, 0, WriteSheet( packed->m_Sheet ) } );
		const fs::path target = output ? fs::path{ *output } : fs::path{ *mks }.replace_extension( ".vtf" );
		const std::vector<SourceImage_t> sources{ { packed->m_Atlas, {} } };
		if ( !WriteFile( target, BuildVtf( sources, *options, &szError ) ) )
		{
			std::fprintf( stderr, "%s: %s\n", target.string().c_str(), szError.c_str() );
			return 1;
		}
		std::printf( "%s\n", target.string().c_str() );
		return 0;
	}

	const auto make = [ & ]( const std::vector<fs::path> &group, const fs::path &target ) {
		std::vector<SourceImage_t> sources;
		for ( const fs::path &file : group )
		{
			std::string szError;
			auto read = ReadPictures( file, &szError );
			if ( read.empty() )
			{
				std::fprintf( stderr, "%s: %s\n", file.string().c_str(), szError.c_str() );
				return false;
			}
			std::ranges::move( read, std::back_inserter( sources ) );
		}
		if ( const auto size = arguments.Value( "--panorama" ) )
		{
			const SourceImage_t panorama = sources.front();
			sources.clear();
			const float flYaw = arguments.Value( "--yaw" ) ? static_cast<float>( std::atof( arguments.Value( "--yaw" )->c_str() ) ) : 0.0f;
			for ( CFloatImage &face : xv::sky::CubeMapFromPanorama( panorama.m_Light.Empty() ? ToFloat( panorama.m_Image ) : panorama.m_Light, std::atoi( size->c_str() ), flYaw ) )
			{
				sources.push_back( { ToEightBit( face ), panorama.m_Light.Empty() ? CFloatImage{} : std::move( face ) } );
			}
		}
		auto options = BuildOptionsFrom( arguments, HasClearTexels( sources ) );
		if ( !options )
		{
			return false;
		}
		if ( arguments.Has( "--volume" ) )
		{
			options->m_Write.m_nDepth = static_cast<int>( sources.size() );
		}
		std::string szError;
		const auto bytes = arguments.Has( "--spray" ) ? BuildSpray( sources, &szError ) : BuildVtf( sources, *options, &szError );
		if ( !WriteFile( target, bytes ) )
		{
			std::fprintf( stderr, "%s: %s\n", target.string().c_str(), szError.empty() ? "can't write it" : szError.c_str() );
			return false;
		}
		std::printf( "%s\n", target.string().c_str() );
		return true;
	};
	// Each output and what it is made from.
	std::vector<std::pair<std::vector<fs::path>, fs::path>> jobs;
	if ( bTogether || arguments.Has( "--panorama" ) )
	{
		if ( !files.empty() )
		{
			jobs.push_back( { files, output ? fs::path{ *output } : fs::path{ files.front() }.replace_extension( ".vtf" ) } );
		}
	}
	else
	{
		std::error_code ec;
		const bool bIntoFolder = output && ( files.size() > 1 || fs::is_directory( *output, ec ) );
		for ( const fs::path &file : files )
		{
			const fs::path name = fs::path{ file.filename() }.replace_extension( ".vtf" );
			jobs.push_back( { { file }, bIntoFolder ? fs::path{ *output } / name : output ? fs::path{ *output }
																						  : fs::path{ file }.replace_extension( ".vtf" ) } );
		}
	}
	if ( jobs.empty() )
	{
		std::fprintf( stderr, "no pictures\n" );
		return 1;
	}
	for ( const auto &[ group, target ] : jobs )
	{
		nFailed += make( group, target ) ? 0 : 1;
	}
	if ( !arguments.Has( "--watch" ) )
	{
		return nFailed ? 1 : 0;
	}
	// Watching: made again whenever a picture's time changes.
	std::map<fs::path, fs::file_time_type> seen;
	const auto times = [ & ] {
		std::map<fs::path, fs::file_time_type> now;
		for ( const fs::path &file : Expand( arguments.m_Inputs, !arguments.Has( "--no-recurse" ), false ) )
		{
			std::error_code ec;
			now[ file ] = fs::last_write_time( file, ec );
		}
		return now;
	};
	seen = times();
	std::printf( "watching; Ctrl+C stops\n" );
	std::fflush( stdout );
	while ( true )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
		const auto now = times();
		if ( now == seen )
		{
			continue;
		}
		for ( const auto &[ group, target ] : jobs )
		{
			if ( std::ranges::any_of( group, [ & ]( const fs::path &file ) { return !seen.contains( file ) || seen.at( file ) != now.at( file ); } ) )
			{
				make( group, target );
			}
		}
		seen = now;
		std::fflush( stdout );
	}
}

//-----------------------------------------------------------------------------
// Purpose: edit: the header changed, or (with a format or mipmaps asked for) written again.
//-----------------------------------------------------------------------------
int Edit( const Arguments_t &arguments )
{
	if ( arguments.m_Inputs.size() != 1 )
	{
		std::fprintf( stderr, "edit takes one VTF\n" );
		return 1;
	}
	const fs::path input{ arguments.m_Inputs.front() };
	const auto data = ReadFile( input );
	std::string szError;
	const auto vtf = data ? ReadVtf( *data, &szError ) : std::nullopt;
	if ( !vtf )
	{
		std::fprintf( stderr, "%s: %s\n", input.string().c_str(), data ? szError.c_str() : "can't read it" );
		return 1;
	}
	std::vector<std::byte> bytes = vtf->m_Data;
	// Writing it again first, when a format or new mipmaps are asked for.
	if ( arguments.Has( "--set-format" ) || arguments.Has( "--recompute-mips" ) )
	{
		BuildOptions_t options = OptionsOf( *vtf );
		if ( const auto format = arguments.Value( "--set-format" ) )
		{
			const auto nFormat = FormatByName( *format );
			if ( !nFormat || !CanEncode( *nFormat ) )
			{
				std::fprintf( stderr, "unknown format %s\n", format->c_str() );
				return 1;
			}
			options.m_Write.m_nFormat = *nFormat;
		}
		options.m_Write.m_bMipmaps = true;
		options.m_Write.m_nMipFilter = FilterByName( arguments.Value( "--recompute-mips" ).value_or( "kaiser" ) ).value_or( MipFilter_t::MIP_FILTER_KAISER );
		bytes = BuildVtf( SourcesOf( *vtf ), options, &szError );
	}
	auto current = ReadVtf( bytes, &szError );
	if ( !current )
	{
		std::fprintf( stderr, "%s: %s\n", input.string().c_str(), szError.c_str() );
		return 1;
	}
	VtfHeaderEdit_t edit = HeaderOf( *current );
	if ( const auto version = arguments.Value( "--set-version" ) )
	{
		edit.m_nVersionMinor = std::clamp( std::atoi( version->substr( version->find( '.' ) == std::string::npos ? 0 : version->find( '.' ) + 1 ).c_str() ), 1, 5 );
	}
	for ( const char *pszOption : { "--set-flag", "--clear-flag" } )
	{
		if ( const auto it = arguments.m_Options.find( pszOption ); it != arguments.m_Options.end() )
		{
			for ( const std::string &szFlag : it->second )
			{
				const auto nFlag = FlagByName( szFlag );
				if ( !nFlag )
				{
					std::fprintf( stderr, "unknown flag %s\n", szFlag.c_str() );
					return 1;
				}
				edit.m_nFlags = std::string_view{ pszOption } == "--set-flag" ? edit.m_nFlags | *nFlag : edit.m_nFlags & ~*nFlag;
			}
		}
	}
	if ( const auto frame = arguments.Value( "--first-frame" ) )
	{
		edit.m_nFirstFrame = std::atoi( frame->c_str() );
	}
	if ( const auto scale = arguments.Value( "--bump-scale" ) )
	{
		edit.m_flBumpScale = static_cast<float>( std::atof( scale->c_str() ) );
	}
	if ( arguments.Value( "--reflectivity" ) )
	{
		edit.m_vecReflectivity = Reflectivity( *current );
	}
	if ( const auto thumbnail = arguments.Value( "--thumbnail" ) )
	{
		edit.m_nLowRes = *thumbnail == "remove" ? LowResEdit_t::LOW_RES_EDIT_REMOVE : *thumbnail == "make" ? LowResEdit_t::LOW_RES_EDIT_MAKE
																										   : LowResEdit_t::LOW_RES_EDIT_KEEP;
	}
	if ( const auto it = arguments.m_Options.find( "--remove-resource" ); it != arguments.m_Options.end() )
	{
		for ( const std::string &szTag : it->second )
		{
			std::erase_if( edit.m_Resources, [ &szTag ]( const VtfExtraResource_t &resource ) {
				const std::string szName( reinterpret_cast<const char *>( resource.m_Tag.data() ), 3 );
				return Lower( szName ) == Lower( szTag ) || ( resource.m_Tag == SHEET_RESOURCE && Lower( szTag ) == "sheet" );
			} );
		}
	}
	if ( const auto kvd = arguments.Value( "--kvd" ) )
	{
		const auto text = ReadFile( *kvd );
		if ( !text )
		{
			std::fprintf( stderr, "%s: can't read it\n", kvd->c_str() );
			return 1;
		}
		std::erase_if( edit.m_Resources, []( const VtfExtraResource_t &resource ) { return resource.m_Tag == std::array<std::uint8_t, 3>{ 'K', 'V', 'D' }; } );
		edit.m_Resources.push_back( { { 'K', 'V', 'D' }, 0, 0, *text } );
		edit.m_nVersionMinor = std::max( 3, edit.m_nVersionMinor );
	}
	bytes = RewriteVtf( *current, edit );
	const fs::path target = arguments.Value( "-o" ) ? fs::path{ *arguments.Value( "-o" ) } : input;
	if ( !WriteFile( target, bytes ) )
	{
		std::fprintf( stderr, "%s: can't write it\n", target.string().c_str() );
		return 1;
	}
	std::printf( "%s\n", target.string().c_str() );
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: One image of a texture as a picture file.
//-----------------------------------------------------------------------------
std::vector<std::byte> Picture( const CVtf &vtf, int nMip, int nFrame, int nFace, int nSlice, ImageFileType_t nType )
{
	const VtfInfo_t &info = vtf.m_Info;
	const int nWidth = std::max( 1, info.m_nWidth >> nMip );
	const int nHeight = std::max( 1, info.m_nHeight >> nMip );
	const auto bytes = vtf.Image( nMip, nFrame, nFace, nSlice );
	if ( nType == ImageFileType_t::IMAGE_FILE_TYPE_HDR || nType == ImageFileType_t::IMAGE_FILE_TYPE_PFM )
	{
		const auto light = DecodeFloat( info.m_nFormat, nWidth, nHeight, bytes );
		return light ? SaveFloatImage( *light, nType ) : std::vector<std::byte>{};
	}
	const auto image = DecodeImage( info.m_nFormat, nWidth, nHeight, bytes );
	return image ? SaveImage( *image, nType ) : std::vector<std::byte>{};
}

//-----------------------------------------------------------------------------
// Purpose: extract: every frame, face and slice as pictures.
//-----------------------------------------------------------------------------
int Extract( const Arguments_t &arguments )
{
	static constexpr const char *FACE_SUFFIXES[] = { "rt", "lf", "bk", "ft", "up", "dn" };
	const std::string szType = Lower( arguments.Value( "--type" ).value_or( "" ) );
	const int nMip = std::atoi( arguments.Value( "--mip" ).value_or( "0" ).c_str() );
	int nFailed = 0;
	for ( const fs::path &file : Expand( arguments.m_Inputs, !arguments.Has( "--no-recurse" ), true ) )
	{
		const auto data = ReadFile( file );
		std::string szError;
		const auto vtf = data ? ReadVtf( *data, &szError ) : std::nullopt;
		if ( !vtf )
		{
			std::fprintf( stderr, "%s: %s\n", file.string().c_str(), szError.c_str() );
			nFailed++;
			continue;
		}
		const VtfInfo_t &info = vtf->m_Info;
		// PNG, or HDR for HDR textures, unless asked.
		const std::string szExtension = !szType.empty() ? szType : IsFloatFormat( info.m_nFormat ) ? "hdr"
																								   : "png";
		const fs::path folder = arguments.Value( "-o" ) ? fs::path{ *arguments.Value( "-o" ) } : file.parent_path();
		const int nFaces = info.m_nFaces == 7 ? 6 : info.m_nFaces;
		for ( int nFrame = 0; nFrame < info.m_nFrames; nFrame++ )
		{
			for ( int nFace = 0; nFace < nFaces; nFace++ )
			{
				for ( int nSlice = 0; nSlice < std::max( 1, info.m_nDepth >> nMip ); nSlice++ )
				{
					std::string szName = file.stem().string();
					if ( info.m_nFrames > 1 )
					{
						char szFrame[ 16 ];
						std::snprintf( szFrame, sizeof( szFrame ), "_%03d", nFrame );
						szName += szFrame;
					}
					if ( nFaces == 6 )
					{
						szName += std::string{ "_" } + FACE_SUFFIXES[ nFace ];
					}
					if ( info.m_nDepth > 1 )
					{
						szName += "_slice" + std::to_string( nSlice );
					}
					const fs::path target = folder / ( szName + "." + szExtension );
					if ( WriteFile( target, Picture( *vtf, nMip, nFrame, nFace, nSlice, ImageFileTypeFor( szExtension ) ) ) )
					{
						std::printf( "%s\n", target.string().c_str() );
					}
					else
					{
						std::fprintf( stderr, "%s: can't write it\n", target.string().c_str() );
						nFailed++;
					}
				}
			}
		}
	}
	return nFailed ? 1 : 0;
}

//-----------------------------------------------------------------------------
// Purpose: info: what a VTF holds, as text or KeyValues.
//-----------------------------------------------------------------------------
int Info( const Arguments_t &arguments )
{
	const bool bKv = arguments.Has( "--kv" );
	int nFailed = 0;
	for ( const fs::path &file : Expand( arguments.m_Inputs, false, true ) )
	{
		const auto data = ReadFile( file );
		std::string szError;
		const auto vtf = data ? ReadVtf( *data, &szError ) : std::nullopt;
		if ( !vtf )
		{
			std::fprintf( stderr, "%s: %s\n", file.string().c_str(), szError.c_str() );
			nFailed++;
			continue;
		}
		const VtfInfo_t &info = vtf->m_Info;
		std::vector<std::pair<std::string, std::string>> rows{
			{ "file", file.string() },
			{ "version", std::to_string( info.m_nVersionMajor ) + "." + std::to_string( info.m_nVersionMinor ) },
			{ "width", std::to_string( info.m_nWidth ) },
			{ "height", std::to_string( info.m_nHeight ) },
			{ "depth", std::to_string( info.m_nDepth ) },
			{ "format", std::string{ FormatName( info.m_nFormat ) } },
			{ "mipmaps", std::to_string( info.m_nMipCount ) },
			{ "frames", std::to_string( info.m_nFrames ) },
			{ "first_frame", std::to_string( info.m_nFirstFrame ) },
			{ "faces", std::to_string( info.m_nFaces ) },
			{ "bump_scale", std::to_string( info.m_flBumpScale ) },
			{ "reflectivity", std::to_string( info.m_vecReflectivity[ 0 ] ) + " " + std::to_string( info.m_vecReflectivity[ 1 ] ) + " " + std::to_string( info.m_vecReflectivity[ 2 ] ) },
			{ "thumbnail", info.m_nLowResFormat == VtfFormat_t::VTF_FORMAT_NONE ? "none" : std::string{ FormatName( info.m_nLowResFormat ) } + " " + std::to_string( info.m_nLowResWidth ) + "x" + std::to_string( info.m_nLowResHeight ) },
			{ "size", std::to_string( data->size() ) },
		};
		std::string szFlags;
		for ( const std::string &szFlag : FlagNames( info.m_nFlags ) )
		{
			szFlags += ( szFlags.empty() ? "" : " " ) + szFlag;
		}
		rows.push_back( { "flags", szFlags } );
		std::string szSpray = "yes";
		for ( const std::string &szProblem : SprayProblems( *vtf, data->size() ) )
		{
			szSpray = ( szSpray == "yes" ? "no: " : szSpray + "; " ) + szProblem;
		}
		rows.push_back( { "spray", szSpray } );
		if ( bKv )
		{
			std::printf( "\"vtf\"\n{\n" );
			for ( const auto &[ szKey, szValue ] : rows )
			{
				std::printf( "\t\"%s\"\t\"%s\"\n", szKey.c_str(), szValue.c_str() );
			}
		}
		else
		{
			for ( const auto &[ szKey, szValue ] : rows )
			{
				std::printf( "%-14s %s\n", szKey.c_str(), szValue.c_str() );
			}
		}
		for ( const VtfResource_t &resource : vtf->m_Resources )
		{
			// The three-letter tags as they are; the two image entries and the sheet by name.
			std::string szTag = resource.m_Tag == LOW_RES_RESOURCE ? "thumbnail" : resource.m_Tag == IMAGE_RESOURCE ? "image"
				: resource.m_Tag == SHEET_RESOURCE                                                                  ? "sheet"
																													: std::string( reinterpret_cast<const char *>( resource.m_Tag.data() ), 3 );
			std::erase( szTag, '\0' );
			const bool bImage = resource.m_Tag == LOW_RES_RESOURCE || resource.m_Tag == IMAGE_RESOURCE;
			const std::string szValue = bImage               ? "at byte " + std::to_string( resource.m_nValue )
				: resource.m_nFlags & VtfResource_t::NO_DATA ? "value " + std::to_string( resource.m_nValue )
															 : std::to_string( vtf->Resource( resource.m_Tag ).size() ) + " bytes";
			bKv ? std::printf( "\t\"resource\"\t\"%s %s\"\n", szTag.c_str(), szValue.c_str() ) : std::printf( "resource       %s %s\n", szTag.c_str(), szValue.c_str() );
		}
		// The sheet in full.
		if ( const auto sheet = ReadSheet( vtf->Resource( SHEET_RESOURCE ) ) )
		{
			bKv ? std::printf( "\t\"sheet\"\n\t{\n\t\t\"version\"\t\"%d\"\n", sheet->m_nVersion ) : std::printf( "sheet          version %d\n", sheet->m_nVersion );
			for ( const CSheet::Sequence_t &sequence : sheet->m_Sequences )
			{
				if ( bKv )
				{
					std::printf( "\t\t\"sequence\"\n\t\t{\n\t\t\t\"number\"\t\"%d\"\n\t\t\t\"loop\"\t\"%d\"\n\t\t\t\"time\"\t\"%g\"\n", sequence.m_nNumber, sequence.m_bClamp ? 0 : 1, static_cast<double>( sequence.m_flTotalTime ) );
				}
				else
				{
					std::printf( "  sequence %d: %zu frames, %g long, %s\n", sequence.m_nNumber, sequence.m_Frames.size(), static_cast<double>( sequence.m_flTotalTime ), sequence.m_bClamp ? "holds the last" : "loops" );
				}
				for ( const CSheet::Frame_t &frame : sequence.m_Frames )
				{
					const auto &r = frame.m_Rect;
					bKv ? std::printf( "\t\t\t\"frame\"\t\"%g %g %g %g %g\"\n", static_cast<double>( frame.m_flDuration ), static_cast<double>( r[ 0 ] ), static_cast<double>( r[ 1 ] ), static_cast<double>( r[ 2 ] ), static_cast<double>( r[ 3 ] ) )
						: std::printf( "    lasts %g: %g %g to %g %g\n", static_cast<double>( frame.m_flDuration ), static_cast<double>( r[ 0 ] ), static_cast<double>( r[ 1 ] ), static_cast<double>( r[ 2 ] ), static_cast<double>( r[ 3 ] ) );
				}
				if ( bKv )
				{
					std::printf( "\t\t}\n" );
				}
			}
			if ( bKv )
			{
				std::printf( "\t}\n" );
			}
		}
		if ( bKv )
		{
			std::printf( "}\n" );
		}
	}
	return nFailed ? 1 : 0;
}

//-----------------------------------------------------------------------------
// Purpose: thumbnail <vtf> <out.png> [size]: the first image, at most size on a side, as PNG
//          (what file managers' thumbnailers run).
//-----------------------------------------------------------------------------
int Thumbnail( const Arguments_t &arguments )
{
	if ( arguments.m_Inputs.size() < 2 )
	{
		std::fprintf( stderr, "thumbnail <vtf> <out.png> [size]\n" );
		return 1;
	}
	const int nSize = arguments.m_Inputs.size() > 2 ? std::max( 16, std::atoi( arguments.m_Inputs[ 2 ].c_str() ) ) : 256;
	const auto data = ReadFile( arguments.m_Inputs[ 0 ] );
	const auto vtf = data ? ReadVtf( *data ) : std::nullopt;
	if ( !vtf )
	{
		return 1;
	}
	// The smallest mipmap still at least the size asked for.
	const VtfInfo_t &info = vtf->m_Info;
	int nMip = 0;
	while ( nMip + 1 < info.m_nMipCount && std::max( info.m_nWidth, info.m_nHeight ) >> ( nMip + 1 ) >= nSize )
	{
		nMip++;
	}
	auto image = vtf->Decode( nMip );
	if ( !image )
	{
		return 1;
	}
	const float flScale = std::min( 1.0f, static_cast<float>( nSize ) / static_cast<float>( std::max( image->m_nWidth, image->m_nHeight ) ) );
	const CImage fitted = Resize( *image, std::max( 1, static_cast<int>( std::lround( image->m_nWidth * flScale ) ) ), std::max( 1, static_cast<int>( std::lround( image->m_nHeight * flScale ) ) ) );
	return WriteFile( arguments.m_Inputs[ 1 ], SaveImage( fitted, ImageFileType_t::IMAGE_FILE_TYPE_PNG ) ) ? 0 : 1;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int main( int argc, char **argv )
{
	if ( argc > 1 && std::string_view( argv[ 1 ] ) == "model" )
		return ModelCommand( argc - 2, argv + 2 );
	if ( argc > 1 && std::string_view( argv[ 1 ] ) == "map" )
		return MapCommand( argc - 2, argv + 2 );
	const Arguments_t arguments = Parse( argc, argv );
	if ( arguments.m_szMode.empty() || arguments.Has( "--help" ) || arguments.Has( "-h" ) )
	{
		std::printf( "%s", USAGE );
		return arguments.m_szMode.empty() && argc > 1 ? 1 : 0;
	}
	if ( arguments.m_szMode == "create" || arguments.m_szMode == "convert" )
	{
		return Create( arguments );
	}
	if ( arguments.m_szMode == "edit" )
	{
		return Edit( arguments );
	}
	if ( arguments.m_szMode == "extract" )
	{
		return Extract( arguments );
	}
	if ( arguments.m_szMode == "info" )
	{
		return Info( arguments );
	}
	if ( arguments.m_szMode == "thumbnail" )
	{
		return Thumbnail( arguments );
	}
	std::fprintf( stderr, "unknown mode %s\n\n%s", arguments.m_szMode.c_str(), USAGE );
	return 1;
}
