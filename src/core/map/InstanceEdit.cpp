//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See InstanceEdit.h.
//
//===========================================================================//
#include "map/InstanceEdit.h"

#include <algorithm>
#include <map>
#include <set>

#include "kv1/Kv1.h"
#include "map/Visgroups.h"
#include "util/Numbers.h"

namespace xv::map
{
namespace
{
struct Fixup_t
{
	std::string m_szName;
	int m_nStyle = 0;
};
using Names_t = std::map<std::string, std::string, std::less<>>;
using Aliases_t = std::map<std::pair<std::string, std::string>, std::string>;

//-----------------------------------------------------------------------------
// Purpose: Explicit propagate_fixup takes precedence; older maps use targetname for both.
//-----------------------------------------------------------------------------
Fixup_t FixupOf( const Entity_t &entity )
{
	std::string szName{ entity.GetValue( "propagate_fixup" ) };
	if ( szName.empty() )
		szName = entity.GetValue( "targetname" );
	if ( szName.empty() )
		szName = "InstanceAuto" + std::to_string( entity.m_nId );
	return { szName, ParseNumber<int>( entity.GetValue( "fixup_style" ) ).value_or( 0 ) };
}

//-----------------------------------------------------------------------------
// Purpose: Inner fixups precede their parents. Global @names and engine !targets stay global.
//-----------------------------------------------------------------------------
std::string FixedName( std::string_view szName, const std::vector<Fixup_t> &fixups )
{
	std::string sz{ szName };
	if ( sz.empty() || sz.front() == '@' || sz.front() == '!' )
		return sz;
	for ( auto it = fixups.rbegin(); it != fixups.rend(); ++it )
	{
		if ( it->m_nStyle == 0 )
			sz = it->m_szName + "-" + sz;
		else if ( it->m_nStyle == 1 )
			sz += "-" + it->m_szName;
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: References to locally declared names follow their declarations, including wildcards.
//-----------------------------------------------------------------------------
std::string Reference( std::string_view szName, const Names_t &names, const std::vector<Fixup_t> &fixups )
{
	const auto nComma = szName.find( ',' );
	const auto szTarget = szName.substr( 0, nComma );
	if ( const auto found = names.find( szTarget ); found != names.end() )
		return found->second + ( nComma == std::string_view::npos ? "" : std::string{ szName.substr( nComma ) } );
	if ( const auto nStar = szTarget.find( '*' ); nStar != std::string_view::npos &&
		std::ranges::any_of( names, [ & ]( const auto &entry ) { return entry.first.starts_with( szTarget.substr( 0, nStar ) ); } ) )
		return FixedName( szName, fixups );
	return std::string{ szName };
}

//-----------------------------------------------------------------------------
// Purpose: The VDC's instance:entity;Input syntax addresses the resolved inner entity directly.
//-----------------------------------------------------------------------------
void ResolveInput( const CMap &map, Connection_t &connection, const Aliases_t &aliases )
{
	constexpr std::string_view PREFIX = "instance:";
	if ( !connection.m_szInput.starts_with( PREFIX ) )
		return;
	const auto nSeparator = connection.m_szInput.find( ';', PREFIX.size() );
	if ( nSeparator == std::string_view::npos )
		return;
	const std::string szInternal{ connection.m_szInput.substr( PREFIX.size(), nSeparator - PREFIX.size() ) };
	if ( const auto found = aliases.find( { std::string{ connection.m_szTarget }, szInternal } ); found != aliases.end() )
	{
		connection.m_szTarget = map.Store( found->second );
		connection.m_szInput = map.Store( std::string{ connection.m_szInput.substr( nSeparator + 1 ) } );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Build the names used by one resolved level of an instance.
//-----------------------------------------------------------------------------
Names_t NamesOf( const CScene &scene, const std::vector<Fixup_t> &fixups )
{
	Names_t names;
	for ( const auto &entity : scene.m_Map.m_Entities )
		if ( !entity.m_bDeleted && !entity.GetValue( "targetname" ).empty() )
			names.emplace( entity.GetValue( "targetname" ), FixedName( entity.GetValue( "targetname" ), fixups ) );
	return names;
}

//-----------------------------------------------------------------------------
// Purpose: Nested aliases can be addressed before their object IDs have been allocated.
//-----------------------------------------------------------------------------
Aliases_t ChildAliases( const CScene &scene, const std::vector<Fixup_t> &fixups )
{
	Aliases_t aliases;
	for ( const auto &placed : scene.m_Instances )
	{
		const auto &entity = scene.m_Map.m_Entities[ scene.GetIndex( placed.m_nObject ) ];
		auto childFixups = fixups;
		childFixups.push_back( FixupOf( entity ) );
		for ( const auto &[ szOld, szNew ] : NamesOf( *placed.m_pScene, childFixups ) )
		{
			aliases.emplace( std::pair{ std::string{ entity.GetValue( "targetname" ) }, szOld }, szNew );
			aliases.emplace( std::pair{ FixedName( entity.GetValue( "targetname" ), fixups ), szOld }, szNew );
		}
	}
	return aliases;
}

//-----------------------------------------------------------------------------
// Purpose: Only name-bearing keys are fixed up; numbers, paths and unrelated strings stay intact.
//-----------------------------------------------------------------------------
void FixEntityNames( const CScene &scene, Entity_t &entity, const Names_t &names, const std::vector<Fixup_t> &fixups )
{
	const auto *pClass = scene.m_pFgd ? scene.m_pFgd->Find( entity.GetClassname() ) : nullptr;
	for ( auto &key : entity.m_Keys )
	{
		if ( key.m_bDeleted )
			continue;
		const auto *pKey = pClass ? pClass->FindKey( key.m_szKey ) : nullptr;
		const bool bName = pKey && ( pKey->m_szType == "target_destination" || pKey->m_szType == "target_source" || pKey->m_szType == "target_name_or_class" );
		const bool bKnown = key.m_szKey == "parentname" || key.m_szKey == "target" || key.m_szKey == "killtarget" || key.m_szKey == "filtername" || key.m_szKey == "filter01" || key.m_szKey == "filter02";
		if ( key.m_szKey == "targetname" || bName || bKnown )
		{
			key.m_szValue = scene.m_Map.Store( key.m_szKey == "targetname" ? FixedName( key.m_szValue, fixups ) : Reference( key.m_szValue, names, fixups ) );
			key.m_bChanged = true;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Flatten one resolved scene using exactly the transform drawn for it. Exporting each
//          level through the ordinary clipboard path shares ID and foreign-string adoption.
//-----------------------------------------------------------------------------
bool Flatten( const PlacedInstance_t &placed, const Entity_t &owner, const Transform_t &toWorld,
	std::vector<Fixup_t> fixups, const Aliases_t &outerAliases, std::shared_ptr<CScene> &pWorking,
	MapEdit_t &result, std::vector<std::uint32_t> &added, std::string &szError )
{
	const CScene &inner = *placed.m_pScene;
	fixups.push_back( FixupOf( owner ) );
	const auto names = NamesOf( inner, fixups );
	const auto aliases = ChildAliases( inner, fixups );
	std::set<std::string> proxies;
	for ( const auto &entity : inner.m_Map.m_Entities )
		if ( entity.GetClassname() == "func_instance_io_proxy" )
			proxies.emplace( entity.GetValue( "targetname" ) );
	MapEdit_t renamed;
	std::vector<std::uint32_t> objects;
	for ( std::uint32_t s = 0; s < inner.m_Map.m_Solids.size(); ++s )
		if ( !inner.m_Map.m_Solids[ s ].m_bDeleted && inner.m_Map.m_Solids[ s ].m_nEntity < 0 )
			objects.push_back( inner.SolidObject( s ) );
	for ( std::uint32_t e = 0; e < inner.m_Map.m_Entities.size(); ++e )
	{
		const auto &source = inner.m_Map.m_Entities[ e ];
		if ( source.m_bDeleted )
			continue;
		const auto szClass = source.GetClassname();
		if ( szClass == "func_instance" )
		{
			const auto found = std::ranges::find_if( inner.m_Instances, [ & ]( const auto &child ) { return child.m_nObject == inner.EntityObject( e ); } );
			if ( found == inner.m_Instances.end() )
			{
				szError = "Cannot collapse an unresolved nested instance: " + std::string{ source.GetValue( "file" ) };
				return false;
			}
			Entity_t child = source;
			for ( auto &connection : child.m_Connections )
			{
				connection.m_szTarget = inner.m_Map.Store( Reference( connection.m_szTarget, names, fixups ) );
				ResolveInput( inner.m_Map, connection, aliases );
			}
			if ( !Flatten( *found, child, toWorld * found->m_ToParent, fixups, outerAliases, pWorking, result, added, szError ) )
				return false;
			continue;
		}
		if ( szClass == "func_instance_origin" || szClass == "func_instance_parms" )
			continue;
		Entity_t entity = source;
		FixEntityNames( inner, entity, names, fixups );
		std::vector<Connection_t> connections;
		for ( auto connection : source.m_Connections )
		{
			if ( ( proxies.contains( std::string{ connection.m_szTarget } ) && connection.m_szInput == "ProxyRelay" ) || ( szClass == "func_instance_io_proxy" && connection.m_szOutput == "OnProxyRelay" ) )
				continue;
			connection.m_szTarget = inner.m_Map.Store( Reference( connection.m_szTarget, names, fixups ) );
			ResolveInput( inner.m_Map, connection, aliases );
			ResolveInput( inner.m_Map, connection, outerAliases );
			connections.push_back( connection );
		}
		const std::string szPrefix = "instance:" + std::string{ source.GetValue( "targetname" ) } + ";";
		for ( auto connection : owner.m_Connections )
		{
			if ( !connection.m_szOutput.starts_with( szPrefix ) )
				continue;
			const auto szOutput = connection.m_szOutput.substr( szPrefix.size() );
			float flProxyDelay = 0;
			int nProxyTimes = -1;
			for ( const auto &route : source.m_Connections )
				if ( route.m_szOutput == szOutput && proxies.contains( std::string{ route.m_szTarget } ) && route.m_szInput == "ProxyRelay" )
				{
					flProxyDelay = route.m_flDelay;
					nProxyTimes = route.m_nTimes;
					break;
				}
			connection.m_szOutput = inner.m_Map.Store( std::string{ szOutput } );
			connection.m_flDelay += flProxyDelay;
			if ( nProxyTimes >= 0 )
				connection.m_nTimes = connection.m_nTimes < 0 ? nProxyTimes : std::min( connection.m_nTimes, nProxyTimes );
			ResolveInput( inner.m_Map, connection, outerAliases );
			connections.push_back( connection );
		}
		SetConnections( inner.m_Map, entity, std::move( connections ) );
		renamed.m_Entities.emplace_back( e, std::move( entity ) );
		objects.push_back( inner.EntityObject( e ) );
	}
	const auto prepared = ApplyEdit( inner, renamed );
	std::vector<std::uint32_t> newObjects;
	auto edit = ObjectsFromText( *pWorking, ObjectsToText( *prepared, objects ), toWorld, &newObjects );
	pWorking = ApplyEdit( *pWorking, edit );
	result.m_Solids.insert( result.m_Solids.end(), edit.m_Solids.begin(), edit.m_Solids.end() );
	result.m_Entities.insert( result.m_Entities.end(), edit.m_Entities.begin(), edit.m_Entities.end() );
	added.insert( added.end(), newObjects.begin(), newObjects.end() );
	return true;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Build the whole collapse before returning any edit, so a missing child loses nothing.
//-----------------------------------------------------------------------------
MapEdit_t CollapseInstances( const CScene &scene, const std::vector<std::uint32_t> &objects,
	std::vector<std::uint32_t> *pAdded, std::string *pError )
{
	MapEdit_t result;
	if ( pError )
		pError->clear();
	if ( pAdded )
		pAdded->clear();
	std::vector<PlacedInstance_t> selected;
	for ( const auto nObject : objects )
	{
		if ( nObject >= scene.GetObjectCount() || scene.IsDeleted( nObject ) || scene.IsSolidObject( nObject ) )
			continue;
		const auto &entity = scene.m_Map.m_Entities[ scene.GetIndex( nObject ) ];
		if ( entity.GetClassname() != "func_instance" )
			continue;
		const auto found = std::ranges::find_if( scene.m_Instances, [ & ]( const auto &placed ) { return placed.m_nObject == nObject; } );
		if ( found == scene.m_Instances.end() )
		{
			if ( pError )
				*pError = "Cannot collapse an unresolved instance: " + std::string{ entity.GetValue( "file" ) };
			return {};
		}
		if ( std::ranges::none_of( selected, [ & ]( const auto &placed ) { return placed.m_nObject == nObject; } ) )
			selected.push_back( *found );
	}
	CScene root = scene;
	root.m_Instances = selected;
	const auto aliases = ChildAliases( root, {} );
	auto pWorking = std::make_shared<CScene>( scene );
	std::vector<std::uint32_t> added;
	std::string szError;
	for ( const auto &placed : selected )
	{
		const auto &entity = scene.m_Map.m_Entities[ scene.GetIndex( placed.m_nObject ) ];
		if ( !Flatten( placed, entity, placed.m_ToParent, {}, aliases, pWorking, result, added, szError ) )
		{
			if ( pError )
				*pError = szError;
			return {};
		}
	}
	if ( selected.empty() )
		return result;
	std::set<std::uint32_t> removed;
	for ( const auto &placed : selected )
	{
		const auto e = scene.GetIndex( placed.m_nObject );
		removed.insert( e );
		auto entity = scene.m_Map.m_Entities[ e ];
		entity.m_bDeleted = true;
		result.m_Entities.emplace_back( e, std::move( entity ) );
	}
	for ( std::uint32_t e = 0; e < scene.m_Map.m_Entities.size(); ++e )
	{
		if ( removed.contains( e ) || scene.m_Map.m_Entities[ e ].m_bDeleted )
			continue;
		auto entity = scene.m_Map.m_Entities[ e ];
		bool bChanged = false;
		for ( auto &connection : entity.m_Connections )
		{
			const auto before = connection;
			ResolveInput( scene.m_Map, connection, aliases );
			bChanged |= before.m_szTarget != connection.m_szTarget || before.m_szInput != connection.m_szInput;
		}
		if ( bChanged )
		{
			entity.m_bConnectionsChanged = true;
			result.m_Entities.emplace_back( e, std::move( entity ) );
		}
	}
	added.clear();
	std::uint32_t nObject = scene.GetObjectCount();
	for ( const auto &[ e, entity ] : result.m_Entities )
		if ( e >= scene.m_Map.m_Entities.size() )
			added.push_back( nObject++ );
	for ( const auto &[ s, solid ] : result.m_Solids )
		if ( s >= scene.m_Map.m_Solids.size() )
			added.push_back( nObject++ );
	if ( pAdded )
		*pAdded = std::move( added );
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: A saved child changes drawing data without rewriting or dirtying its parent map.
//-----------------------------------------------------------------------------
std::shared_ptr<CScene> RefreshInstances( const CScene &scene )
{
	auto pScene = std::make_shared<CScene>( scene );
	SceneOptions_t options;
	options.m_Instances = scene.m_InstanceOptions;
	options.m_Instances.m_pCache.reset();
	LoadInstances( *pScene, options );
	pScene->m_Changed.clear();
	for ( std::uint32_t e = 0; e < pScene->m_Map.m_Entities.size(); ++e )
		if ( pScene->m_Map.m_Entities[ e ].GetClassname() == "func_instance" && !pScene->m_Map.m_Entities[ e ].m_bDeleted )
			pScene->m_Changed.push_back( pScene->EntityObject( e ) );
	for ( const auto &chunk : pScene->GetInstanceMesh().m_Chunks )
		pScene->m_Bounds.Add( chunk.m_Bounds );
	pScene->m_AutoVisgroups = BuildAutoVisgroups( *pScene );
	++pScene->m_nRevision;
	return pScene;
}
} // namespace xv::map
