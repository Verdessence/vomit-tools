//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map's props (prop_static and every entity with a model) as the 3D views draw them:
//          each model placed at its entity, turned and scaled, all in one mesh like the map's
//          own, so both renderers, picking and selection treat them as they treat brushes.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "map/Geometry.h"
#include "map/Mesh.h"
#include "model/Model.h"

namespace xv::map
{

class CScene;

// A model as loaded for the map, with what its materials are in the map's material table.
struct PropModel_t
{
	std::shared_ptr<const model::CModel> m_pModel;
	std::vector<std::uint32_t> m_Materials;         // per model material: its index in the table
	std::vector<std::array<int, 2>> m_TextureSizes; // per model material: its base texture's size
};

// Where an entity's model stands.
struct PropPlacement_t
{
	std::uint32_t m_nObject = 0;
	std::uint32_t m_nModel = 0;       // into the models
	Point_t m_Origin{};
	std::array<double, 3> m_Angles{}; // pitch, yaw, roll in degrees
	double m_flScale = 1.0;
};

// A model an entity shows, by its path and skin, for loading each once.
struct PropKey_t
{
	std::string m_szModel; // "models/props/crate.mdl"
	int m_nSkin = 0;

	friend auto operator<=>( const PropKey_t &, const PropKey_t & ) = default;
};

// The models the scene's entities show, each once, and each entity that shows one (its object
// and the key's index).
struct PropList_t
{
	std::vector<PropKey_t> m_Keys;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> m_Entities;
};

[[nodiscard]] PropList_t ListProps( const CScene &scene );

// Every placement's model in world space, batched by material and by chunks of flChunkSize
// (each prop goes in the chunk of its origin). Placements whose model has no triangles are
// left out.
[[nodiscard]] CMapMesh BuildPropMesh( std::span<const PropModel_t> models, std::span<const PropPlacement_t> placements, double flChunkSize = 1024.0 );

} // namespace xv::map
