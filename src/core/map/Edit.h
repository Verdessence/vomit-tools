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

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
	// Transforms of existing solids, also applied to overlays attached to their faces. Geometry
	// replacements without a transform (clip/delete) only remove references to vanished faces.
	std::vector<std::pair<std::uint32_t, Transform_t>> m_SolidTransforms;
	// The world's keys, outputs and editor notes as they are to be (its solids stay as they are).
	std::optional<Entity_t> m_World;
	// Groups to add (Hammer's group blocks in the world), and groups an earlier edit added to take
	// out again by id (undoing it; such groups have no text in the file).
	std::vector<Group_t> m_Groups;
	std::vector<int> m_RemovedGroups;
	// The cordons as they are to be, and whether the cordon is on.
	std::optional<std::pair<std::vector<Cordon_t>, bool>> m_Cordons;
	std::optional<std::pair<std::vector<Camera_t>, int>> m_Cameras;

	[[nodiscard]] bool IsEmpty() const { return m_Solids.empty() && m_Entities.empty() && !m_World && m_Groups.empty() && m_RemovedGroups.empty() && !m_Cordons && !m_Cameras; }
	[[nodiscard]] bool IsCameraEdit() const { return m_Cameras && m_Solids.empty() && m_Entities.empty() && m_SolidTransforms.empty() && !m_World && m_Groups.empty() && m_RemovedGroups.empty() && !m_Cordons; }
};

inline constexpr std::uint32_t NEW_MATERIAL = ~0u;

// The scene after edit, sharing what it didn't touch with scene. Its revision is one more, and
// m_Changed lists the objects the edit changed or added. bDisplacementPreview is only for
// geometry/alpha painting of existing displacements on the stroke's base snapshot;
// it grows bounds conservatively and leaves automatic groups unchanged until commit.
[[nodiscard]] std::shared_ptr<CScene> ApplyEdit( const CScene &scene, const MapEdit_t &edit, bool bDisplacementPreview = false );
// The edit that puts back what edit changes (for undo): the objects as they are in scene, and
// those it adds, deleted.
[[nodiscard]] MapEdit_t InverseOf( const CScene &scene, const MapEdit_t &edit );

// A solid or entity moved, turned or scaled by transform. With texture lock the texture moves with
// the faces, as Hammer's Texture Lock does; without it stays put in the world. With the scale
// lock too, a stretch stretches the texture; without, it keeps its size. A transform that
// mirrors turns the planes' points round, so the faces still face out. The result is marked
// changed.
[[nodiscard]] Solid_t TransformSolid( const Solid_t &solid, const Transform_t &transform, bool bTextureLock, bool bScaleLock = false );
// Moves an entity's origin and turns its angles (a point entity; a brush entity's solids are
// transformed on their own).
[[nodiscard]] Entity_t TransformEntity( const CMap &map, const Entity_t &entity, const Transform_t &transform );
// Source's angles (pitch, yaw, roll) of a rotation.
[[nodiscard]] std::array<double, 3> AnglesOf( const Transform_t &transform );

// The objects a selection acts on: the solids (those of selected entities too, as they move with
// them) and the entities, by index.
struct ObjectSet_t
{
	std::vector<std::uint32_t> m_Solids;
	std::vector<std::uint32_t> m_Entities;
};
[[nodiscard]] ObjectSet_t ObjectsOf( const CScene &scene, const std::vector<std::uint32_t> &objects );

// Deletes the objects (an entity with its solids).
[[nodiscard]] MapEdit_t DeleteObjects( const CScene &scene, const std::vector<std::uint32_t> &objects );
// Moves, turns or scales the objects: solids by TransformSolid, entities by TransformEntity.
[[nodiscard]] MapEdit_t TransformObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, const Transform_t &transform, bool bTextureLock, bool bScaleLock = false );
// Copies of the objects, transformed, with ids of their own; pAdded gets the object numbers they
// will have once the edit is made (the copies of entities first, then of solids).
[[nodiscard]] MapEdit_t CopyObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, const Transform_t &transform, bool bTextureLock, std::vector<std::uint32_t> *pAdded );

// New objects added (their ids given here): solids whose m_nEntity is an index into entities belong
// to that new entity, -1 to the world. Their text must be in the map's store (as MakeBlock and
// MakePointEntity keep it). pAdded as for CopyObjects.
[[nodiscard]] MapEdit_t AddObjects( const CScene &scene, std::vector<Solid_t> solids, std::vector<Entity_t> entities, std::vector<std::uint32_t> *pAdded );

// The objects as VMF text, for the clipboard: the world's solids in a world block, entities with
// theirs. What other editors (and Hammer) read too.
[[nodiscard]] std::string ObjectsToText( const CScene &scene, const std::vector<std::uint32_t> &objects );
// What ObjectsToText wrote (or any VMF's objects), added to the scene moved by transform, with ids
// of their own; pAdded as for CopyObjects. Empty when the text holds nothing.
[[nodiscard]] MapEdit_t ObjectsFromText( const CScene &scene, std::string_view szText, const Transform_t &transform, std::vector<std::uint32_t> *pAdded );

// Hammer's Paste Special: m_nCopies copies of the text's objects, each moved by m_Offset and
// turned by m_Angles (degrees about x, y and z, about the set's own centre) once more than the
// one before. Without m_Start the first copy is one step from where the objects were; with it,
// the first copy's centre is put there and the steps follow. m_bGroup groups each copy;
// m_bUniqueNames gives a copy's entities names the map doesn't have yet (a number appended, or
// a trailing number raised), and its own outputs follow the renames.
// The box round the text's objects (solids' shapes, point entities' origins); empty for none.
[[nodiscard]] Box_t BoundsOfText( std::string_view szText );
struct PasteSpecial_t
{
	int m_nCopies = 1;
	Point_t m_Offset{};
	std::array<double, 3> m_Angles{};
	std::optional<Point_t> m_Start;
	// What the turns are about (Hammer++'s pivot), where the pasted set is now; else its own
	// centre. m_Start moves the point along with the set.
	std::optional<Point_t> m_Pivot;
	bool m_bGroup = false;
	bool m_bUniqueNames = false;
};
[[nodiscard]] MapEdit_t PasteSpecial( const CScene &scene, std::string_view szText, const PasteSpecial_t &how, std::vector<std::uint32_t> *pAdded );

// Tie to Entity: the objects' solids (world solids, and those of selected entities, which are
// emptied) made one new brush entity of szClass, with the keys the game data gives it; pAdded
// gets the new entity's object number.
[[nodiscard]] MapEdit_t TieToEntity( const CScene &scene, const std::vector<std::uint32_t> &objects, std::string_view szClass, std::vector<std::uint32_t> *pAdded );
// Move to World: the selected entities' solids back to the world, the entities deleted.
[[nodiscard]] MapEdit_t MoveToWorld( const CScene &scene, const std::vector<std::uint32_t> &objects );
// Group: a new group round the objects (an entity's solids follow it); Ungroup: the objects'
// outermost groups taken off them.
[[nodiscard]] MapEdit_t GroupObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, int *pGroupId );
[[nodiscard]] MapEdit_t UngroupObjects( const CScene &scene, const std::vector<std::uint32_t> &objects );

// Sets an entity's key (adding it if it has none), keeping the value in map's store.
void SetKeyValue( const CMap &map, Entity_t &entity, std::string_view szKey, std::string_view szValue );
// Takes a key out (it is marked deleted, so the others keep their places in the file).
void RemoveKey( Entity_t &entity, std::string_view szKey );
// One of the spawnflags bits on or off (the key added when there is none; nDefault is what
// the game data ticks by default, which an entity without the key has).
void SetSpawnFlag( const CMap &map, Entity_t &entity, std::uint32_t nBit, bool bOn, std::uint32_t nDefault );
// The entity's outputs replaced, their text kept in map's store.
void SetConnections( const CMap &map, Entity_t &entity, std::vector<Connection_t> connections );
// The editor block's comments.
void SetComments( const CMap &map, Entity_t &entity, std::string_view szComments );
// An edit that changes the selected entities (those of the objects, a brush entity's solids
// standing for it; the world when bWorld) by pfnEdit, each as it is now.
[[nodiscard]] MapEdit_t EditEntities( const CScene &scene, const std::vector<std::uint32_t> &objects, const std::function<void( Entity_t & )> &pfnEdit );
[[nodiscard]] MapEdit_t EditWorld( const CScene &scene, const std::function<void( Entity_t & )> &pfnEdit );

} // namespace xv::map
