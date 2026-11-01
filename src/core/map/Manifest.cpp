//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Manifest.h. The format is the one CS:GO's and Left 4 Dead 2's Hammer write (no
//          VDC article on it here yet): Info > NextMapID, then Maps with a VMF block a submap
//          (Name, File, InternalID, TopLevel).
//
//===========================================================================//
#include "map/Manifest.h"

#include <algorithm>

#include "kv1/Kv1.h"
#include "map/GameConfig.h"
#include "map/Vmf.h"
#include "map/Write.h"
#include "util/Numbers.h"

namespace xv::map
{
namespace
{
//-----------------------------------------------------------------------------
// Purpose: A value for a VMF key: VMFs have no escapes, so a quote can't be kept.
//-----------------------------------------------------------------------------
std::string Quoted( std::string_view szValue )
{
	std::string sz{ szValue };
	std::ranges::replace( sz, '"', '\'' );
	return '"' + sz + '"';
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<ManifestMap_t> ReadManifest( std::string_view szText, const std::filesystem::path &manifest, std::vector<std::string> *pErrors )
{
	std::vector<ManifestMap_t> maps;
	const kv1::CParseResult parsed = kv1::Parse( szText );
	if ( pErrors )
		for ( const kv1::Diagnostic_t &diagnostic : parsed.m_Diagnostics )
			if ( diagnostic.m_nSeverity == kv1::Diagnostic_t::Severity_t::SEVERITY_ERROR )
				pErrors->push_back( "line " + std::to_string( diagnostic.m_nLine ) + ": " + diagnostic.m_szMessage );
	const kv1::CNode *pMaps = parsed.m_Document.Find( "Maps" );
	if ( !pMaps )
	{
		if ( pErrors )
			pErrors->push_back( "no Maps block: not a map manifest" );
		return maps;
	}
	const std::filesystem::path folder = manifest.parent_path();
	std::error_code error;
	for ( const kv1::CNode &node : pMaps->GetChildren() )
	{
		if ( !node.IsBlock() || !kv1::EqualsIgnoreCase( node.GetKey(), "VMF" ) )
			continue;
		ManifestMap_t map;
		auto value = [ & ]( std::string_view szKey ) {
			const kv1::CNode *pNode = node.Find( szKey );
			return pNode ? pNode->GetValue() : std::string{};
		};
		map.m_szName = value( "Name" );
		map.m_szFile = value( "File" );
		map.m_nId = ParseNumber<int>( value( "InternalID" ) ).value_or( 0 );
		map.m_bTopLevel = ParseNumber<int>( value( "TopLevel" ) ).value_or( 0 ) != 0;
		std::string szFile = map.m_szFile;
		std::ranges::replace( szFile, '\\', '/' );
		for ( const std::filesystem::path &candidate : { folder / szFile, folder / manifest.stem() / szFile } )
		{
			const std::filesystem::path found = FindFileIgnoringCase( candidate );
			if ( !szFile.empty() && std::filesystem::is_regular_file( found, error ) )
			{
				map.m_Path = found;
				break;
			}
		}
		if ( map.m_Path.empty() && pErrors )
			pErrors->push_back( "submap \"" + map.m_szName + "\": " + ( map.m_szFile.empty() ? std::string{ "no File" } : map.m_szFile + " isn't there" ) );
		maps.push_back( std::move( map ) );
	}
	return maps;
}

//-----------------------------------------------------------------------------
// Purpose: Instances named by their submaps but with fixup style 2 (none), so the entities
//          keep the names the submaps give them, as in the manifest.
//-----------------------------------------------------------------------------
std::string ManifestToVmf( const std::vector<ManifestMap_t> &maps, const std::filesystem::path &manifest )
{
	std::string szWorld;
	const auto top = std::ranges::find_if( maps, []( const ManifestMap_t &map ) { return map.m_bTopLevel && !map.m_Path.empty(); } );
	const auto first = top != maps.end() ? top : std::ranges::find_if( maps, []( const ManifestMap_t &map ) { return !map.m_Path.empty(); } );
	if ( first != maps.end() )
	{
		if ( const auto world = ReadVmfFile( first->m_Path ) )
		{
			for ( const EntityKey_t &key : world->m_World.m_Keys )
				if ( !key.m_bDeleted && !kv1::EqualsIgnoreCase( key.m_szKey, "id" ) && !kv1::EqualsIgnoreCase( key.m_szKey, "classname" ) )
					szWorld += "\t" + Quoted( key.m_szKey ) + " " + Quoted( key.m_szValue ) + "\r\n";
		}
	}
	std::string szText = NewVmf();
	const std::size_t nWorld = szText.find( "world\r\n{" );
	const std::size_t nWorldEnd = szText.find( "}\r\n", nWorld );
	szText.replace( nWorld, nWorldEnd + 3 - nWorld, "world\r\n{\r\n\t\"id\" \"1\"\r\n\t\"classname\" \"worldspawn\"\r\n" + ( szWorld.empty() ? std::string{ "\t\"skyname\" \"sky_day01_01\"\r\n" } : szWorld ) + "}\r\n" );
	std::string szEntities;
	int nId = 2;
	for ( const ManifestMap_t &map : maps )
	{
		if ( map.m_Path.empty() )
			continue;
		const std::string szFile = map.m_Path.lexically_relative( manifest.parent_path() ).generic_string();
		szEntities += "entity\r\n{\r\n\t\"id\" \"" + std::to_string( nId++ ) + "\"\r\n\t\"classname\" \"func_instance\"\r\n\t\"targetname\" " + Quoted( map.m_szName ) +
			"\r\n\t\"file\" " + Quoted( szFile ) + "\r\n\t\"fixup_style\" \"2\"\r\n\t\"angles\" \"0 0 0\"\r\n\t\"origin\" \"0 0 0\"\r\n}\r\n";
	}
	szText.insert( szText.find( "cameras\r\n" ), szEntities );
	return szText;
}

} // namespace xv::map
