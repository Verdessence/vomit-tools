//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The colour of a point on a map's face in each of the 3D view's modes. The software
//          renderer calls this for every pixel; the graphics card's shader (map.frag) does the
//          same steps in the same order. Change the two together.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sky/Sky.h"
#include "texture/Image.h"
#include "texture/Vtf.h"
#include "util/Vec3.h"

namespace xv::map
{

enum class RasterMode_t
{
	RASTER_MODE_WIREFRAME,
	RASTER_MODE_FLAT,
	RASTER_MODE_TEXTURED,
	RASTER_MODE_TEXTURED_SHADED,
	RASTER_MODE_LIGHTMAP_GRID,
	RASTER_MODE_LIGHTING,       // textured, lit by the lighting preview (map/Lighting.h)
};

class CLighting;
struct Fog_t;

// A texture as both renderers get it: its mipmaps, largest first, and the full size the map's
// texture coordinates are measured against (the VTF's own, before any was dropped).
// One of a texture's mips as the VTF stores it (DXT), for a graphics card to take as it is.
struct CompressedMip_t
{
	int m_nWidth = 0;
	int m_nHeight = 0;
	std::vector<std::byte> m_Data;
};

// A texture for drawing. The card draws from the VTF's own DXT mips; software from the decoded
// ones, which are only made when something draws in software (they are 4 to 8 times bigger).
struct RasterTexture_t
{
	std::vector<texture::CImage> m_Mips;          // decoded, from the largest kept down
	std::vector<CompressedMip_t> m_Compressed;    // the same mips as the VTF has them, when DXT
	texture::VtfFormat_t m_nCompressedFormat = texture::VtfFormat_t::VTF_FORMAT_NONE;
	int m_nWidth = 0;                             // the VTF's own size (the mips kept may start below it)
	int m_nHeight = 0;
	bool m_bClampS = false;
	bool m_bClampT = false;
	std::array<float, 3> m_Reflectivity{};       // the VTF's, for the light faces bounce

	// The largest mip kept, from whichever set there is.
	[[nodiscard]] int GetTopWidth() const { return !m_Mips.empty() ? m_Mips.front().m_nWidth : !m_Compressed.empty() ? m_Compressed.front().m_nWidth
																													 : 0; }
	[[nodiscard]] int GetTopHeight() const { return !m_Mips.empty() ? m_Mips.front().m_nHeight : !m_Compressed.empty() ? m_Compressed.front().m_nHeight
																													   : 0; }
	[[nodiscard]] std::size_t GetMipCount() const { return !m_Mips.empty() ? m_Mips.size() : m_Compressed.size(); }
};

// Whether a texture's mips run from its top level down to 1 by 1. Those that stop short (NOMIP
// VTFs keep only the top) are drawn from their top level alone, as Source draws NOMIP textures.
[[nodiscard]] bool HasMipChain( const RasterTexture_t &texture );

// What shading needs of a material.
struct ShadeMaterial_t
{
	const RasterTexture_t *m_pBase = nullptr;  // null: drawn white (or the missing checker)
	const RasterTexture_t *m_pBase2 = nullptr;
	bool m_bMissing = false;                   // the VMT wasn't found: Source's checker
	bool m_bSky = false;                       // toolsskybox and its kind: the map's sky shows
	bool m_bTinted = false;                    // sprites: times the object's colour (a light's)
	bool m_bUnlit = false;                     // sprites: never shaded by facing
	bool m_bVertexColor = false;               // particles: times their vertices' colour (carried in the normal) and alpha (in the blend)
	// Particles, as the particle editor draws them: the colour weighted by its alpha before it is
	// laid over or added, so an overbright past 1 still brightens; with m_bAddOver
	// (SpriteCard's $addoverblend) weighted by the vertex's alpha alone.
	bool m_bPremultiplied = false;
	bool m_bAddOver = false;
	float m_flAddSelf = 0.0f;                  // SpriteCard's $addself: the texture added on top of blending it
	bool m_bBlend = false;
	bool m_bAlphaTest = false;
	bool m_bTranslucent = false;
	bool m_bAdditive = false;
	float m_flAlphaReference = 0.7f;
	float m_flAlpha = 1.0f;
	CVec3 m_vecColor{ 1, 1, 1 };
	bool m_bSelfIllum = false;                 // glows where its base texture's alpha says, in the lighting preview
	CVec3 m_vecSelfIllumTint{ 1, 1, 1 };
	// Refract and Water: drawn by ShadeRefract over what is behind; m_pBase is the normal map,
	// m_pBase2 the tint texture ($refracttinttexture).
	bool m_bRefract = false;
	CVec3 m_vecRefractTint{ 1, 1, 1 };
	bool m_bEnvMap = false;                    // reflects (water always)
	bool m_bReflectsSky = false;               // the reflection shows the sky (water without $envmap), not the cube map
	CVec3 m_vecEnvTint{ 1, 1, 1 };
	int m_nWidth = 128;                        // what texels divide by when there's no texture
	int m_nHeight = 128;
	// $basetexturetransform (a proxy's TextureScroll, often): u' = a*u + b*v + tx, v' = c*u + d*v + ty,
	// in the base texture's 0-1 coordinates.
	std::array<float, 6> m_UvTransform{ 1, 0, 0, 1, 0, 0 };
	// $detail: combined with the base by m_nDetailMode ($detailblendmode), m_flDetailBlend of it.
	// Its coordinates are the base's 0-1 ones (before the base's transform) through
	// m_DetailTransform, times m_DetailScale.
	const RasterTexture_t *m_pDetail = nullptr;
	int m_nDetailMode = 0;
	float m_flDetailBlend = 1.0f;
	std::array<float, 2> m_DetailScale{ 4.0f, 4.0f };
	std::array<float, 6> m_DetailTransform{ 1, 0, 0, 1, 0, 0 };
	friend bool operator==( const ShadeMaterial_t &, const ShadeMaterial_t & ) = default;

	[[nodiscard]] bool HasUvTransform() const { return m_UvTransform != std::array<float, 6>{ 1, 0, 0, 1, 0, 0 }; }
};

struct ShadeInput_t
{
	std::array<float, 2> m_Texel{};            // in texels, as the mesh stores it
	std::array<float, 2> m_Luxel{};            // in luxels
	float m_flBlend = 0.0f;                    // displacement alpha, 0-1
	CVec3 m_vecNormal{ 0, 0, 1 };
	CVec3 m_vecVertexColor{ 1, 1, 1 };         // the normal as interpolated, not made unit length: particles' colour
	float m_flLod = 0.0f;                      // mip level, fractional
	std::array<float, 4> m_Gradient{};         // texels the point moves a pixel right (u, v) and down (u, v)
	CVec3 m_vecObjectColor{ 1, 1, 1 };         // the flat colour (Hammer's editor colour), 0-1
	CVec3 m_vecDirection{ 1, 0, 0 };           // from the eye to the point (for the sky)
	bool m_bSelected = false;
	bool m_bInstance = false;                  // a func_instance's: tinted
	bool m_bInvalid = false;                   // a broken solid's: tinted
	std::array<float, 4> m_Light{};            // how the lighting preview lights it (map/Lighting.h's LightVertex_t)
};

struct ShadeOptions_t
{
	RasterMode_t m_nMode = RasterMode_t::RASTER_MODE_TEXTURED;
	CVec3 m_vecSelection{ 220.0f / 255.0f, 0.0f, 0.0f };
	float m_flSelectionTint = 0.5f;
	CVec3 m_vecInstance{ 1.0f, 140.0f / 255.0f, 0.0f }; // the Func_instance article's orange tint
	float m_flInstanceTint = 0.35f;
	CVec3 m_vecInvalid{ 1.0f, 0.0f, 0.0f };    // broken solids' faces go this far toward red (Hammer++)
	float m_flInvalidTint = 0.5f;
	bool m_bMipBlend = true;                   // trilinear: between mips; else the nearest mip (bilinear)
	int m_nAnisotropy = 1;                     // samples along a pixel's footprint at most (anisotropic filtering)
	CVec3 m_vecFaceShade{ 0.65f, 0.8f, 1.0f }; // brightness of faces facing along x, y and z
	float m_flLuxelShade = 0.75f;              // the lightmap grid's darker squares
	const sky::CCube *m_pSky = nullptr;        // Hammer++'s 2D skybox on sky faces; null: none
	// What stands in for env_cubemap in reflections: the game's default cube map, as a map shows
	// before buildcubemaps. Null: the sky does.
	const sky::CCube *m_pEnvMap = nullptr;
	const CLighting *m_pLighting = nullptr;    // the lighting preview's atlas; null: not baked yet
	float m_flLightScale = 1.0f;               // the lighting preview's brightness (times HDR's exposure)
	bool m_bLightHdr = false;                  // shown as the game with HDR: no overbright
	const Fog_t *m_pFog = nullptr;             // the map's fog, in the textured views; null: none
	bool m_bLightOnly = false;                 // the lighting preview's light alone (mat_fullbright 2): textures a plain grey
	CVec3 m_vecViewForward{ 1, 0, 0 };         // for fog measured along the view
	float m_flMonitorGamma = 2.2f;             // the lighting preview's mat_monitorgamma (the game's brightness setting): 2.2 changes nothing
};

// With HDR the game draws a lightmap linear, times a scale its shaders take from the engine
// (GetLightMapScaleFactor, not in the SDK). 2 is what CS:S's pictures of the lighting playground
// show (docs/comparison/m8-lighting-2): a dark room needs it with the exposure at its most, and
// the sunlit courtyard with it near its least.
inline constexpr float HDR_LIGHTMAP_SCALE = 2.0f;

// What the lighting preview multiplies a texture by for a light (linear, 1 fully lit), as Source
// shows a lightmap without HDR: stored at half (its overbright of 2) in gamma 2.2, doubled when
// drawn. A face lit fully comes out about 1.46 times its texture. With HDR: the light times
// HDR_LIGHTMAP_SCALE, in gamma 2.2 (flScale carries the exposure).
[[nodiscard]] float LightFactor( float flLight, float flScale, bool bHdr = false );

// The game's auto exposure (mat_tonemap_algorithm 1, viewpostprocess.cpp): from the luminance of
// the picture drawn with flLast (linear, 0 to 1), the scale that puts the brightest 2% of it at
// 60%, or the middle of it at 3% when that is brighter; within the limits
// (mat_autoexposure_min/max: 0.5 and 2). The game eases toward it over time: flRate is how much
// of the way (in stops) this step goes.
[[nodiscard]] float NextExposure( std::vector<float> luminance, float flLast, float flMin = 0.5f, float flMax = 2.0f, float flRate = 0.5f );
// The alpha the HDR lighting preview draws with what its auto exposure leaves out: the sky (its
// 2D sky and the 3D skybox), and the faces it shows unlit (tool textures), which the game doesn't
// draw at all. CS:S keeps the ground's brightness with a white sky over most of the screen
// (docs/comparison/m8-lighting-2), which counting the sky would pull to the least.
inline constexpr std::uint8_t UNMEASURED_ALPHA = 254;

// The linear luminance (the game's weights) of an RGBA8 picture as drawn, every nStep-th pixel
// each way, with the monitor gamma the lighting preview applied (ShadeOptions_t) undone; pixels
// marked UNMEASURED_ALPHA left out.
[[nodiscard]] std::vector<float> ShownLuminance( const std::uint8_t *pRgba, int nWidth, int nHeight, std::size_t nStride, int nStep, float flMonitorGamma = 2.2f );

// Bilinear between texels, trilinear between mips (or the nearest mip without bMipBlend); u and v
// in texture widths (wrapping). DXT mips are read as they are when there are no decoded ones.
[[nodiscard]] std::array<float, 4> SampleTexture( const RasterTexture_t &texture, float u, float v, float flLod, bool bMipBlend = true );
// A texture as the shaded modes sample it, at texel position texel (in the texture's full-size
// texels) with the point's gradient: one sample at the gradient's mip level, or, with anisotropy,
// up to that many along the footprint's long side at a finer level, averaged. map.frag's
// SampleFiltered does the same.
[[nodiscard]] std::array<float, 4> SampleFiltered( const RasterTexture_t &texture, std::array<float, 2> texel, const ShadeInput_t &input, const ShadeOptions_t &options );
// The fixed light Hammer's shaded modes use: by how much a face looks along each axis.
[[nodiscard]] float FaceShade( CVec3 vecNormal, CVec3 vecShade );
// RGBA 0-1; nullopt where alpha testing throws the point away.
[[nodiscard]] std::optional<std::array<float, 4>> ShadeFragment( const ShadeMaterial_t &material, const ShadeInput_t &input, const ShadeOptions_t &options );

// What a Refract or Water face does to what is behind it: times m_vecTint, plus m_vecReflection.
// The graphics card draws it in two passes (a multiply, then an add); software in one.
struct RefractColor_t
{
	CVec3 m_vecTint{ 1, 1, 1 };
	CVec3 m_vecReflection{};
};
// Whether the material is drawn by ShadeRefract in this mode: the textured ones.
[[nodiscard]] bool IsRefracting( const ShadeMaterial_t &material, const ShadeOptions_t &options );
// What is behind is not shifted (the game's refraction): the card would need a copy of the
// picture so far. The normal map bends the reflection; the reflection is the map's sky (a VMF has
// no built cubemaps), a grey without one. map.frag does the same steps.
[[nodiscard]] RefractColor_t ShadeRefract( const ShadeMaterial_t &material, const ShadeInput_t &input, const ShadeOptions_t &options );

} // namespace xv::map
