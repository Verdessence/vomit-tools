//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Changing a map. An edit gives solids and entities as they are to be, and ApplyEdit
//          makes the scene after it as a new snapshot that shares everything else with the one
//          before. Only what the edit touched is built again: those solids' shapes (drawn from a
//          mesh of their own, the edit mesh, and left out of the mesh as read), those entities'
//          looks, and what is worked out over the whole map (bounds, automatic visgroups).
//
//          Objects are never taken out of the map: deleting marks one deleted, so object numbers,
//          and indices into the map's solids and entities, mean the same thing for its lifetime.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "map/Instance.h"
#include "map/Map.h"
#include "map/Scene.h"

namespace xv::map
{

struct MapEdit_t
{
	// Solids and entities as they are to be, by their index in the map; an index past the last
	// adds one (they must follow on). A side whose material index is NEW_MATERIAL is looked up (or
	// added) by its spelling, m_szMaterial.
	std::vector<std::pair<std::uint32_t, Solid_t>> m_Solids;
	std::vector<std::pair<std::uint32_t, Entity_t>> m_Entities;

	[[nodiscard]] bool IsEmpty() const { return m_Solids.empty() && m_Entities.empty(); }
};

inline constexpr std::uint32_t NEW_MATERIAL = ~0u;

// The scene after edit, sharing what it didn't touch with scene. Its revision is one more, and
// m_Changed lists the objects the edit changed or added.
[[nodiscard]] std::shared_ptr<CScene> ApplyEdit( const CScene &scene, const MapEdit_t &edit );
// The edit that puts back what edit changes (for undo): the objects as they are in scene, and
// those it adds, deleted.
[[nodiscard]] MapEdit_t InverseOf( const CScene &scene, const MapEdit_t &edit );

// A solid or entity moved, turned or scaled by transform. With texture lock the texture moves with
// the faces, as Hammer's Texture Lock does; without it stays put in the world. A transform that
// mirrors turns the planes' points round, so the faces still face out. The result is marked
// changed.
[[nodiscard]] Solid_t TransformSolid( const Solid_t &solid, const Transform_t &transform, bool bTextureLock );
// Moves an entity's origin and turns its angles (a point entity; a brush entity's solids are
// transformed on their own).
[[nodiscard]] Entity_t TransformEntity( const CMap &map, const Entity_t &entity, const Transform_t &transform );
// Source's angles (pitch, yaw, roll) of a rotation.
[[nodiscard]] std::array<double, 3> AnglesOf( const Transform_t &transform );

// Sets an entity's key (adding it if it has none), keeping the value in map's store.
void SetKeyValue( const CMap &map, Entity_t &entity, std::string_view szKey, std::string_view szValue );

} // namespace xv::map
