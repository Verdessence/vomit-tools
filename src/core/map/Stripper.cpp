//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Stripper.h.
//
//===========================================================================//
#include "map/Stripper.h"

#include <algorithm>
#include <optional>

#include "kv1/Kv1.h"
#include "map/Entities.h"
#include "map/Io.h"
#include "map/VmfValues.h"
#include "util/Numbers.h"

namespace xv::map
{

namespace
{

constexpr Color_t ADDED_COLOR{ 60, 200, 60 };
constexpr Color_t MODIFIED_COLOR{ 230, 190, 40 };
constexpr Color_t REMOVED_COLOR{ 220, 50, 50 };
constexpr Color_t GROUP_COLOR{ 200, 120, 220 };

//-----------------------------------------------------------------------------
// Purpose: An output as a compiled map has it (CS:S's commas).
//-----------------------------------------------------------------------------
std::string OutputValue( const Connection_t &connection )
{
	return std::string{ connection.m_szTarget } + ',' + std::string{ connection.m_szInput } + ',' + std::string{ connection.m_szParameter } + ',' + FormatNumber( connection.m_flDelay ) + ',' + std::to_string( connection.m_nTimes );
}

//-----------------------------------------------------------------------------
// Purpose: The visgroup's index of that name under nParent, added (with the next free id) when
//          there is none.
//-----------------------------------------------------------------------------
int FindOrAddVisgroup( const CMap &map, std::vector<Visgroup_t> &visgroups, std::string_view szName, int nParent, Color_t color )
{
	for ( std::size_t i = 0; i < visgroups.size(); i++ )
	{
		if ( visgroups[ i ].m_nParent == nParent && visgroups[ i ].m_szName == szName )
			return static_cast<int>( i );
	}
	int nId = 0;
	for ( const Visgroup_t &group : visgroups )
		nId = std::max( nId, group.m_nId );
	Visgroup_t group;
	group.m_szName = map.Store( std::string{ szName } );
	group.m_nId = nId + 1;
	group.m_Color = color;
	group.m_nParent = nParent;
	visgroups.push_back( group );
	return static_cast<int>( visgroups.size() - 1 );
}

//-----------------------------------------------------------------------------
// Purpose: The editor notes in a visgroup too (once), and hidden by it when bHide.
//-----------------------------------------------------------------------------
void JoinVisgroup( EditorInfo_t &editor, int nId, bool bHide )
{
	if ( std::ranges::find( editor.m_Visgroups, nId ) == editor.m_Visgroups.end() )
		editor.m_Visgroups.push_back( nId );
	if ( bHide )
		editor.m_bVisgroupShown = false;
	editor.m_bChanged = true;
}

//-----------------------------------------------------------------------------
// Purpose: The entity's keys and outputs made what the config left of them: keys gone are
//          taken out (their places kept), changed ones set, new ones added; the outputs replaced
//          only when they differ.
//-----------------------------------------------------------------------------
void TakeKeys( const CMap &map, Entity_t &entity, const bsp::Entity_t &result )
{
	std::vector<std::pair<std::string_view, std::string_view>> keys;
	std::vector<std::pair<std::string_view, std::string_view>> outputs;
	for ( const auto &[ szKey, szValue ] : result.m_KeyValues )
	{
		if ( kv1::EqualsIgnoreCase( szKey, "hammerid" ) )
			continue;
		( IsStripperOutput( szValue ) ? outputs : keys ).emplace_back( szKey, szValue );
	}
	std::vector<std::string> gone;
	for ( const EntityKey_t &key : entity.m_Keys )
	{
		if ( !key.m_bDeleted && std::ranges::none_of( keys, [ & ]( const auto &pair ) { return kv1::EqualsIgnoreCase( pair.first, key.m_szKey ); } ) )
			gone.emplace_back( key.m_szKey );
	}
	for ( const std::string &szKey : gone )
		RemoveKey( entity, szKey );
	for ( const auto &[ szKey, szValue ] : keys )
		SetKeyValue( map, entity, szKey, szValue );

	std::vector<std::pair<std::string, std::string>> before;
	for ( const Connection_t &connection : entity.m_Connections )
		before.emplace_back( std::string{ connection.m_szOutput }, OutputValue( connection ) );
	const bool bSame = before.size() == outputs.size() && std::ranges::equal( before, outputs, []( const auto &a, const auto &b ) { return a.first == b.first && a.second == b.second; } );
	if ( bSame )
		return;
	const char chSeparator = entity.m_Connections.empty() ? ',' : entity.m_Connections.front().m_chSeparator;
	std::vector<Connection_t> connections;
	for ( const auto &[ szOutput, szValue ] : outputs )
	{
		Connection_t connection = ParseConnection( map.Store( std::string{ szOutput } ), map.Store( std::string{ szValue } ) );
		connection.m_chSeparator = chSeparator;
		connections.push_back( connection );
	}
	SetConnections( map, entity, std::move( connections ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<bsp::Entity_t> StripperEntitiesOf( const CMap &map, std::vector<int> *pIndices )
{
	std::vector<bsp::Entity_t> entities;
	const auto add = [ & ]( const Entity_t &entity, int nIndex ) {
		bsp::Entity_t out;
		for ( const EntityKey_t &key : entity.m_Keys )
		{
			if ( !key.m_bDeleted )
				out.m_KeyValues.emplace_back( std::string{ key.m_szKey }, std::string{ key.m_szValue } );
		}
		out.m_KeyValues.emplace_back( "hammerid", std::to_string( entity.m_nId ) );
		for ( const Connection_t &connection : entity.m_Connections )
			out.m_KeyValues.emplace_back( std::string{ connection.m_szOutput }, OutputValue( connection ) );
		entities.push_back( std::move( out ) );
		if ( pIndices )
			pIndices->push_back( nIndex );
	};
	add( map.m_World, -1 );
	for ( std::size_t i = 0; i < map.m_Entities.size(); i++ )
	{
		if ( !map.m_Entities[ i ].m_bDeleted )
			add( map.m_Entities[ i ], static_cast<int>( i ) );
	}
	return entities;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsStripperOutput( std::string_view szValue )
{
	const char chSeparator = szValue.find( '\x1b' ) != std::string_view::npos ? '\x1b' : ',';
	if ( std::ranges::count( szValue, chSeparator ) < 4 )
		return false;
	// The delay and times are the last two fields; the parameter may hold the separator itself.
	const std::size_t nLast = szValue.rfind( chSeparator );
	const std::size_t nDelay = szValue.rfind( chSeparator, nLast - 1 );
	return ParseNumber<double>( szValue.substr( nDelay + 1, nLast - nDelay - 1 ) ).has_value() && ParseNumber<int>( szValue.substr( nLast + 1 ) ).has_value();
}

//-----------------------------------------------------------------------------
// Purpose: Filtered entities are kept but hidden, so nothing is lost: deleting them is the
//          mapper's call. An entity a later rule removes after an earlier one added it is left
//          out altogether.
//-----------------------------------------------------------------------------
StripperImport_t ImportStripper( const CScene &scene, const std::vector<bsp::StripperConfig_t> &configs, std::string_view szName )
{
	StripperImport_t result;
	const CMap &map = scene.m_Map;
	std::vector<int> indices;
	const std::vector<bsp::Entity_t> entities = StripperEntitiesOf( map, &indices );
	const std::vector<bsp::StrippedEntity_t> stripped = bsp::ApplyStripper( entities, configs );

	std::vector<Visgroup_t> visgroups = map.m_Visgroups;
	const std::size_t nVisgroupsBefore = visgroups.size();
	const int nGroup = FindOrAddVisgroup( map, visgroups, "Stripper: " + std::string{ szName }, -1, GROUP_COLOR );
	std::optional<int> added, modified, removed;
	const auto idOf = [ & ]( std::optional<int> &nIndex, const char *pszName, Color_t color ) {
		if ( !nIndex )
			nIndex = FindOrAddVisgroup( map, visgroups, pszName, nGroup, color );
		return visgroups[ static_cast<std::size_t>( *nIndex ) ].m_nId;
	};

	MapEdit_t &edit = result.m_Edit;
	std::vector<Entity_t> newEntities;
	std::optional<Entity_t> world;
	for ( const bsp::StrippedEntity_t &entity : stripped )
	{
		using Status_t = bsp::StrippedEntity_t::Status_t;
		if ( entity.m_nStatus == Status_t::STATUS_KEPT )
			continue;
		if ( !entity.m_Original )
		{
			if ( entity.m_nStatus != Status_t::STATUS_ADDED )
				continue;
			Entity_t made;
			made.m_Editor.m_Color = ENTITY_COLOR;
			made.m_Editor.m_bHasColor = true;
			JoinVisgroup( made.m_Editor, idOf( added, "Added", ADDED_COLOR ), false );
			bsp::Entity_t keys = entity.m_Entity;
			TakeKeys( map, made, keys );
			newEntities.push_back( std::move( made ) );
			result.m_nAdded++;
			continue;
		}
		const int nIndex = indices[ *entity.m_Original ];
		const bool bRemoved = entity.m_nStatus == Status_t::STATUS_REMOVED;
		if ( nIndex < 0 )
		{
			// The world can only be modified (a filter on worldspawn would empty the map).
			if ( bRemoved )
				continue;
			world = map.m_World;
			TakeKeys( map, *world, entity.m_Entity );
			result.m_nModified++;
			continue;
		}
		Entity_t changed = map.m_Entities[ static_cast<std::size_t>( nIndex ) ];
		const int nId = bRemoved ? idOf( removed, "Removed", REMOVED_COLOR ) : idOf( modified, "Modified", MODIFIED_COLOR );
		JoinVisgroup( changed.m_Editor, nId, bRemoved );
		if ( !bRemoved )
			TakeKeys( map, changed, entity.m_Entity );
		// A brush entity's solids go with it, as Hammer puts them in its visgroups.
		for ( const std::uint32_t nSolid : changed.m_Solids )
		{
			Solid_t solid = map.m_Solids[ nSolid ];
			JoinVisgroup( solid.m_Editor, nId, bRemoved );
			solid.m_bChanged = true;
			edit.m_Solids.emplace_back( nSolid, std::move( solid ) );
		}
		edit.m_Entities.emplace_back( static_cast<std::uint32_t>( nIndex ), std::move( changed ) );
		( bRemoved ? result.m_nRemoved : result.m_nModified )++;
	}
	if ( result.m_nAdded + result.m_nModified + result.m_nRemoved == 0 )
	{
		return {};
	}
	if ( world )
		edit.m_World = std::move( world );
	if ( !newEntities.empty() )
	{
		MapEdit_t adding = AddObjects( scene, {}, std::move( newEntities ), &result.m_Added );
		for ( auto &pair : adding.m_Entities )
			edit.m_Entities.push_back( std::move( pair ) );
	}
	if ( visgroups.size() != nVisgroupsBefore )
		edit.m_Visgroups = std::move( visgroups );
	result.m_nGroupId = edit.m_Visgroups ? ( *edit.m_Visgroups )[ static_cast<std::size_t>( nGroup ) ].m_nId : map.m_Visgroups[ static_cast<std::size_t>( nGroup ) ].m_nId;
	const auto &groups = edit.m_Visgroups ? *edit.m_Visgroups : map.m_Visgroups;
	result.m_nRemovedId = removed ? groups[ static_cast<std::size_t>( *removed ) ].m_nId : 0;
	return result;
}

} // namespace xv::map
