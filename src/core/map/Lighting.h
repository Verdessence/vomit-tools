//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The lighting preview: the map's light entities read as vrad reads them, and their
//          light worked out on every luxel of the faces (shadows traced through the solids,
//          the sky's sun and ambient through its sky faces, then light bounced between faces).
//          The result is a lightmap atlas both renderers sample, so the graphics card and the
//          software views show the same light.
//
//===========================================================================//
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <span>
#include <string_view>
#include <vector>

#include "map/Geometry.h"
#include "map/Map.h"
#include "map/Materials.h"
#include "map/Mesh.h"
#include "map/Scene.h"
#include "util/Vec3.h"

namespace xv::map
{

enum class LightType_t
{
	LIGHT_TYPE_POINT,       // light
	LIGHT_TYPE_SPOT,        // light_spot
	LIGHT_TYPE_SUN,         // light_environment's direct light, through the sky faces
	LIGHT_TYPE_SKY_AMBIENT, // light_environment's ambient, from every sky face in view
	LIGHT_TYPE_SURFACE,     // a texture light's piece of a face (lights.rad): out of the face only
};

// A light as vrad works with it. Intensities are linear, in vrad's units (255 lights a face
// facing the light fully).
struct Light_t
{
	LightType_t m_nType = LightType_t::LIGHT_TYPE_POINT;
	Point_t m_Origin{};
	Point_t m_Direction{ 0, 0, -1 }; // the way the light travels (spots and the sun)
	CVec3 m_vecIntensity;
	// Falloff: the light is divided by constant + linear d + quadratic d².
	double m_flConstant = 0.0;
	double m_flLinear = 0.0;
	double m_flQuadratic = 1.0;
	double m_flCapDistance = 1.0e22; // the falloff stops changing past here
	double m_flFadeStart = 0.0;      // faded to nothing between these (when end > start)
	double m_flFadeEnd = -1.0;
	// Spots: cosines of the inner and outer cones, and the focus between them.
	double m_flInnerCos = 1.0;
	double m_flOuterCos = 1.0;
	double m_flExponent = 0.0;
	double m_flSunSpread = 0.0;      // the sun's size: the sine of SunSpreadAngle
	double m_flReach = 1.0e22;       // past here it adds nothing worth drawing
	std::uint32_t m_nEntity = 0;
	// Named or styled (vrad puts its light on a page of its own): it doesn't bounce, and props'
	// light is baked with it off.
	bool m_bNamed = false;
	std::uint32_t m_nFace = ~0u;     // a surface light's face in the bake, which it doesn't light
};

// The lights of the map's entities that vrad bakes: light, light_spot (and light_glspot) and the
// first light_environment (its sun and its ambient). Named lights that start dark give none.
// bHdr reads the _lightHDR values where they are set, as an HDR compile does.
[[nodiscard]] std::vector<Light_t> ReadLights( const CMap &map, bool bHdr = false );
// A light's "_light"-style value: "r g b brightness" (or "r g b", or one value), gamma 2.2 made
// linear and scaled by the brightness, as vrad reads it; nullopt when it isn't one.
[[nodiscard]] std::optional<CVec3> ParseLightValue( std::string_view szValue );
// The falloff that makes a light half as bright at d50 and 1/256 as bright at d0, vrad's
// _fifty_percent_distance and _zero_percent_distance: constant, linear and quadratic.
[[nodiscard]] std::array<double, 3> FalloffForDistances( double d50, double d0 );
// How much of a light reaches flDistance (1 at the light for 50%/0% lights), fade included.
[[nodiscard]] double LightFalloff( const Light_t &light, double flDistance );
// A point and spot light's share for a point facing normal, before shadows: intensity, falloff,
// the spot's cone and the angle it lands at.
[[nodiscard]] CVec3 PointLightAt( const Light_t &light, Point_t point, Point_t normal );

// A texture light, from a RAD file (lights.rad, <map>.rad): what a face with this material gives
// off, as a "_light" value (LDR and, when given, HDR).
struct TexLight_t
{
	CVec3 m_vecLdr;
	std::optional<CVec3> m_Hdr;
};

// The RAD file rules the bake uses: texture lights and the materials that cast no shadow
// ("noshadow"), by material name in lower case. Later files' rules replace earlier ones.
struct RadRules_t
{
	std::map<std::string, TexLight_t, std::less<>> m_TexLights;
	std::set<std::string, std::less<>> m_NoShadow;
};

// Reads a RAD file's rules into rules: "material r g b brightness [hdr r g b brightness]",
// "noshadow material", "ldr:" and "hdr:" prefixes (a line with both is ignored), and VRAD++'s
// "//" comments.
void ParseRadFile( std::string_view szText, RadRules_t &rules );

// env_fog_controller's fog as the game draws it.
struct Fog_t
{
	CVec3 m_vecColor{ 1, 1, 1 };   // 0-1, as written (gamma)
	CVec3 m_vecColor2{ 1, 1, 1 };  // blended to by the view's direction (fogblend)
	Point_t m_Direction{ 1, 0, 0 };
	bool m_bBlend = false;
	double m_flStart = 0.0;
	double m_flEnd = 1.0;
	double m_flMaxDensity = 1.0;
	bool m_bRadial = false;        // by the distance from the eye, else along the view
};

// The first env_fog_controller that starts with fog on.
[[nodiscard]] std::optional<Fog_t> ReadFog( const CMap &map );

// What lighting needs of a material.
struct LightMaterial_t
{
	bool m_bLit = true;     // gets a lightmap (not a tool texture, the sky, or unlit)
	bool m_bBlocks = true;  // stops light (opaque and solid, as vrad's MASK_OPAQUE)
	bool m_bSky = false;    // the sun and the sky's ambient come through it
	CVec3 m_vecReflectivity{ 0.5f, 0.5f, 0.5f }; // how much light it bounces (the VTF's)
	bool m_bKnown = false;  // from the material's VMT, not guessed from its name
	std::array<int, 2> m_TextureSize{ 0, 0 }; // the base texture's, for texture lights' brightness
};

[[nodiscard]] LightMaterial_t DescribeLightMaterial( const MaterialInfo_t &info, std::array<float, 3> reflectivity, std::array<int, 2> textureSize = {} );
// For a material not read (yet, or without a game): from its name, Valve's tool textures known.
[[nodiscard]] LightMaterial_t GuessLightMaterial( std::string_view szName );

class CLighting;

struct LightingOptions_t
{
	bool m_bHdr = false;
	int m_nBounces = 3;           // passes of light bounced between faces
	bool m_bConverge = true;      // the passes not made estimated from the last two (as vrad bounces until it settles)
	int m_nSkySamples = 48;       // rays toward the sky per sample, for its ambient
	int m_nSkySpacing = 2;        // the sky's ambient is gathered every this many atlas texels, then smoothed
	int m_nBounceSamples = 32;    // rays gathering bounced light per sample
	int m_nBounceSpacing = 4;     // and bounced light every this many
	std::size_t m_nMaxLuxels = std::size_t{ 4 } << 20; // more than this: every other luxel, and so on
	// Only what lies within m_flRadius of m_Centre is lit (Hammer++'s Bake in Radius); the rest
	// still casts shadows and is left unlit. 0: everything.
	Point_t m_Centre{};
	double m_flRadius = 0.0;
	// Called now and then from the baking thread: how far along (0-100) and at what.
	std::function<void( int nPercent, std::string_view szStage )> m_pfnProgress;
	const std::atomic<bool> *m_pCancel = nullptr; // set from another thread to stop early
	RadRules_t m_Rad;             // texture lights and noshadow materials
	// Called from the baking thread with the direct light alone, before the bounces, to show
	// something sooner.
	std::function<void( std::shared_ptr<const CLighting> )> m_pfnPartial;
};

// Where a face's luxels are in the atlas. Luxel (u, v) of the face is atlas texel
// (m_nX + (u - m_flMinU) / step, m_nY + (v - m_flMinV) / step).
struct LightmapRect_t
{
	int m_nX = 0;
	int m_nY = 0;
	int m_nWidth = 0;
	int m_nHeight = 0;
	float m_flMinU = 0.0f;
	float m_flMinV = 0.0f;
	float m_flMaxU = 0.0f; // the face's luxel extent as it was baked
	float m_flMaxV = 0.0f;
	CVec3 m_vecFirst;      // its first corner as it was baked, to tell a face that moved
};

// Per vertex, how the renderers light it: [3] says how, and the rest what with.
inline constexpr float LIGHT_KIND_NONE = 0.0f;       // not lit yet: the shaded look
inline constexpr float LIGHT_KIND_LIGHTMAP = 1.0f;   // [0], [1]: where its light is in the atlas, in texels
inline constexpr float LIGHT_KIND_VERTEX = 2.0f;     // [0]-[2]: the light itself (linear, 1 fully lit)
inline constexpr float LIGHT_KIND_FULLBRIGHT = 3.0f; // drawn as it is: tool textures, as in Hammer++
using LightVertex_t = std::array<float, 4>;
inline constexpr LightVertex_t LIGHT_VERTEX_NONE{ 0.0f, 0.0f, 0.0f, LIGHT_KIND_NONE };

// What else a bake lights beside the faces.
struct LightingExtras_t
{
	std::vector<std::shared_ptr<const CMapMesh>> m_Props;    // models placed in the world (map/Props.h); prop_static's cast shadows
	std::vector<std::shared_ptr<const CMapMesh>> m_Overlays; // overlays and decals on the faces (map/Overlay.h)
	std::vector<std::pair<std::uint32_t, Point_t>> m_Points; // objects lit as a whole, at a point (sprites)
	bool m_bPropShadows = true;
};

// A finished bake. Texels are linear light over 255 (1: a fully lit face), as half floats in RGBA
// order, so the graphics card takes them as they are.
class CLighting
{
public:
	int m_nWidth = 0;
	int m_nHeight = 0;
	int m_nStep = 1;                         // luxels per atlas texel, each way
	std::vector<std::uint16_t> m_Texels;     // RGBA half floats, row by row
	std::map<FaceRef_t, LightmapRect_t> m_Faces; // the map's solids' sides
	std::vector<LightmapRect_t> m_InstanceFaces; // the instance mesh's faces, by index
	std::shared_ptr<const CMapMesh> m_pInstanceMesh;
	std::vector<bool> m_Unlit;               // by the scene's materials: no lightmap by design (drawn as they are)
	// The extras' vertices (props, overlays), by mesh; the meshes are held so their addresses stay theirs.
	std::map<const CMapMesh *, std::shared_ptr<const std::vector<LightVertex_t>>> m_Extras;
	std::vector<std::shared_ptr<const CMapMesh>> m_ExtraMeshes;
	std::map<std::uint32_t, std::array<float, 3>> m_Points; // the extras' points, by object
	std::size_t m_nLights = 0;
	std::uint64_t m_nRevision = 0;           // the scene's, when it was baked
	float m_flMeanLight = 0.0f;              // the luxels' mean brightness (linear, 1 fully lit), for HDR's auto exposure

	// The light a texel holds, linear (1: fully lit), RGB.
	[[nodiscard]] std::array<float, 3> GetTexel( int x, int y ) const;
	// Bilinear between texels at a position in texels (centres at .5), clamped at the edges.
	[[nodiscard]] std::array<float, 3> Sample( float x, float y ) const;
	// What each vertex of a mesh of the scene (or one of the extras) is lit with. Faces that
	// changed since the bake (moved, or their luxels reach past what was baked) get none.
	[[nodiscard]] std::vector<LightVertex_t> MeshVertices( const CScene &scene, const CMapMesh &mesh ) const;
};

[[nodiscard]] std::shared_ptr<const CLighting> BakeLighting( const CScene &scene, std::span<const LightMaterial_t> materials, const LightingOptions_t &options = {}, const LightingExtras_t &extras = {} );

// Half floats, as the atlas stores them.
[[nodiscard]] std::uint16_t FloatToHalf( float fl );
[[nodiscard]] float HalfToFloat( std::uint16_t nHalf );

} // namespace xv::map
