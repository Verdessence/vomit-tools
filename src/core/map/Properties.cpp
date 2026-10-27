//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Object Properties sheet for a selection, worked out from the map and the FGD.
//
//===========================================================================//
#include "map/Properties.h"

#include <algorithm>
#include <optional>

#include "kv1/Kv1.h"
#include "map/Scene.h"
#include "util/Numbers.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The value every entity has for a key, or nullopt when they differ. An entity
//          without the key counts as having szMissing.
//-----------------------------------------------------------------------------
std::optional<std::string> Common( const std::vector<const Entity_t *> &entities, std::string_view szKey, std::string_view szMissing )
{
	std::optional<std::string> value;
	for ( const Entity_t *pEntity : entities )
	{
		const std::string_view szValue = pEntity->HasKey( szKey ) ? pEntity->GetValue( szKey ) : szMissing;
		if ( value && *value != szValue )
		{
			return std::nullopt;
		}
		value = std::string{ szValue };
	}
	return value;
}

//-----------------------------------------------------------------------------
// Purpose: The spawnflags an entity has, or the class's defaults when it has none.
//-----------------------------------------------------------------------------
std::uint32_t FlagsOf( const Entity_t &entity, std::uint32_t nDefault )
{
	if ( !entity.HasKey( "spawnflags" ) )
	{
		return nDefault;
	}
	return static_cast<std::uint32_t>( ParseNumber<long long>( entity.GetValue( "spawnflags" ) ).value_or( 0 ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PropertyConnection_t ConnectionRow( const Connection_t &connection, std::string_view szEntity, std::uint32_t nObject )
{
	PropertyConnection_t row;
	row.m_szEntity = szEntity;
	row.m_szOutput = connection.m_szOutput;
	row.m_szInput = connection.m_szInput;
	row.m_szParameter = connection.m_szParameter;
	row.m_flDelay = connection.m_flDelay;
	row.m_bOnce = connection.m_nTimes == 1;
	row.m_nObject = nObject;
	return row;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool TargetMatches( std::string_view szTarget, std::string_view szName )
{
	if ( szTarget.ends_with( '*' ) )
	{
		szTarget.remove_suffix( 1 );
		return szName.size() >= szTarget.size() && kv1::EqualsIgnoreCase( szName.substr( 0, szTarget.size() ), szTarget );
	}
	return kv1::EqualsIgnoreCase( szTarget, szName );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PropertySheet_t DescribeSelection( const CScene &scene, const std::vector<std::uint32_t> &objects, bool bSmartEdit )
{
	PropertySheet_t sheet;
	const CMap &map = scene.m_Map;
	std::vector<std::size_t> indices;
	std::vector<bool> seen( map.m_Entities.size() );
	for ( const std::uint32_t n : objects )
	{
		const int nEntity = scene.GetObjectEntity( n );
		if ( nEntity < 0 )
		{
			sheet.m_nWorldSolids += scene.IsSolidObject( n ) ? 1 : 0;
		}
		else if ( !seen[ static_cast<std::size_t>( nEntity ) ] )
		{
			seen[ static_cast<std::size_t>( nEntity ) ] = true;
			indices.push_back( static_cast<std::size_t>( nEntity ) );
		}
	}
	sheet.m_nEntities = indices.size();
	if ( indices.empty() )
	{
		return sheet;
	}
	std::vector<const Entity_t *> entities;
	for ( const std::size_t i : indices )
	{
		entities.push_back( &map.m_Entities[ i ] );
	}
	const Entity_t &first = *entities.front();
	const bool bSameClass = std::ranges::all_of( entities, [ & ]( const Entity_t *pEntity ) { return kv1::EqualsIgnoreCase( pEntity->GetClassname(), first.GetClassname() ); } );
	const FgdClass_t *pClass = nullptr;
	if ( bSameClass )
	{
		sheet.m_szClass = first.GetClassname();
		pClass = scene.m_pFgd ? scene.m_pFgd->Find( sheet.m_szClass ) : nullptr;
	}
	sheet.m_bKnownClass = pClass != nullptr;
	if ( pClass )
	{
		sheet.m_szClassHelp = pClass->m_szDescription;
	}
	const FgdKey_t *pFlagsKey = pClass ? pClass->FindKey( "spawnflags" ) : nullptr;
	pFlagsKey = pFlagsKey && pFlagsKey->m_szType == "flags" ? pFlagsKey : nullptr;

	// The class is shown on its own, and the flags on their own tab.
	auto listed = [ & ]( std::string_view szKey ) { return !kv1::EqualsIgnoreCase( szKey, "classname" ) && !( pFlagsKey && kv1::EqualsIgnoreCase( szKey, "spawnflags" ) ); };
	auto add = [ & ]( PropertyRow_t row, const std::optional<std::string> &value ) {
		row.m_bDiffers = !value;
		row.m_szValue = value.value_or( std::string{} );
		row.m_szShown = row.m_szValue;
		sheet.m_Rows.push_back( std::move( row ) );
	};
	std::vector<std::string_view> done;
	if ( bSmartEdit && pClass )
	{
		// The class's keys in the FGD's order, a key no entity has showing its default.
		for ( const FgdKey_t &key : pClass->m_Keys )
		{
			if ( key.m_szType == "flags" || !listed( key.m_szName ) )
			{
				continue;
			}
			PropertyRow_t row;
			row.m_szKey = key.m_szName;
			row.m_szName = key.m_szDisplayName.empty() ? key.m_szName : key.m_szDisplayName;
			row.m_szHelp = key.m_szDescription;
			row.m_bDefault = std::ranges::none_of( entities, [ & ]( const Entity_t *pEntity ) { return pEntity->HasKey( key.m_szName ); } );
			add( std::move( row ), Common( entities, key.m_szName, key.m_szDefault ) );
			PropertyRow_t &added = sheet.m_Rows.back();
			if ( key.m_szType == "choices" )
			{
				const auto choice = std::ranges::find_if( key.m_Choices, [ & ]( const FgdChoice_t &c ) { return c.m_szValue == added.m_szValue; } );
				added.m_szShown = choice != key.m_Choices.end() && !added.m_bDiffers ? choice->m_szName : added.m_szValue;
			}
			done.push_back( key.m_szName );
		}
	}
	// Keys as the file has them (with SmartEdit, the ones the FGD doesn't know), in the first
	// entity's order, when every entity has them.
	for ( const EntityKey_t &key : first.m_Keys )
	{
		const bool bDone = std::ranges::any_of( done, [ & ]( std::string_view szDone ) { return kv1::EqualsIgnoreCase( szDone, key.m_szKey ); } );
		if ( bDone || !listed( key.m_szKey ) || !std::ranges::all_of( entities, [ & ]( const Entity_t *pEntity ) { return pEntity->HasKey( key.m_szKey ); } ) )
		{
			continue;
		}
		PropertyRow_t row;
		row.m_szKey = key.m_szKey;
		row.m_szName = key.m_szKey;
		if ( const FgdKey_t *pKey = pClass ? pClass->FindKey( key.m_szKey ) : nullptr )
		{
			row.m_szHelp = pKey->m_szDescription;
		}
		add( std::move( row ), Common( entities, key.m_szKey, {} ) );
		done.push_back( key.m_szKey );
	}

	if ( pFlagsKey )
	{
		std::uint32_t nDefault = 0;
		for ( const FgdChoice_t &choice : pFlagsKey->m_Choices )
		{
			nDefault |= choice.m_bDefault ? static_cast<std::uint32_t>( ParseNumber<long long>( choice.m_szValue ).value_or( 0 ) ) : 0u;
		}
		for ( const FgdChoice_t &choice : pFlagsKey->m_Choices )
		{
			PropertyFlag_t flag;
			flag.m_nBit = static_cast<std::uint32_t>( ParseNumber<long long>( choice.m_szValue ).value_or( 0 ) );
			flag.m_szName = choice.m_szName;
			const auto nOn = std::ranges::count_if( entities, [ & ]( const Entity_t *pEntity ) { return ( FlagsOf( *pEntity, nDefault ) & flag.m_nBit ) != 0; } );
			flag.m_nState = nOn == 0                                    ? PropertyFlagState_t::PROPERTY_FLAG_STATE_OFF
				: nOn == static_cast<std::ptrdiff_t>( entities.size() ) ? PropertyFlagState_t::PROPERTY_FLAG_STATE_ON
																		: PropertyFlagState_t::PROPERTY_FLAG_STATE_MIXED;
			sheet.m_Flags.push_back( std::move( flag ) );
		}
	}

	for ( std::size_t k = 0; k < entities.size(); k++ )
	{
		for ( const Connection_t &connection : entities[ k ]->m_Connections )
		{
			sheet.m_Outputs.push_back( ConnectionRow( connection, connection.m_szTarget, scene.EntityObject( indices[ k ] ) ) );
		}
	}
	// The outputs anywhere in the map aimed at a selected entity's name.
	std::vector<std::string_view> names;
	for ( const Entity_t *pEntity : entities )
	{
		if ( const std::string_view szName = pEntity->GetValue( "targetname" ); !szName.empty() )
		{
			names.push_back( szName );
		}
	}
	for ( std::size_t i = 0; i < map.m_Entities.size() && !names.empty(); i++ )
	{
		const Entity_t &sender = map.m_Entities[ i ];
		const std::string_view szSender = sender.GetValue( "targetname" ).empty() ? sender.GetClassname() : sender.GetValue( "targetname" );
		for ( const Connection_t &connection : sender.m_Connections )
		{
			if ( std::ranges::any_of( names, [ & ]( std::string_view szName ) { return TargetMatches( connection.m_szTarget, szName ); } ) )
			{
				sheet.m_Inputs.push_back( ConnectionRow( connection, szSender, scene.EntityObject( i ) ) );
			}
		}
	}
	if ( entities.size() == 1 )
	{
		sheet.m_szComments = first.m_Editor.m_szComments;
	}
	return sheet;
}

} // namespace xv::map
