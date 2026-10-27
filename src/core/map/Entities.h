//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: How the editor shows an entity, from its keys and its class in the FGD: where it is,
//          which way it faces, the box or model or sprite drawn for it, and its colour.
//
//===========================================================================//
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "map/Fgd.h"
#include "map/Geometry.h"
#include "map/Map.h"

namespace xv::map
{

// Hammer's colour for entities whose class gives none.
inline constexpr Color_t ENTITY_COLOR{ 220, 30, 220 };

struct EntityVisual_t
{
	const FgdClass_t *m_pClass = nullptr; // nullptr when the FGD doesn't know the class
	bool m_bPoint = true;                 // stands at its origin (no solids)
	Point_t m_Origin{};
	std::array<double, 3> m_Angles{};     // pitch, yaw, roll in degrees
	Box_t m_Box;                          // around the origin: the class's size(), or 16 units
	Color_t m_Color = ENTITY_COLOR;
	bool m_bClassColor = false;           // the colour came from the class

	std::string m_szModel;                // "models/x.mdl" to draw instead of the box
	bool m_bModelBox = false;             // studioprop: the box comes from the model
	bool m_bInvertPitch = false;          // lightprop
	int m_nSkin = 0;
	double m_flModelScale = 1.0;

	std::string m_szSprite;               // a material to draw facing the camera
	double m_flSpriteScale = 0.25;
	Color_t m_SpriteTint{ 255, 255, 255 };

	bool m_bDecal = false;                // decal(): the "texture" key projected on the faces near it
	bool m_bOverlay = false;              // overlay(): an info_overlay
	bool m_bInstance = false;             // instance(): a func_instance
	bool m_bSkyCamera = false;            // Hammer++'s skycamera(): sky_camera
	std::vector<double> m_Spheres;        // sphere() radii, shown on the selected entity
	std::string_view m_szName;            // targetname, for labels and lines to it
};

// pFgd may be nullptr (no game data): every entity is then a magenta box.
[[nodiscard]] EntityVisual_t DescribeEntity( const Entity_t &entity, const CFgd *pFgd );

// "p y r" or "x y z"; nullopt unless three numbers.
[[nodiscard]] std::optional<std::array<double, 3>> ParseTriple( std::string_view szText );

} // namespace xv::map
