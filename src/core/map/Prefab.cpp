//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Prefab.h. The local VDC Prefab article specifies VMFs, hierarchy by subfolder,
//          origin-relative insertion and &i tokens. OL containers belong to pre-Source Hammer.
//
//===========================================================================//
#include "map/Prefab.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "map/GameConfig.h"
#include "map/Vmf.h"

namespace xv::map
{
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Source names and Windows prefab files are matched without case sensitivity.
//-----------------------------------------------------------------------------
std::string Lower( std::string sz )
{
	std::ranges::transform( sz, sz.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: All explicit prefab name tokens in one placement use the same number.
//-----------------------------------------------------------------------------
std::string ExpandToken( std::string_view szText, std::uint64_t nCopy )
{
	std::string sz{ szText };
	const auto szNumber = std::to_string( nCopy );
	for ( auto n = sz.find( "&i" ); n != std::string::npos; n = sz.find( "&i", n + szNumber.size() ) )
		sz.replace( n, 2, szNumber );
	return sz;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Search the game and both supported Hammer binary layouts without reading libraries
//          from unrelated game installations.
//-----------------------------------------------------------------------------
std::vector<std::filesystem::path> PrefabFolders( const std::filesystem::path &gameDir )
{
	return { FindFileIgnoringCase( gameDir / "prefabs" ), FindFileIgnoringCase( gameDir.parent_path() / "bin/prefabs" ), FindFileIgnoringCase( gameDir.parent_path() / "bin/x64/prefabs" ) };
}

//-----------------------------------------------------------------------------
// Purpose: Enumerate VMFs only, preserving root priority while sorting libraries and entries.
//-----------------------------------------------------------------------------
std::vector<PrefabLibrary_t> FindPrefabs( std::span<const std::filesystem::path> folders )
{
	std::map<std::string, PrefabLibrary_t> libraries;
	for ( const auto &folder : folders )
	{
		std::error_code error;
		std::filesystem::recursive_directory_iterator it( folder, std::filesystem::directory_options::skip_permission_denied, error ), end;
		for ( ; !error && it != end; it.increment( error ) )
		{
			if ( !it->is_regular_file( error ) || Lower( it->path().extension().string() ) != ".vmf" )
				continue;
			const auto relative = it->path().parent_path().lexically_relative( folder );
			const std::string szLibrary = relative.empty() || relative == "." ? "Prefabs" : relative.generic_string();
			auto &library = libraries[ Lower( szLibrary ) ];
			if ( library.m_szName.empty() )
				library.m_szName = szLibrary;
			const auto szName = it->path().stem().string();
			if ( std::ranges::none_of( library.m_Prefabs, [ & ]( const auto &prefab ) { return Lower( prefab.m_szName ) == Lower( szName ); } ) )
				library.m_Prefabs.push_back( { szName, it->path() } );
		}
	}
	std::vector<PrefabLibrary_t> out;
	for ( auto &[ szName, library ] : libraries )
	{
		std::ranges::sort( library.m_Prefabs, []( const auto &a, const auto &b ) { return Lower( a.m_szName ) < Lower( b.m_szName ); } );
		out.push_back( std::move( library ) );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Export the selected objects, with the VMF format's explicit prefab marker.
//-----------------------------------------------------------------------------
std::string SelectionToPrefab( const CScene &scene, const std::vector<std::uint32_t> &objects )
{
	const auto szObjects = ObjectsToText( scene, objects );
	if ( szObjects.empty() )
		return {};
	std::string szHeader = "versioninfo\n{\n\t\"editorversion\" \"400\"\n\t\"formatversion\" \"100\"\n\t\"prefab\" \"1\"\n}\n";
	if ( scene.m_Map.GetNewline() == "\r\n" )
		for ( std::size_t n = 0; n < szHeader.size(); ++n )
			if ( szHeader[ n ] == '\n' )
				szHeader.insert( n++, 1, '\r' );
	return szHeader + szObjects;
}

//-----------------------------------------------------------------------------
// Purpose: Expand deliberate name tokens, keep ordinary names and rebase nested instance paths
//          before handing allocation, texture locks and attachment remapping to the clipboard.
//-----------------------------------------------------------------------------
MapEdit_t PlacePrefab( const CScene &scene, const std::filesystem::path &file, Point_t at,
	std::vector<std::uint32_t> *pAdded, std::string *pError )
{
	if ( pError )
		pError->clear();
	if ( pAdded )
		pAdded->clear();
	auto map = ReadVmfFile( file, {}, pError );
	if ( !map )
		return {};
	std::set<std::string> taken;
	for ( const auto &entity : scene.m_Map.m_Entities )
		if ( !entity.m_bDeleted )
			taken.insert( Lower( std::string{ entity.GetValue( "targetname" ) } ) );
	std::uint64_t nCopy = 1;
	while ( std::ranges::any_of( map->m_Entities, [ & ]( const auto &entity ) {
		const auto szName = entity.GetValue( "targetname" );
		return szName.find( "&i" ) != std::string_view::npos && taken.contains( Lower( ExpandToken( szName, nCopy ) ) );
	} ) )
		++nCopy;
	std::map<std::string, std::string, std::less<>> names;
	for ( const auto &entity : map->m_Entities )
	{
		const auto szName = entity.GetValue( "targetname" );
		if ( szName.find( "&i" ) != std::string_view::npos )
			names.emplace( szName, ExpandToken( szName, nCopy ) );
	}
	InstanceOptions_t options;
	options.m_MapFile = file;
	for ( auto &entity : map->m_Entities )
	{
		for ( auto &key : entity.m_Keys )
		{
			if ( const auto found = names.find( key.m_szValue ); found != names.end() )
			{
				key.m_szValue = map->Store( found->second );
				key.m_bChanged = true;
			}
		}
		for ( auto &connection : entity.m_Connections )
		{
			if ( const auto found = names.find( connection.m_szTarget ); found != names.end() )
			{
				connection.m_szTarget = map->Store( found->second );
				entity.m_bConnectionsChanged = true;
			}
		}
		if ( entity.GetClassname() == "func_instance" )
			if ( const auto found = FindInstanceFile( entity.GetValue( "file" ), options ) )
				SetKeyValue( *map, entity, "file", found->generic_string() );
	}
	const auto prefab = BuildScene( std::move( *map ), scene.m_pFgd );
	if ( !prefab )
		return {};
	std::vector<std::uint32_t> objects;
	for ( std::uint32_t n = 0; n < prefab->GetObjectCount(); ++n )
		if ( !prefab->IsDeleted( n ) )
			objects.push_back( n );
	return ObjectsFromText( scene, ObjectsToText( *prefab, objects ), Transform_t::Translation( at ), pAdded );
}
} // namespace xv::map
