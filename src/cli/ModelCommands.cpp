//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Model inspection, exchange/source export and validated batch edits from the CLI.
//===========================================================================//
#include "model/Export.h"
#include "model/Phy.h"
#include "model/Transform.h"
#include "model/Vmf.h"
#include "vfs/Vfs.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
namespace
{
namespace fs = std::filesystem;
using namespace xv;
//-----------------------------------------------------------------------------
// Purpose: Reads compiled input as bytes without depending on Qt.
//-----------------------------------------------------------------------------
std::optional<model::Bytes_t> Read( const fs::path &path )
{
	std::ifstream in( path, std::ios::binary );
	if ( !in )
		return std::nullopt;
	std::vector<char> bytes( ( std::istreambuf_iterator<char>( in ) ), {} );
	return model::Bytes_t( reinterpret_cast<const std::byte *>( bytes.data() ), reinterpret_cast<const std::byte *>( bytes.data() + bytes.size() ) );
}
//-----------------------------------------------------------------------------
// Purpose: Requires a complete finite numeric value rather than accepting a malformed prefix.
//-----------------------------------------------------------------------------
float Number( const std::string &text )
{
	float v = 0;
	const auto [ p, ec ] = std::from_chars( text.data(), text.data() + text.size(), v );
	if ( ec != std::errc{} || p != text.data() + text.size() || !std::isfinite( v ) )
		throw std::runtime_error( "Invalid number: " + text );
	return v;
}
//-----------------------------------------------------------------------------
// Purpose: Stages every file before replacing originals, restoring backups if a rename fails.
//-----------------------------------------------------------------------------
void Write( const std::map<fs::path, model::Bytes_t> &files )
{
	struct File_t
	{
		fs::path m_Target, m_Temp, m_Backup;
		bool m_bBacked = false, m_bInstalled = false;
	};
	std::vector<File_t> staged;
	const auto id = std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() );
	try
	{
		for ( const auto &[ target, data ] : files )
		{
			if ( !target.parent_path().empty() )
				fs::create_directories( target.parent_path() );
			File_t file{ target, fs::path( target.native() + vfs::FromUtf8( ".vomit-" + id + ".tmp" ).native() ), fs::path( target.native() + vfs::FromUtf8( ".vomit-" + id + ".bak" ).native() ) };
			if ( fs::exists( file.m_Temp ) || fs::exists( file.m_Backup ) )
				throw std::runtime_error( "Temporary output path already exists" );
			staged.push_back( file );
			std::ofstream out( file.m_Temp, std::ios::binary );
			out.write( reinterpret_cast<const char *>( data.data() ), data.size() );
			out.close();
			if ( !out )
				throw std::runtime_error( "Cannot write " + vfs::ToUtf8( target ) );
		}
		for ( auto &file : staged )
			if ( fs::exists( file.m_Target ) )
			{
				fs::rename( file.m_Target, file.m_Backup );
				file.m_bBacked = true;
			}
		for ( auto &file : staged )
		{
			fs::rename( file.m_Temp, file.m_Target );
			file.m_bInstalled = true;
		}
	}
	catch ( ... )
	{
		for ( auto it = staged.rbegin(); it != staged.rend(); ++it )
		{
			std::error_code ec;
			if ( it->m_bInstalled )
				fs::remove( it->m_Target, ec );
			if ( it->m_bBacked )
				fs::rename( it->m_Backup, it->m_Target, ec );
			fs::remove( it->m_Temp, ec );
		}
		throw;
	}
	for ( const auto &file : staged )
		if ( file.m_bBacked )
		{
			std::error_code ec;
			fs::remove( file.m_Backup, ec );
		}
}
//-----------------------------------------------------------------------------
// Purpose: Validates bounded integer switches before converting them to engine indices.
//-----------------------------------------------------------------------------
int Index( const std::string &text, int nMaximum = 1000000 )
{
	const float f = Number( text );
	if ( f < 0 || f > nMaximum || std::floor( f ) != f )
		throw std::runtime_error( "Invalid integer index: " + text );
	return static_cast<int>( f );
}
struct Input_t
{
	std::string m_szPath;
	model::Reader_t m_pfnRead;
	fs::path m_DiskRoot;
	std::shared_ptr<vfs::CFileSystem> m_pFs;
};
//-----------------------------------------------------------------------------
// Purpose: Resolves disk companions first, optionally falling back to a mounted game.
//-----------------------------------------------------------------------------
Input_t Input( const std::string &name, const fs::path &game )
{
	Input_t input;
	if ( !game.empty() )
	{
		std::string error;
		const auto info = vfs::CGameInfo::Load( fs::is_directory( game ) ? game / "gameinfo.txt" : game, &error );
		if ( !info )
			throw std::runtime_error( error );
		input.m_pFs = std::make_shared<vfs::CFileSystem>();
		input.m_pFs->Mount( *info );
		input.m_DiskRoot = info->m_GameDir;
	}
	fs::path file = vfs::FromUtf8( name ), root;
	const bool bDisk = fs::is_regular_file( file );
	if ( bDisk )
	{
		file = fs::absolute( file );
		for ( fs::path dir = file.parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path() )
		{
			auto folder = vfs::ToUtf8( dir.filename() );
			std::ranges::transform( folder, folder.begin(), []( unsigned char c ) { return std::tolower( c ); } );
			if ( folder == "models" )
			{
				root = dir.parent_path();
				break;
			}
		}
		input.m_szPath = root.empty() ? vfs::ToUtf8( file.filename() ) : vfs::ToUtf8( file.lexically_relative( root ) );
		input.m_DiskRoot = root.empty() ? file.parent_path() : root;
	}
	else
		input.m_szPath = vfs::NormalizePath( name );
	input.m_pfnRead = [ file, root, bDisk, pFs = input.m_pFs ]( std::string_view path ) -> std::optional<model::Bytes_t> {if(bDisk){const auto local=(root.empty()?file.parent_path():root)/vfs::FromUtf8(path);if(auto bytes=Read(vfs::FindCaseInsensitive(local)))return bytes;if(root.empty()&&!path.starts_with("materials/"))if(auto bytes=Read(vfs::FindCaseInsensitive(file.parent_path()/vfs::FromUtf8(path).filename())))return bytes;}return pFs?pFs->Read(path):std::nullopt; };
	return input;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Implements model subcommands with existing checked readers and edit primitives.
//-----------------------------------------------------------------------------
int ModelCommand( int argc, char **argv )
{
	const char *usage = "model info <mdl...> [--game gameinfo.txt]\nmodel export <mdl> -o folder [--format obj|smd|gltf|qc|pack] [--game gameinfo.txt]\n  [--skin N --lod N --sequence N --first N --last N --fps N --base-blend]\nmodel vmf <mdl> <out.vmf> [--game gameinfo.txt]\n  [--hulls auto|physics|decompose|cutout --texture nodraw|dev|model]\n  [--grid N --max-hulls N --max-sides N --max-vertices N --concavity N --voxel-resolution N]\n  [--offset X Y Z --world --no-reference --no-hammer-vertices --reuse-materials]\n  [--skin N --lod N --bodygroup INDEX MODEL (repeatable)]\nmodel set surfaceprop|mass|flags|contents <value> <mdl...> [-o folder] [--game gameinfo.txt]\nmodel scale <factor> <mdl...> [-o folder] [--game gameinfo.txt]\n";
	if ( !argc || std::string_view( argv[ 0 ] ) == "--help" || std::string_view( argv[ 0 ] ) == "-h" )
	{
		std::printf( "%s", usage );
		return 0;
	}
	try
	{
		const std::string command = argv[ 0 ];
		std::vector<std::string> args;
		std::string format = "obj";
		fs::path output, game;
		model::Selection_t selection;
		model::Clip_t clip;
		model::VmfOptions_t vmfOptions;
		for ( int i = 1; i < argc; i++ )
		{
			const std::string arg = argv[ i ];
			const auto value = [ & ]() {if(i+1>=argc)throw std::runtime_error("Missing value for "+arg);return std::string(argv[++i]); };
			if ( command == "vmf" && arg == "--hulls" )
			{
				const auto szSource = value();
				if ( szSource == "auto" )
					vmfOptions.m_nHullSource = model::VmfOptions_t::HullSource_t::HULL_SOURCE_AUTO;
				else if ( szSource == "physics" )
					vmfOptions.m_nHullSource = model::VmfOptions_t::HullSource_t::HULL_SOURCE_PHYSICS;
				else if ( szSource == "decompose" )
					vmfOptions.m_nHullSource = model::VmfOptions_t::HullSource_t::HULL_SOURCE_DECOMPOSE;
				else if ( szSource == "cutout" )
					vmfOptions.m_nHullSource = model::VmfOptions_t::HullSource_t::HULL_SOURCE_CUTOUT;
				else
					throw std::runtime_error( "Unknown hull source: " + szSource );
			}
			else if ( command == "vmf" && arg == "--texture" )
			{
				const auto szTexture = value();
				if ( szTexture == "nodraw" )
					vmfOptions.m_nTexturing = model::VmfOptions_t::Texturing_t::TEXTURING_NODRAW;
				else if ( szTexture == "dev" )
					vmfOptions.m_nTexturing = model::VmfOptions_t::Texturing_t::TEXTURING_DEV;
				else if ( szTexture == "model" )
					vmfOptions.m_nTexturing = model::VmfOptions_t::Texturing_t::TEXTURING_MODEL;
				else
					throw std::runtime_error( "Unknown VMF texture mode: " + szTexture );
			}
			else if ( command == "vmf" && arg == "--grid" )
				vmfOptions.m_flGrid = Number( value() );
			else if ( command == "vmf" && arg == "--concavity" )
				vmfOptions.m_flConcavity = Number( value() );
			else if ( command == "vmf" && arg == "--max-hulls" )
				vmfOptions.m_nMaxHulls = Index( value() );
			else if ( command == "vmf" && arg == "--max-sides" )
				vmfOptions.m_nMaxSides = Index( value() );
			else if ( command == "vmf" && arg == "--max-vertices" )
				vmfOptions.m_nMaxVerticesPerHull = Index( value() );
			else if ( command == "vmf" && arg == "--voxel-resolution" )
				vmfOptions.m_nVoxelResolution = Index( value(), 8000000 );
			else if ( command == "vmf" && arg == "--offset" )
			{
				vmfOptions.m_vecOffset.m_flX = Number( value() );
				vmfOptions.m_vecOffset.m_flY = Number( value() );
				vmfOptions.m_vecOffset.m_flZ = Number( value() );
			}
			else if ( command == "vmf" && arg == "--world" )
				vmfOptions.m_bDetail = false;
			else if ( command == "vmf" && arg == "--no-reference" )
				vmfOptions.m_bReferenceProp = false;
			else if ( command == "vmf" && arg == "--no-hammer-vertices" )
				vmfOptions.m_bHammerPlusPlusVertices = false;
			else if ( command == "vmf" && arg == "--reuse-materials" )
				vmfOptions.m_bNodrawIncompatibleMaterials = false;
			else if ( arg == "--bodygroup" )
			{
				const int nGroup = Index( value() ), nModel = Index( value() );
				if ( nGroup > 1023 )
					throw std::runtime_error( "Bodygroup index exceeds limit" );
				selection.m_Bodygroups.resize( std::max( selection.m_Bodygroups.size(), static_cast<std::size_t>( nGroup + 1 ) ) );
				selection.m_Bodygroups[ nGroup ] = nModel;
			}
			else if ( arg == "-o" || arg == "--output" )
				output = vfs::FromUtf8( value() );
			else if ( arg == "--game" )
				game = vfs::FromUtf8( value() );
			else if ( arg == "--format" )
				format = value();
			else if ( arg == "--skin" )
				selection.m_nSkin = Index( value() );
			else if ( arg == "--lod" )
				selection.m_nLod = Index( value() );
			else if ( arg == "--sequence" )
				clip.m_nSequence = Index( value() );
			else if ( arg == "--first" )
				clip.m_nFirst = Index( value() );
			else if ( arg == "--last" )
				clip.m_nLast = Index( value() );
			else if ( arg == "--fps" )
				clip.m_flFps = Number( value() );
			else if ( arg == "--base-blend" )
				clip.m_bAllowBaseBlend = true;
			else if ( arg == "--help" || arg == "-h" )
			{
				std::printf( "%s", usage );
				return 0;
			}
			else if ( arg.starts_with( "--" ) )
				throw std::runtime_error( "Unknown model option: " + arg );
			else
				args.push_back( arg );
		}
		std::string field, value;
		if ( command == "set" )
		{
			if ( args.size() < 3 )
				throw std::runtime_error( usage );
			field = args[ 0 ];
			value = args[ 1 ];
			args.erase( args.begin(), args.begin() + 2 );
		}
		else if ( command == "scale" )
		{
			if ( args.size() < 2 )
				throw std::runtime_error( usage );
			value = args[ 0 ];
			args.erase( args.begin() );
		}
		if ( args.empty() )
			throw std::runtime_error( usage );
		if ( command == "vmf" )
		{
			if ( args.size() != 2 || !output.empty() )
				throw std::runtime_error( "model vmf needs one model and an output VMF path" );
			const auto input = Input( args[ 0 ], game );
			vmfOptions.m_Selection = selection;
			const auto result = model::ExportVmf( input.m_szPath, input.m_pfnRead, vmfOptions );
			Write( { { vfs::FromUtf8( args[ 1 ] ), result.m_Files.at( "model.vmf" ) } } );
			for ( const auto &szNote : result.m_Notes )
				std::fprintf( stderr, "%s\n", szNote.c_str() );
			return 0;
		}
		if ( command == "export" && ( args.size() != 1 || output.empty() ) )
			throw std::runtime_error( "model export needs one model and -o folder" );
		int nFailed = 0;
		for ( const auto &name : args )
			try
			{
				const auto input = Input( name, game );
				std::string error;
				auto studio = model::ReadStudio( input.m_szPath, input.m_pfnRead, &error );
				if ( !studio )
					throw std::runtime_error( error );
				auto mesh = model::LoadModel( input.m_szPath, input.m_pfnRead, &error, selection );
				if ( command == "info" )
				{
					std::printf( "%s\n  version %d  checksum %08x  %zu bones  %zu sequences\n  surface %s  mass %g  %zu bodygroups  %zu skins\n", name.c_str(), studio->m_nVersion, static_cast<unsigned>( studio->m_nChecksum ), studio->m_Bones.size(), studio->m_Sequences.size(), studio->m_szSurface.c_str(), static_cast<double>( studio->m_flMass ), studio->m_Bodygroups.size(), studio->m_Skins.size() );
					if ( mesh )
						std::printf( "  %zu vertices  %d triangles\n", mesh->m_Vertices.size(), mesh->GetTriangleCount() );
					else
						std::printf( "  %s\n", error.c_str() );
					continue;
				}
				if ( command == "export" )
				{
					model::Export_t result;
					if ( format == "qc" )
						result = model::ExportSourceProject( input.m_szPath, input.m_pfnRead );
					else if ( format == "pack" )
						result = model::CollectModelFiles( input.m_szPath, input.m_pfnRead );
					else
					{
						if ( !mesh )
							throw std::runtime_error( error );
						if ( format == "obj" )
							result = model::ExportObj( *mesh, input.m_pfnRead );
						else if ( format == "gltf" )
							result = model::ExportGltf( *mesh, input.m_pfnRead, clip.m_nSequence >= 0 ? &*studio : nullptr, clip );
						else if ( format == "smd" )
						{
							const auto text = clip.m_nSequence >= 0 ? model::ExportAnimationSmd( *studio, *mesh, clip ) : model::ExportSmd( *mesh );
							result.m_Files[ clip.m_nSequence >= 0 ? "animation.smd" : "model.smd" ] = { reinterpret_cast<const std::byte *>( text.data() ), reinterpret_cast<const std::byte *>( text.data() + text.size() ) };
						}
						else
							throw std::runtime_error( "Unknown export format" );
					}
					std::map<fs::path, model::Bytes_t> files;
					for ( auto &[ path, bytes ] : result.m_Files )
					{
						if ( !model::IsExportPath( path ) )
							throw std::runtime_error( "Unsafe export path" );
						files.emplace( output / vfs::FromUtf8( path ), std::move( bytes ) );
					}
					Write( files );
					for ( const auto &note : result.m_Notes )
						std::fprintf( stderr, "%s\n", note.c_str() );
					continue;
				}
				model::Files_t files;
				std::map<std::string, std::string> paths;
				const auto base = input.m_szPath.substr( 0, input.m_szPath.size() - 4 );
				for ( const auto *kind : { ".mdl", ".vvd", ".dx90.vtx", ".dx80.vtx", ".dx7_2bone.vtx", ".dx7_3bone.vtx", ".sw.vtx", ".vtx", ".phy" } )
					if ( auto data = input.m_pfnRead( base + kind ) )
					{
						files[ kind ] = std::move( *data );
						paths[ kind ] = base + kind;
					}
				if ( !studio->m_szAnimationFile.empty() && studio->m_pAnimationBytes )
				{
					files[ ".ani" ] = *studio->m_pAnimationBytes;
					paths[ ".ani" ] = studio->m_szAnimationFile;
				}
				const auto before = files;
				if ( command == "scale" )
					model::ScaleModel( files, Number( value ) );
				else if ( command == "set" )
				{
					if ( field == "surfaceprop" )
						model::SetSurfaceProp( files.at( ".mdl" ), value );
					else if ( field == "mass" )
						model::SetMass( files.at( ".mdl" ), Number( value ) );
					else if ( field == "flags" || field == "contents" )
					{
						std::size_t n = 0;
						const auto flag = std::stoll( value, &n, 0 );
						if ( n != value.size() || flag < INT32_MIN || flag > UINT32_MAX )
							throw std::runtime_error( "Invalid flag value" );
						if ( field == "flags" )
							model::SetFlags( files.at( ".mdl" ), static_cast<std::int32_t>( flag ) );
						else
							model::SetContents( files.at( ".mdl" ), static_cast<std::int32_t>( flag ) );
					}
					else
						throw std::runtime_error( "Unknown model field" );
				}
				else
					throw std::runtime_error( "Unknown model command" );
				const model::Reader_t check = [ & ]( std::string_view path ) -> std::optional<model::Bytes_t> {for(const auto &[kind,p]:paths)if(p==path)return files.at(kind);return input.m_pfnRead(path); };
				if ( !model::ReadStudio( input.m_szPath, check, &error ) || ( mesh && !model::LoadModel( input.m_szPath, check, &error ) ) )
					throw std::runtime_error( "Refusing invalid output: " + error );
				if ( files.contains( ".phy" ) && !model::ReadPhysics( files.at( ".phy" ), &error ) )
					throw std::runtime_error( error );
				std::map<fs::path, model::Bytes_t> writes;
				for ( const auto &[ kind, bytes ] : files )
					if ( !output.empty() || before.at( kind ) != bytes )
					{
						const auto &path = paths.at( kind );
						if ( !model::IsExportPath( path ) )
							throw std::runtime_error( "Unsafe compiled output path" );
						writes.emplace( ( output.empty() ? input.m_DiskRoot : output ) / vfs::FromUtf8( path ), bytes );
					}
				Write( writes );
			}
			catch ( const std::exception &e )
			{
				std::fprintf( stderr, "%s: %s\n", name.c_str(), e.what() );
				nFailed++;
			}
		return nFailed ? 1 : 0;
	}
	catch ( const std::exception &e )
	{
		std::fprintf( stderr, "%s\n", e.what() );
		return 1;
	}
}
