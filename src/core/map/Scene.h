//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Everything a map's views draw from, built once from the file: the map itself, its
//          solids' faces, how each entity looks, and the mesh. Views number what can be picked
//          as objects: the solids first, then the entities.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include "map/Brushes.h"
#include "map/Entities.h"
#include "map/Fgd.h"
#include "map/Instance.h"
#include "map/Map.h"
#include "map/Mesh.h"
#include "map/Skybox3D.h"
#include "map/Visgroups.h"

namespace xv::map
{

struct OverlaySurface_t;

// A side of a solid, by their indices in the map.
struct FaceRef_t
{
	std::uint32_t m_nSolid = 0;
	std::uint32_t m_nSide = 0;
	friend bool operator==( const FaceRef_t &, const FaceRef_t & ) = default;
	friend auto operator<=>( const FaceRef_t &, const FaceRef_t & ) = default;
};

// What an object number stands for: a solid or an entity of the map, by its index.
struct ObjectRef_t
{
	bool m_bSolid = true;
	std::uint32_t m_nIndex = 0;
};

// A copy is a snapshot: copies share everything until one changes it (see CSharedVector), so an
// edit makes a new scene for little, and threads go on reading the one they were given.
//
// Views number what can be picked as objects. As read, the solids come first, then the entities;
// what edits add is numbered after everything there was, and what they delete keeps its number
// (the object is marked deleted), so a number means the same object for the map's lifetime.
class CScene
{
public:
	CMap m_Map;
	std::shared_ptr<const CFgd> m_pFgd;      // null without game data
	CSharedVector<SolidGeometry_t> m_Solids; // lines up with m_Map.m_Solids
	CSharedVector<EntityVisual_t> m_Entities; // lines up with m_Map.m_Entities
	std::shared_ptr<const CMapMesh> m_pMesh = std::make_shared<const CMapMesh>(); // every solid as read
	Box_t m_Bounds;                          // everything drawn
	AutoVisgroup_t m_AutoVisgroups;         // the Filter Control's Auto tab
	// What the meshes' material indices name: the map's own materials, then those only its
	// instances use.
	std::vector<std::string> m_Materials;
	std::vector<PlacedInstance_t> m_Instances;
	InstanceOptions_t m_InstanceOptions; // retained for edits and child-file refreshes
	std::shared_ptr<const CMapMesh> m_pInstanceMesh = std::make_shared<const CMapMesh>(); // every instance's solids, placed, as its func_instance
	std::vector<std::string> m_Problems;    // with its instances, beside the mesh's own
	std::optional<Skybox3D_t> m_Skybox3D;   // the room around the sky_camera, if there is one
	// How far the map may reach on each axis: the FGD's @mapsize (Source's 16384 without one).
	double m_flWorldLimit = 16384.0;
	std::vector<ObjectRef_t> m_Objects;         // by object number
	// Edits (map/Edit.h): the solids drawn from the edit mesh rather than the mesh as read (sorted
	// solid indices), how many edits made this snapshot, and the objects the last one changed.
	std::shared_ptr<const CMapMesh> m_pEditMesh = std::make_shared<const CMapMesh>();
	// Painted solids retain independent meshes across strokes; the bulk edit mesh stays uploaded.
	std::vector<std::pair<std::uint32_t, std::shared_ptr<const CMapMesh>>> m_SolidMeshes;
	bool m_bDisplacementEdit = false;
	std::vector<std::uint32_t> m_EditedSolids;
	std::uint64_t m_nRevision = 0;
	std::vector<std::uint32_t> m_Changed;
	// Ids for new objects: Hammer numbers solids and entities (and groups) from one count, sides
	// from another.
	int m_nNextObjectId = 1;
	int m_nNextSideId = 1;
	std::vector<std::uint32_t> m_SolidObjects;  // by solid: its object number
	std::vector<std::uint32_t> m_EntityObjects; // by entity: its object number

	[[nodiscard]] std::optional<FaceRef_t> FindSide( int nId ) const;
	struct SideLookup_t
	{
		std::once_flag m_Once;
		std::once_flag m_OverlayOnce;
		std::shared_ptr<const OverlaySurface_t> m_pOverlaySurface;
		std::map<int, FaceRef_t> m_Faces;
	};
	std::shared_ptr<SideLookup_t> m_pSideFaces = std::make_shared<SideLookup_t>();
	[[nodiscard]] const CMapMesh &GetMesh() const { return *m_pMesh; }
	[[nodiscard]] const CMapMesh &GetInstanceMesh() const { return *m_pInstanceMesh; }
	[[nodiscard]] const CMapMesh &GetEditMesh() const { return *m_pEditMesh; }
	[[nodiscard]] const CMapMesh &GetSolidMesh( std::uint32_t nSolid ) const;
	// Whether a solid is drawn from the edit mesh (so not from the mesh as read).
	[[nodiscard]] bool IsEditedSolid( std::uint32_t nSolid ) const;
	[[nodiscard]] std::uint32_t GetObjectCount() const { return static_cast<std::uint32_t>( m_Objects.size() ); }
	[[nodiscard]] std::uint32_t EntityObject( std::size_t nEntity ) const { return m_EntityObjects[ nEntity ]; }
	[[nodiscard]] std::uint32_t SolidObject( std::size_t nSolid ) const { return m_SolidObjects[ nSolid ]; }
	[[nodiscard]] bool IsSolidObject( std::uint32_t nObject ) const { return nObject < m_Objects.size() && m_Objects[ nObject ].m_bSolid; }
	// The solid or entity an object is (its index in m_Map.m_Solids or m_Map.m_Entities).
	[[nodiscard]] std::uint32_t GetIndex( std::uint32_t nObject ) const { return m_Objects[ nObject ].m_nIndex; }
	// Whether an object was deleted (its number is not used again).
	[[nodiscard]] bool IsDeleted( std::uint32_t nObject ) const;
	// Numbers the solids, then the entities, as a scene is numbered when it is read.
	void NumberObjects();
	// The entity an object is, or owns it (a brush entity's solid); -1 for world solids.
	[[nodiscard]] int GetObjectEntity( std::uint32_t nObject ) const;
	// The editor notes of an object: a solid's own, or its entity's.
	[[nodiscard]] const EditorInfo_t &GetObjectEditor( std::uint32_t nObject ) const;
	// The world box of a point entity as drawn (its class's box around its origin).
	[[nodiscard]] Box_t GetEntityBox( std::size_t nEntity ) const;
	// The map's own id for an object (a solid's or an entity's), for "go to" and messages.
	[[nodiscard]] int GetObjectId( std::uint32_t nObject ) const;
};

struct SceneOptions_t
{
	// Called now and then from the building thread: how far along (0-100) and at what.
	std::function<void( int nPercent, std::string_view szStage )> m_pfnProgress;
	// Set from another thread to stop early; BuildScene then returns nullptr.
	const std::atomic<bool> *m_pCancel = nullptr;
	// Where the map's file is, for its func_instances' files; none are read without it.
	InstanceOptions_t m_Instances;
};

[[nodiscard]] std::unique_ptr<CScene> BuildScene( CMap map, std::shared_ptr<const CFgd> pFgd, const SceneOptions_t &options = {} );

// The FGDs read in order (later classes replacing earlier ones) and finished; null when files
// is empty.
[[nodiscard]] std::shared_ptr<const CFgd> LoadFgds( const std::vector<std::filesystem::path> &files );

} // namespace xv::map
