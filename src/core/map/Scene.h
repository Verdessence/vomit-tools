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

class CScene
{
public:
	CMap m_Map;
	std::shared_ptr<const CFgd> m_pFgd;      // null without game data
	std::vector<SolidGeometry_t> m_Solids;   // lines up with m_Map.m_Solids
	std::vector<EntityVisual_t> m_Entities;  // lines up with m_Map.m_Entities
	CMapMesh m_Mesh;
	Box_t m_Bounds;                          // everything drawn
	AutoVisgroup_t m_AutoVisgroups;         // the Filter Control's Auto tab
	// What the meshes' material indices name: the map's own materials, then those only its
	// instances use.
	std::vector<std::string> m_Materials;
	std::vector<PlacedInstance_t> m_Instances;
	CMapMesh m_InstanceMesh;                 // every instance's solids, placed, as its func_instance
	std::optional<Skybox3D_t> m_Skybox3D;   // the room around the sky_camera, if there is one
	// How far the map may reach on each axis: the FGD's @mapsize (Source's 16384 without one).
	double m_flWorldLimit = 16384.0;

	[[nodiscard]] std::uint32_t GetObjectCount() const { return static_cast<std::uint32_t>( m_Solids.size() + m_Entities.size() ); }
	[[nodiscard]] std::uint32_t EntityObject( std::size_t nEntity ) const { return static_cast<std::uint32_t>( m_Solids.size() + nEntity ); }
	[[nodiscard]] bool IsSolidObject( std::uint32_t nObject ) const { return nObject < m_Solids.size(); }
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
