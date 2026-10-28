//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Check for Problems, the Entity Report's filter and Find Entities.
//
//===========================================================================//
#include "Check.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "kv1/Kv1.h"
#include "map/Fgd.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: How Hammer names an entity in a message: its targetname, else its class.
//-----------------------------------------------------------------------------
std::string NameOf( const Entity_t &entity )
{
	const std::string_view szName = entity.GetValue( "targetname" );
	return std::string{ szName.empty() ? entity.GetClassname() : szName };
}

//-----------------------------------------------------------------------------
// Purpose: Keys every entity may carry whatever its class says: Hammer writes them itself.
//-----------------------------------------------------------------------------
bool IsCommonKey( std::string_view szKey )
{
	for ( const std::string_view szCommon : { "classname", "origin", "angles", "spawnflags", "id", "hammerid", "targetname", "parentname" } )
	{
		if ( kv1::EqualsIgnoreCase( szKey, szCommon ) )
		{
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Lower case for matching parts of names.
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szLower{ szText };
	for ( char &ch : szLower )
	{
		ch = static_cast<char>( std::tolower( static_cast<unsigned char>( ch ) ) );
	}
	return szLower;
}

//-----------------------------------------------------------------------------
// Purpose: Whether an output's target names something: a targetname or a classname (ignoring
//          case), a name with a wildcard that some name starts with, or one of the game's
//          !special targets.
//-----------------------------------------------------------------------------
bool TargetExists( std::string_view szTarget, const std::set<std::string> &names )
{
	if ( szTarget.empty() || szTarget.front() == '!' )
	{
		return true;
	}
	const std::string szLower = Lower( szTarget );
	if ( const std::size_t nStar = szLower.find( '*' ); nStar != std::string::npos )
	{
		const std::string_view szPrefix = std::string_view{ szLower }.substr( 0, nStar );
		return std::ranges::any_of( names, [ & ]( const std::string &szName ) { return szName.starts_with( szPrefix ); } );
	}
	return names.contains( szLower );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Problem_t> CheckMap( const CScene &scene, const std::function<bool( std::uint32_t )> &pfnInclude )
{
	std::vector<Problem_t> problems;
	const CMap &map = scene.m_Map;
	auto included = [ & ]( std::uint32_t nObject ) { return !scene.IsDeleted( nObject ) && ( !pfnInclude || pfnInclude( nObject ) ); };
	// Every name an output could reach.
	std::set<std::string> names;
	for ( const Entity_t &entity : map.m_Entities )
	{
		if ( !entity.m_bDeleted )
		{
			names.insert( Lower( entity.GetClassname() ) );
			if ( const std::string_view szName = entity.GetValue( "targetname" ); !szName.empty() )
			{
				names.insert( Lower( szName ) );
			}
		}
	}
	for ( std::size_t i = 0; i < map.m_Solids.size(); i++ )
	{
		const std::uint32_t nObject = scene.SolidObject( i );
		if ( included( nObject ) && i < scene.m_Solids.size() && !scene.m_Solids[ i ].IsValid() )
		{
			problems.push_back( { ProblemKind_t::PROBLEM_INVALID_SOLID, nObject, "Invalid solid structure", {}, false } );
		}
	}
	bool bPlayerStart = false;
	for ( std::size_t i = 0; i < map.m_Entities.size(); i++ )
	{
		const Entity_t &entity = map.m_Entities[ i ];
		const std::uint32_t nObject = scene.EntityObject( i );
		if ( entity.m_bDeleted )
		{
			continue;
		}
		const std::string_view szClass = entity.GetClassname();
		bPlayerStart = bPlayerStart || Lower( szClass ).starts_with( "info_player_" );
		if ( !included( nObject ) )
		{
			continue;
		}
		const FgdClass_t *pClass = scene.m_pFgd ? scene.m_pFgd->Find( szClass ) : nullptr;
		const bool bSolids = std::ranges::any_of( entity.m_Solids, [ & ]( std::uint32_t nSolid ) { return nSolid < map.m_Solids.size() && !map.m_Solids[ nSolid ].m_bDeleted; } );
		if ( pClass )
		{
			if ( pClass->m_nKind == FgdClassKind_t::FGD_CLASS_KIND_SOLID && !bSolids )
			{
				problems.push_back( { ProblemKind_t::PROBLEM_EMPTY_SOLID_ENTITY, nObject, "Solid Entity (" + NameOf( entity ) + ") is empty.", {}, true } );
			}
			else if ( pClass->m_nKind != FgdClassKind_t::FGD_CLASS_KIND_SOLID && bSolids )
			{
				problems.push_back( { ProblemKind_t::PROBLEM_POINT_CLASS_SOLIDS, nObject, "Entity (" + NameOf( entity ) + ") is a point class with solids.", {}, false } );
			}
			for ( const EntityKey_t &key : entity.m_Keys )
			{
				if ( key.m_bDeleted || IsCommonKey( key.m_szKey ) )
				{
					continue;
				}
				const bool bKnown = std::ranges::any_of( pClass->m_Keys, [ & ]( const FgdKey_t &fgdKey ) { return kv1::EqualsIgnoreCase( fgdKey.m_szName, key.m_szKey ); } );
				if ( !bKnown )
				{
					problems.push_back( { ProblemKind_t::PROBLEM_UNUSED_KEY, nObject, "Entity (" + NameOf( entity ) + ") has unused keyvalue \"" + std::string{ key.m_szKey } + "\".", std::string{ key.m_szKey }, true } );
				}
			}
		}
		else if ( scene.m_pFgd )
		{
			problems.push_back( { ProblemKind_t::PROBLEM_UNKNOWN_CLASS, nObject, "Entity (" + NameOf( entity ) + ") has a class the game data doesn't know: " + std::string{ szClass } + ".", {}, false } );
		}
		else if ( !bSolids && !entity.GetValue( "origin" ).empty() )
		{
			// Without game data, a point entity is one with an origin; a brush entity has solids.
		}
		for ( const Connection_t &connection : entity.m_Connections )
		{
			if ( !connection.m_bMalformed && !TargetExists( connection.m_szTarget, names ) )
			{
				problems.push_back( { ProblemKind_t::PROBLEM_MISSING_TARGET, nObject, "Entity (" + NameOf( entity ) + ") output " + std::string{ connection.m_szOutput } + " targets \"" + std::string{ connection.m_szTarget } + "\", which nothing is named.", {}, false } );
			}
		}
	}
	if ( !bPlayerStart )
	{
		problems.push_back( { ProblemKind_t::PROBLEM_NO_PLAYER_START, std::nullopt, "There is no player start.", {}, false } );
	}
	return problems;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t FixProblem( const CScene &scene, const Problem_t &problem )
{
	return FixProblems( scene, { problem }, problem.m_nKind );
}

//-----------------------------------------------------------------------------
// Purpose: Empty brush entities deleted together; unused keys removed from each entity once.
//-----------------------------------------------------------------------------
MapEdit_t FixProblems( const CScene &scene, const std::vector<Problem_t> &problems, ProblemKind_t nKind )
{
	MapEdit_t edit;
	const CMap &map = scene.m_Map;
	switch ( nKind )
	{
		case ProblemKind_t::PROBLEM_EMPTY_SOLID_ENTITY:
		{
			std::vector<std::uint32_t> objects;
			for ( const Problem_t &problem : problems )
			{
				if ( problem.m_nKind == nKind && problem.m_nObject && !scene.IsDeleted( *problem.m_nObject ) )
				{
					objects.push_back( *problem.m_nObject );
				}
			}
			return objects.empty() ? edit : DeleteObjects( scene, objects );
		}
		case ProblemKind_t::PROBLEM_UNUSED_KEY:
		{
			std::vector<std::uint32_t> objects;
			for ( const Problem_t &problem : problems )
			{
				if ( problem.m_nKind == nKind && problem.m_nObject && !scene.IsDeleted( *problem.m_nObject ) && !scene.IsSolidObject( *problem.m_nObject ) )
				{
					objects.push_back( *problem.m_nObject );
				}
			}
			std::ranges::sort( objects );
			objects.erase( std::ranges::unique( objects ).begin(), objects.end() );
			for ( const std::uint32_t nObject : objects )
			{
				const std::uint32_t nEntity = scene.GetIndex( nObject );
				Entity_t entity = map.m_Entities[ nEntity ];
				for ( const Problem_t &problem : problems )
				{
					if ( problem.m_nKind == nKind && problem.m_nObject == nObject )
					{
						RemoveKey( entity, problem.m_szKey );
					}
				}
				edit.m_Entities.emplace_back( nEntity, std::move( entity ) );
			}
			return edit;
		}
		default:
			return edit;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> FilterEntities( const CScene &scene, const EntityFilter_t &filter )
{
	std::vector<std::uint32_t> found;
	const CMap &map = scene.m_Map;
	const std::string szClass = Lower( filter.m_szClass );
	const std::string szValue = Lower( filter.m_szValue );
	for ( std::size_t i = 0; i < map.m_Entities.size(); i++ )
	{
		const Entity_t &entity = map.m_Entities[ i ];
		if ( entity.m_bDeleted )
		{
			continue;
		}
		const bool bSolids = std::ranges::any_of( entity.m_Solids, [ & ]( std::uint32_t nSolid ) { return nSolid < map.m_Solids.size() && !map.m_Solids[ nSolid ].m_bDeleted; } );
		if ( ( filter.m_nKind == EntityFilter_t::Kind_t::KIND_POINT && bSolids ) || ( filter.m_nKind == EntityFilter_t::Kind_t::KIND_BRUSH && !bSolids ) )
		{
			continue;
		}
		if ( !szClass.empty() && Lower( entity.GetClassname() ).find( szClass ) == std::string::npos )
		{
			continue;
		}
		if ( !filter.m_szKey.empty() || !szValue.empty() )
		{
			bool bMatch = false;
			for ( const EntityKey_t &key : entity.m_Keys )
			{
				if ( key.m_bDeleted || ( !filter.m_szKey.empty() && !kv1::EqualsIgnoreCase( key.m_szKey, filter.m_szKey ) ) )
				{
					continue;
				}
				const std::string szHave = Lower( key.m_szValue );
				bMatch = szValue.empty() || ( filter.m_bExact ? szHave == szValue : szHave.find( szValue ) != std::string::npos );
				if ( bMatch )
				{
					break;
				}
			}
			if ( !bMatch )
			{
				continue;
			}
		}
		found.push_back( scene.EntityObject( i ) );
	}
	return found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> FindEntities( const CScene &scene, std::string_view szName )
{
	EntityFilter_t filter;
	filter.m_szKey = "targetname";
	filter.m_szValue = std::string{ szName };
	filter.m_bExact = true;
	return szName.empty() ? std::vector<std::uint32_t>{} : FilterEntities( scene, filter );
}

} // namespace xv::map
