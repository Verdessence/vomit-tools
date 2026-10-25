//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Stripper:Source configs, which change a map's entities as it loads without touching
//          the .bsp (addons/stripper/global_filters.cfg, then addons/stripper/maps/<map>.cfg):
//          reading them, and what they make of a map's entities, for a preview.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Bsp.h"

namespace xv::bsp
{

struct StripperRule_t
{
	enum class Kind_t
	{
		KIND_ADD,
		KIND_FILTER,
		KIND_MODIFY
	};
	Kind_t m_nKind = Kind_t::KIND_ADD;
	Entity_t m_Entity; // add: the entity; filter: what to match; modify: its match block
	Entity_t m_Replace;
	Entity_t m_Delete;
	Entity_t m_Insert;
	std::string m_szWhere; // "global_filters.cfg:12", for showing which rule did what
};

struct StripperConfig_t
{
	std::vector<StripperRule_t> m_Rules;
	std::vector<std::string> m_Problems; // with their line numbers
};

// Reads a config; szName goes in front of each rule's line number. Sections are add:, filter:
// (remove: too) and modify: with match:, replace:, delete: and insert: inside. Lines starting with
// ;, // or # are comments.
[[nodiscard]] StripperConfig_t ParseStripper( std::string_view szText, std::string_view szName );

// Whether a match block holds for an entity: every pair in it has a pair in the entity with the
// same key (in any case) and the same value, or a value its /regex/ finds.
[[nodiscard]] bool StripperMatches( const Entity_t &match, const Entity_t &entity );

struct StrippedEntity_t
{
	enum class Status_t
	{
		STATUS_KEPT,
		STATUS_MODIFIED,
		STATUS_ADDED,
		STATUS_REMOVED
	};
	Entity_t m_Entity;                // as it ends up (a removed one: as it was when removed)
	Status_t m_nStatus = Status_t::STATUS_KEPT;
	std::optional<std::size_t> m_Original; // its index in the map; none for added ones
	std::vector<std::string> m_ChangedKeys; // keys replaced or inserted
	std::vector<std::string> m_Rules;       // which rules touched it, in order
	std::vector<std::pair<std::string, std::string>> m_Deleted; // pairs a modify deleted
};

// The map's entities with the configs applied in order, rule by rule as Stripper does: a later
// filter can remove an entity an earlier add made. Removed entities stay in the list, marked.
[[nodiscard]] std::vector<StrippedEntity_t> ApplyStripper( const std::vector<Entity_t> &entities, const std::vector<StripperConfig_t> &configs );

// A map's Stripper configs: the global one, then the map's own, from the nearest addons/stripper
// up from the map (the game folder for maps/, download/maps/ and custom/<pack>/maps/). Only the
// ones that exist.
[[nodiscard]] std::vector<std::filesystem::path> FindStripperConfigs( const std::filesystem::path &map );

// The file a map's own config is in, existing or not: <stripper>/maps/<map>.cfg under the nearest
// addons/stripper up from the map, else under the game folder (the nearest one up holding
// gameinfo.txt). Empty when neither is found.
[[nodiscard]] std::filesystem::path StripperMapConfigPath( const std::filesystem::path &map );

// Config text for one entity, to add to a config. Entities are matched by hammerid when they have
// one (unique in a map), else by classname, targetname and origin.
// filter: that removes it.
[[nodiscard]] std::string StripperFilterFor( const Entity_t &entity );
// modify: matching it, with its single keys in replace: to change (outputs and repeated keys are
// left out: replace would set every one of them to the same value).
[[nodiscard]] std::string StripperModifyFor( const Entity_t &entity );
// add: making a copy of it.
[[nodiscard]] std::string StripperAddFor( const Entity_t &entity );

} // namespace xv::bsp
