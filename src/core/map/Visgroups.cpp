//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's automatic visgroups. Which groups Hammer has and what goes in each is
//          matched to Hammer by eye (docs/plan/hammer-check.md); the FGD article describes how
//          @AutoVisGroup entries merge into them.
//
//===========================================================================//
#include "map/Visgroups.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <string_view>

#include "kv1/Kv1.h"
#include "map/Edit.h"
#include "map/Scene.h"

namespace xv::map
{

namespace
{

// The tool brush groups, the most telling texture first: a hint brush's other faces are skip
// and an areaportal's nodraw, so a brush goes with its rarest tool texture.
struct ToolGroup_t
{
	std::string_view m_szName;
	std::array<std::string_view, 3> m_Materials;
};

constexpr std::array<ToolGroup_t, 9> TOOL_GROUPS{ {
	{ "Hints", { "tools/toolshint" } },
	{ "Areaportals", { "tools/toolsareaportal" } },
	{ "Occluders", { "tools/toolsoccluder" } },
	{ "Origins", { "tools/toolsorigin" } },
	{ "Triggers", { "tools/toolstrigger" } },
	{ "Clips", { "tools/toolsclip", "tools/toolsplayerclip", "tools/toolsnpcclip" } },
	{ "Skybox", { "tools/toolsskybox", "tools/toolsskybox2d" } },
	{ "Skips", { "tools/toolsskip" } },
	{ "Nodraws", { "tools/toolsnodraw" } },
} };

//-----------------------------------------------------------------------------
// Purpose: A material's place in TOOL_GROUPS; TOOL_GROUPS.size() for other tool textures, -1 for
//          anything that isn't a tool texture.
//-----------------------------------------------------------------------------
int ToolRank( std::string_view szMaterial )
{
	if ( !szMaterial.starts_with( "tools/" ) )
	{
		return -1;
	}
	for ( std::size_t i = 0; i < TOOL_GROUPS.size(); i++ )
	{
		if ( std::ranges::find( TOOL_GROUPS[ i ].m_Materials, szMaterial ) != TOOL_GROUPS[ i ].m_Materials.end() )
		{
			return static_cast<int>( i );
		}
	}
	return static_cast<int>( TOOL_GROUPS.size() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool StartsWithIgnoreCase( std::string_view szText, std::string_view szLowerStart )
{
	return szText.size() >= szLowerStart.size() && kv1::EqualsIgnoreCase( szText.substr( 0, szLowerStart.size() ), szLowerStart );
}

//-----------------------------------------------------------------------------
// Purpose: The group of that name inside parent, added when it isn't there yet.
//-----------------------------------------------------------------------------
AutoVisgroup_t &Child( AutoVisgroup_t &parent, std::string_view szName )
{
	for ( AutoVisgroup_t &child : parent.m_Children )
	{
		if ( kv1::EqualsIgnoreCase( child.m_szName, szName ) )
		{
			return child;
		}
	}
	AutoVisgroup_t &child = parent.m_Children.emplace_back();
	child.m_szName = szName;
	return child;
}

//-----------------------------------------------------------------------------
// Purpose: The first group of that name anywhere in the tree, depth first.
//-----------------------------------------------------------------------------
AutoVisgroup_t *FindGroup( AutoVisgroup_t &group, std::string_view szName )
{
	if ( kv1::EqualsIgnoreCase( group.m_szName, szName ) )
	{
		return &group;
	}
	for ( AutoVisgroup_t &child : group.m_Children )
	{
		if ( AutoVisgroup_t *pFound = FindGroup( child, szName ) )
		{
			return pFound;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Sorts every group's members and drops the groups left with none. Returns whether
//          the group itself is kept.
//-----------------------------------------------------------------------------
bool Tidy( AutoVisgroup_t &group )
{
	std::ranges::sort( group.m_Objects );
	const auto [ first, last ] = std::ranges::unique( group.m_Objects );
	group.m_Objects.erase( first, last );
	std::erase_if( group.m_Children, []( AutoVisgroup_t &child ) { return !Tidy( child ); } );
	return !group.m_Objects.empty() || !group.m_Children.empty();
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
AutoVisgroup_t BuildAutoVisgroups( const CScene &scene )
{
	const CMap &map = scene.m_Map;
	AutoVisgroup_t root;
	root.m_szName = "Auto";
	// The tree is laid out first, so the references taken below stay put while objects go in.
	Child( Child( root, "Brush Entities" ), "Triggers" );
	Child( root, "Displacements" );
	for ( const std::string_view szName : { "Lights", "Nodes", "NPCs" } )
	{
		Child( Child( root, "Point Entities" ), szName );
	}
	std::array<std::string_view, TOOL_GROUPS.size()> toolNames{};
	std::ranges::transform( TOOL_GROUPS, toolNames.begin(), []( const ToolGroup_t &group ) { return group.m_szName; } );
	std::ranges::sort( toolNames );
	for ( const std::string_view szName : toolNames )
	{
		Child( Child( root, "Tool Brushes" ), szName );
	}
	Child( root, "World Details" );
	Child( root, "World Geometry" );

	AutoVisgroup_t &brushEntities = Child( root, "Brush Entities" );
	AutoVisgroup_t &triggers = Child( brushEntities, "Triggers" );
	AutoVisgroup_t &displacements = Child( root, "Displacements" );
	AutoVisgroup_t &pointEntities = Child( root, "Point Entities" );
	AutoVisgroup_t &lights = Child( pointEntities, "Lights" );
	AutoVisgroup_t &nodes = Child( pointEntities, "Nodes" );
	AutoVisgroup_t &npcs = Child( pointEntities, "NPCs" );
	AutoVisgroup_t &toolBrushes = Child( root, "Tool Brushes" );
	std::array<AutoVisgroup_t *, TOOL_GROUPS.size()> toolGroups{};
	for ( std::size_t i = 0; i < TOOL_GROUPS.size(); i++ )
	{
		toolGroups[ i ] = &Child( toolBrushes, TOOL_GROUPS[ i ].m_szName );
	}
	AutoVisgroup_t &worldDetails = Child( root, "World Details" );
	AutoVisgroup_t &worldGeometry = Child( root, "World Geometry" );

	// A brush entity and its solids go together, so hiding the group hides both.
	auto brushEntity = [ & ]( std::string_view szClass, std::uint32_t nObject ) {
		if ( kv1::EqualsIgnoreCase( szClass, "func_detail" ) )
		{
			worldDetails.m_Objects.push_back( nObject );
			return;
		}
		brushEntities.m_Objects.push_back( nObject );
		if ( StartsWithIgnoreCase( szClass, "trigger_" ) )
		{
			triggers.m_Objects.push_back( nObject );
		}
	};

	std::vector<int> ranks( map.m_Materials.size() );
	std::ranges::transform( map.m_Materials, ranks.begin(), []( const std::string &szMaterial ) { return ToolRank( szMaterial ); } );
	for ( std::uint32_t s = 0; s < map.m_Solids.size(); s++ )
	{
		const Solid_t &solid = map.m_Solids[ s ];
		const std::uint32_t n = scene.SolidObject( s );
		if ( solid.m_bDeleted )
		{
			continue;
		}
		bool bAllTools = !solid.m_Sides.empty();
		bool bDisplacement = false;
		int nBest = static_cast<int>( TOOL_GROUPS.size() );
		for ( const Side_t &side : solid.m_Sides )
		{
			const int nRank = side.m_nMaterial < ranks.size() ? ranks[ side.m_nMaterial ] : -1;
			bAllTools = bAllTools && nRank >= 0;
			nBest = nRank >= 0 ? std::min( nBest, nRank ) : nBest;
			bDisplacement = bDisplacement || side.m_pDisp;
		}
		if ( bAllTools )
		{
			( nBest < static_cast<int>( TOOL_GROUPS.size() ) ? *toolGroups[ static_cast<std::size_t>( nBest ) ] : toolBrushes ).m_Objects.push_back( n );
		}
		if ( solid.m_nEntity >= 0 )
		{
			brushEntity( map.m_Entities[ static_cast<std::size_t>( solid.m_nEntity ) ].GetClassname(), n );
		}
		else if ( bDisplacement )
		{
			displacements.m_Objects.push_back( n );
		}
		else if ( !bAllTools )
		{
			worldGeometry.m_Objects.push_back( n );
		}
	}
	for ( std::size_t i = 0; i < map.m_Entities.size(); i++ )
	{
		const Entity_t &entity = map.m_Entities[ i ];
		const std::string_view szClass = entity.GetClassname();
		const std::uint32_t nObject = scene.EntityObject( i );
		if ( entity.m_bDeleted )
		{
			continue;
		}
		if ( !entity.m_Solids.empty() )
		{
			brushEntity( szClass, nObject );
			continue;
		}
		pointEntities.m_Objects.push_back( nObject );
		if ( StartsWithIgnoreCase( szClass, "light" ) )
		{
			lights.m_Objects.push_back( nObject );
		}
		else if ( StartsWithIgnoreCase( szClass, "info_node" ) )
		{
			nodes.m_Objects.push_back( nObject );
		}
		else if ( StartsWithIgnoreCase( szClass, "npc_" ) )
		{
			npcs.m_Objects.push_back( nObject );
		}
		else if ( kv1::EqualsIgnoreCase( szClass, "prop_static" ) || kv1::EqualsIgnoreCase( szClass, "prop_detail" ) )
		{
			worldDetails.m_Objects.push_back( nObject );
		}
	}

	// The FGDs' own groups: under a group of the parent's name wherever it is, or a new one.
	if ( scene.m_pFgd && !scene.m_pFgd->GetAutoVisGroups().empty() )
	{
		std::map<std::string, std::vector<std::size_t>> byClass;
		for ( std::size_t i = 0; i < map.m_Entities.size(); i++ )
		{
			std::string szClass{ map.m_Entities[ i ].GetClassname() };
			std::ranges::transform( szClass, szClass.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
			byClass[ szClass ].push_back( i );
		}
		for ( const FgdAutoVisGroup_t &entry : scene.m_pFgd->GetAutoVisGroups() )
		{
			AutoVisgroup_t *pParent = FindGroup( root, entry.m_szParent );
			AutoVisgroup_t &group = Child( pParent ? *pParent : Child( root, entry.m_szParent ), entry.m_szName );
			for ( std::string szClass : entry.m_Classes )
			{
				std::ranges::transform( szClass, szClass.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
				const auto found = byClass.find( szClass );
				if ( found == byClass.end() )
				{
					continue;
				}
				for ( const std::size_t i : found->second )
				{
					group.m_Objects.push_back( scene.EntityObject( i ) );
					group.m_Objects.insert( group.m_Objects.end(), map.m_Entities[ i ].m_Solids.begin(), map.m_Entities[ i ].m_Solids.end() );
				}
			}
		}
	}
	Tidy( root );
	return root;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> AutoVisgroupMembers( const AutoVisgroup_t &group )
{
	std::vector<std::uint32_t> members = group.m_Objects;
	for ( const AutoVisgroup_t &child : group.m_Children )
	{
		const std::vector<std::uint32_t> more = AutoVisgroupMembers( child );
		members.insert( members.end(), more.begin(), more.end() );
	}
	std::ranges::sort( members );
	const auto [ first, last ] = std::ranges::unique( members );
	members.erase( first, last );
	return members;
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Each object's editor notes changed by pfnChange (which says whether it changed
//          them): a solid's own, an entity's (a brush entity's solids keep theirs).
//-----------------------------------------------------------------------------
MapEdit_t ChangeEditors( const CScene &scene, const std::vector<std::uint32_t> &objects, const std::function<bool( EditorInfo_t & )> &pfnChange )
{
	MapEdit_t edit;
	for ( const std::uint32_t nObject : objects )
	{
		if ( nObject >= scene.GetObjectCount() || scene.IsDeleted( nObject ) )
			continue;
		const std::uint32_t nIndex = scene.GetIndex( nObject );
		if ( scene.IsSolidObject( nObject ) )
		{
			Solid_t solid = scene.m_Map.m_Solids[ nIndex ];
			if ( !pfnChange( solid.m_Editor ) )
				continue;
			solid.m_Editor.m_bChanged = true;
			solid.m_bChanged = true;
			edit.m_Solids.emplace_back( nIndex, std::move( solid ) );
		}
		else
		{
			Entity_t entity = scene.m_Map.m_Entities[ nIndex ];
			if ( !pfnChange( entity.m_Editor ) )
				continue;
			entity.m_Editor.m_bChanged = true;
			edit.m_Entities.emplace_back( nIndex, std::move( entity ) );
		}
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose: Every object that is in the visgroup.
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> MembersOf( const CScene &scene, int nId )
{
	std::vector<std::uint32_t> members;
	const auto in = [ nId ]( const EditorInfo_t &editor ) { return std::ranges::find( editor.m_Visgroups, nId ) != editor.m_Visgroups.end(); };
	for ( std::size_t s = 0; s < scene.m_Map.m_Solids.size(); s++ )
	{
		if ( !scene.m_Map.m_Solids[ s ].m_bDeleted && in( scene.m_Map.m_Solids[ s ].m_Editor ) )
			members.push_back( scene.SolidObject( s ) );
	}
	for ( std::size_t e = 0; e < scene.m_Map.m_Entities.size(); e++ )
	{
		if ( !scene.m_Map.m_Entities[ e ].m_bDeleted && in( scene.m_Map.m_Entities[ e ].m_Editor ) )
			members.push_back( scene.EntityObject( e ) );
	}
	return members;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t JoinVisgroup( const CScene &scene, const std::vector<std::uint32_t> &objects, int nId )
{
	return ChangeEditors( scene, objects, [ nId ]( EditorInfo_t &editor ) {
		if ( std::ranges::find( editor.m_Visgroups, nId ) != editor.m_Visgroups.end() )
			return false;
		editor.m_Visgroups.push_back( nId );
		return true;
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t LeaveVisgroup( const CScene &scene, const std::vector<std::uint32_t> &objects, int nId )
{
	return ChangeEditors( scene, objects, [ nId ]( EditorInfo_t &editor ) { return std::erase( editor.m_Visgroups, nId ) > 0; } );
}

//-----------------------------------------------------------------------------
// Purpose: A colour from the id, bright enough to read in the lists (Hammer picks at random).
//-----------------------------------------------------------------------------
MapEdit_t NewVisgroup( const CScene &scene, std::string_view szName, const std::vector<std::uint32_t> &objects, int *pId )
{
	std::vector<Visgroup_t> visgroups = scene.m_Map.m_Visgroups;
	int nId = 0;
	for ( const Visgroup_t &group : visgroups )
		nId = std::max( nId, group.m_nId );
	nId++;
	Visgroup_t group;
	group.m_szName = scene.m_Map.Store( std::string{ szName } );
	group.m_nId = nId;
	const std::uint32_t nHash = static_cast<std::uint32_t>( nId ) * 2654435761u;
	group.m_Color = { static_cast<std::uint8_t>( 96 + ( nHash >> 8 ) % 160 ), static_cast<std::uint8_t>( 96 + ( nHash >> 16 ) % 160 ), static_cast<std::uint8_t>( 96 + ( nHash >> 24 ) % 160 ) };
	visgroups.push_back( group );
	MapEdit_t edit = JoinVisgroup( scene, objects, nId );
	edit.m_Visgroups = std::move( visgroups );
	if ( pId )
		*pId = nId;
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t RenameVisgroup( const CScene &scene, int nId, std::string_view szName )
{
	MapEdit_t edit;
	std::vector<Visgroup_t> visgroups = scene.m_Map.m_Visgroups;
	const auto found = std::ranges::find( visgroups, nId, &Visgroup_t::m_nId );
	if ( found == visgroups.end() || found->m_szName == szName )
		return edit;
	found->m_szName = scene.m_Map.Store( std::string{ szName } );
	edit.m_Visgroups = std::move( visgroups );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t DeleteVisgroup( const CScene &scene, int nId )
{
	const auto &groups = scene.m_Map.m_Visgroups;
	const auto found = std::ranges::find( groups, nId, &Visgroup_t::m_nId );
	if ( found == groups.end() )
		return {};
	const int nGone = static_cast<int>( found - groups.begin() );
	MapEdit_t edit = LeaveVisgroup( scene, MembersOf( scene, nId ), nId );
	std::vector<Visgroup_t> visgroups;
	for ( int i = 0; i < static_cast<int>( groups.size() ); i++ )
	{
		if ( i == nGone )
			continue;
		Visgroup_t group = groups[ static_cast<std::size_t>( i ) ];
		if ( group.m_nParent == nGone )
			group.m_nParent = found->m_nParent;
		if ( group.m_nParent > nGone )
			group.m_nParent--;
		visgroups.push_back( group );
	}
	edit.m_Visgroups = std::move( visgroups );
	return edit;
}

} // namespace xv::map
