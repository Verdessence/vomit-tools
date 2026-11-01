//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Entities Hammer shows as a sprite (an FGD's iconsprite() or sprite(), env_sprite):
//          a square of the sprite's material facing the camera, at the entity's origin.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "map/Geometry.h"
#include "map/Mesh.h"

namespace xv::map
{

struct SpritePlacement_t
{
	std::uint32_t m_nObject = 0;
	std::uint32_t m_nMaterial = 0;   // in the map's material table
	Point_t m_Origin{};
	double m_flHalfWidth = 8.0;      // in units: the texture's size times the scale, halved
	double m_flHalfHeight = 8.0;
	std::array<int, 2> m_TextureSize{ 64, 64 };
	std::array<std::uint8_t, 3> m_Tint{ 255, 255, 255 }; // a light's colour; white otherwise
	friend bool operator==( const SpritePlacement_t &, const SpritePlacement_t & ) = default;
};

// Each sprite as a square in the plane of the camera's right and up directions, wound as
// brush faces are (clockwise seen from the camera), with its outline as lines.
[[nodiscard]] CMapMesh BuildSpriteMesh( std::span<const SpritePlacement_t> sprites, Point_t right, Point_t up );

// A particle as the map draws it: its corners in the world (the texture's top-left, top-right,
// bottom-right, bottom-left), the part of its texture, its colour and alpha, its material and that
// texture's size, and the entity it belongs to.
struct ParticleSprite_t
{
	std::array<CVec3, 4> m_Corners{};
	std::array<float, 4> m_Rect{ 0, 0, 1, 1 };
	std::array<float, 4> m_Color{ 1, 1, 1, 1 };
	std::uint32_t m_nMaterial = 0;
	std::array<int, 2> m_TextureSize{ 64, 64 };
	std::uint32_t m_nObject = 0;
};

// The particles in the order given (far to near), a batch each time the material changes. Their
// colour goes in the normal and their alpha in the blend (ShadeMaterial_t::m_bVertexColor):
// particles are never shaded by facing.
[[nodiscard]] CMapMesh BuildParticleMesh( std::span<const ParticleSprite_t> particles );

} // namespace xv::map
