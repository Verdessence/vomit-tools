//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A compiled map drawn as the game draws it, over the scene read back from it
//          (bsp/Decompile.h): its own faces (bsp/Faces.h) in place of the scene's brushes, lit by
//          its own lightmaps, the scene's props and overlays lit from its ambient cubes. The
//          map viewer's .bsp tabs and the demo viewer both draw a map this way; the scene
//          stays what is picked, listed and shown in the 2D views.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "bsp/Faces.h"
#include "map/MapRenderData.h"

class CMapCompiled
{
public:
	explicit CMapCompiled( std::shared_ptr<const xv::bsp::CompiledMap_t> pCompiled );
	~CMapCompiled();

	// What goes after the decompiled map's text, with ids from nFirstId up: a brush entity of
	// small boxes, six materials each, so every material the faces use is in the scene's table
	// (the decompile may not write them all), whose own object (which draws nothing) is the
	// one the faces are drawn as. Hidden, it shows in no view.
	[[nodiscard]] static std::string MapText( const xv::bsp::CompiledMap_t &compiled, int nFirstId = FIRST_ID );
	// Past any id a map file holds.
	static constexpr int FIRST_ID = 950000000;

	// The scene read from the text with MapText's after it is in: the faces put on its
	// materials and that entity's object, and its solids to hide.
	void Build( const xv::map::CScene &scene );
	[[nodiscard]] bool IsBuilt() const { return m_bBuilt; }

	[[nodiscard]] const xv::bsp::CompiledMap_t &GetMap() const { return *m_pCompiled; }
	[[nodiscard]] const std::shared_ptr<const xv::bsp::CompiledMap_t> &GetSharedMap() const { return m_pCompiled; }
	// Model n's faces on the scene's materials; null for one with none.
	[[nodiscard]] const std::shared_ptr<const xv::map::CMapMesh> &GetModelMesh( std::size_t nModel ) const;
	[[nodiscard]] std::size_t GetModelCount() const { return m_Meshes.size(); }
	// The baked light on the world's faces, which also finds where a ray meets them; null
	// before Build.
	[[nodiscard]] const xv::bsp::CSurfaceLight *GetSurfaceLight() const { return m_pSurfaceLight.get(); }
	// The object everything drawn as compiled is: always shown.
	[[nodiscard]] std::uint32_t GetObject() const { return m_nObject; }

	// A placed mesh and its light, drawn after the world.
	struct Drawn_t
	{
		std::shared_ptr<const xv::map::CMapMesh> m_pMesh;
		std::shared_ptr<const std::vector<xv::map::LightVertex_t>> m_pLights;
	};
	// The map's brush entities where its entity lump puts them (made once).
	[[nodiscard]] const std::vector<Drawn_t> &GetLumpPlacements();
	// The light a model gets where it stands, from the bounds of what it draws: the cube at
	// their middle and the light baked on the surface under it (worked out once for a model
	// that changes its pose but not its place).
	struct ModelLight_t
	{
		std::optional<xv::bsp::CAmbientLighting::Cube_t> m_Cube;
		std::optional<xv::CVec3> m_vecBelow;
	};
	[[nodiscard]] ModelLight_t ModelLightAt( const xv::map::Box_t &bounds ) const;
	// A mesh's vertices lit by that light, each by its normal; null when there is none.
	[[nodiscard]] static std::shared_ptr<const std::vector<xv::map::LightVertex_t>> LightModel( const xv::map::CMapMesh &mesh, const ModelLight_t &light );
	// A model placed somewhere: lit where it stands from the cubes and the surface under it.
	[[nodiscard]] std::shared_ptr<const std::vector<xv::map::LightVertex_t>> LightPlaced( const xv::map::CMapMesh &mesh ) const;

	// The frame drawn as compiled: the world and the placed meshes (in the entity parts' slot,
	// which a compiled frame has no other use for), the scene's solids and the objects in
	// hidden (sorted) not shown, its props and overlays lit from the cubes. Textured looks are
	// lit by the compile's light (the frame's exposure is left to the caller); the others
	// show the faces as they are.
	void Apply( MapFrame_t &frame, const std::vector<Drawn_t> &placed, const std::vector<std::uint32_t> &hidden = {} );
	// The world drawn as seen from these clusters only (bsp::CVisibility: the camera's, and
	// the 3D skybox camera's): the faces none of them may see are left out, as the game does.
	// Empty, or a map not vised: all of it.
	void SetClusters( std::vector<int> clusters );
	// The world's triangles drawn last Apply (for tests: fewer from a closed room).
	[[nodiscard]] std::size_t GetWorldTriangles() const { return m_nWorldTriangles; }

private:
	[[nodiscard]] std::shared_ptr<const std::vector<xv::map::LightVertex_t>> CubeLights( const std::shared_ptr<const xv::map::CMapMesh> &pMesh, int nWay ); // a CubeLight_t

	std::shared_ptr<const xv::bsp::CompiledMap_t> m_pCompiled;
	std::vector<std::shared_ptr<const xv::map::CMapMesh>> m_Meshes;
	std::uint32_t m_nObject = 0;
	std::vector<std::uint32_t> m_Hidden; // sorted
	bool m_bBuilt = false;
	std::unique_ptr<const xv::bsp::CSurfaceLight> m_pSurfaceLight; // the world's baked light, looked up at a point
	std::vector<Drawn_t> m_LumpPlacements;
	bool m_bLumpPlaced = false;
	// The scene's props' and overlays' light from the cubes, by mesh (held, so its address
	// stays its own).
	std::map<const xv::map::CMapMesh *, Drawn_t> m_CubeLit;
	// The world as some clusters see it: the last few kept (each is a copy of the world's
	// vertices, so the card's upload of it is kept too while it is drawn again).
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> VisibleWorld();
	std::vector<int> m_Clusters; // sorted
	std::vector<std::pair<std::vector<int>, std::shared_ptr<const xv::map::CMapMesh>>> m_VisibleWorlds; // newest last
	std::size_t m_nWorldTriangles = 0;
};
