//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What a map's view needs to know about a material: the texture to draw, the second
//          texture displacements blend to, how it is see-through, and which compile tool it is
//          (nodraw, clip, trigger, sky...), from its VMT.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "material/Material.h"
#include "util/Vec3.h"

namespace xv::map
{

// The "%compile..." keys tool materials set.
enum MaterialCompile_t : std::uint32_t
{
	MATERIAL_COMPILE_NODRAW = 1u << 0,
	MATERIAL_COMPILE_CLIP = 1u << 1,
	MATERIAL_COMPILE_PLAYERCLIP = 1u << 2,
	MATERIAL_COMPILE_NPCCLIP = 1u << 3,
	MATERIAL_COMPILE_TRIGGER = 1u << 4,
	MATERIAL_COMPILE_HINT = 1u << 5,
	MATERIAL_COMPILE_SKIP = 1u << 6,
	MATERIAL_COMPILE_AREAPORTAL = 1u << 7,
	MATERIAL_COMPILE_SKY = 1u << 8,
	MATERIAL_COMPILE_2DSKY = 1u << 9,
	MATERIAL_COMPILE_ORIGIN = 1u << 10,
	MATERIAL_COMPILE_FOG = 1u << 11,
	MATERIAL_COMPILE_LADDER = 1u << 12,
	MATERIAL_COMPILE_WATER = 1u << 13,
	MATERIAL_COMPILE_INVISIBLE = 1u << 14,
	MATERIAL_COMPILE_OCCLUDER = 1u << 15,
	MATERIAL_COMPILE_DETAIL = 1u << 16,
	MATERIAL_COMPILE_NONSOLID = 1u << 17,
	MATERIAL_COMPILE_BLOCKLIGHT = 1u << 18,
	MATERIAL_COMPILE_NOLIGHT = 1u << 19, // no lightmap: drawn as it is
};

// Tool materials: what Hammer++'s "toggle tool textures" hides.
inline constexpr std::uint32_t MATERIAL_COMPILE_TOOLS = MATERIAL_COMPILE_NODRAW | MATERIAL_COMPILE_CLIP | MATERIAL_COMPILE_PLAYERCLIP | MATERIAL_COMPILE_NPCCLIP | MATERIAL_COMPILE_TRIGGER | MATERIAL_COMPILE_HINT | MATERIAL_COMPILE_SKIP | MATERIAL_COMPILE_AREAPORTAL | MATERIAL_COMPILE_ORIGIN | MATERIAL_COMPILE_INVISIBLE | MATERIAL_COMPILE_OCCLUDER | MATERIAL_COMPILE_BLOCKLIGHT | MATERIAL_COMPILE_FOG;

struct MaterialInfo_t
{
	bool m_bFound = false;            // the VMT was read
	std::string m_szShader;           // lower case, without a _dx9-style suffix
	std::string m_szTexture;          // what the editor draws: $basetexture, else %tooltexture
	std::string m_szTexture2;         // $basetexture2, for blended displacements
	bool m_bBlend = false;            // WorldVertexTransition and its kind: base2 by vertex alpha
	std::uint32_t m_nCompile = 0;     // MaterialCompile_t bits
	bool m_bTranslucent = false;
	bool m_bAdditive = false;
	bool m_bAlphaTest = false;
	float m_flAlphaReference = 0.7f;
	float m_flAlpha = 1.0f;
	CVec3 m_vecColor{ 1, 1, 1 };
	bool m_bNoCull = false;
	float m_flDecalScale = 1.0f;
	std::optional<CVec3> m_Reflectivity; // $reflectivity: what it bounces, over its texture's
	bool m_bSelfIllum = false;         // $selfillum: glows by its base texture's alpha
	CVec3 m_vecSelfIllumTint{ 1, 1, 1 };

	[[nodiscard]] bool IsTool() const { return ( m_nCompile & MATERIAL_COMPILE_TOOLS ) != 0; }
	[[nodiscard]] bool IsSky() const { return ( m_nCompile & ( MATERIAL_COMPILE_SKY | MATERIAL_COMPILE_2DSKY ) ) != 0; }
	[[nodiscard]] bool IsSeeThrough() const { return m_bTranslucent || m_bAdditive || m_flAlpha < 1.0f; }
};

// szMaterial is a map's name for it ("brick/brickwall001a", any case).
[[nodiscard]] MaterialInfo_t ReadMaterialInfo( std::string_view szMaterial, const material::Reader_t &pfnRead );
[[nodiscard]] MaterialInfo_t DescribeMaterial( const material::CMaterial &material );

} // namespace xv::map
