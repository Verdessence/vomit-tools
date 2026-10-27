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
};

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
	bool m_bBlend = false;
	bool m_bAlphaTest = false;
	bool m_bTranslucent = false;
	bool m_bAdditive = false;
	float m_flAlphaReference = 0.7f;
	float m_flAlpha = 1.0f;
	CVec3 m_vecColor{ 1, 1, 1 };
	int m_nWidth = 128;                        // what texels divide by when there's no texture
	int m_nHeight = 128;
};

struct ShadeInput_t
{
	std::array<float, 2> m_Texel{};            // in texels, as the mesh stores it
	std::array<float, 2> m_Luxel{};            // in luxels
	float m_flBlend = 0.0f;                    // displacement alpha, 0-1
	CVec3 m_vecNormal{ 0, 0, 1 };
	float m_flLod = 0.0f;                      // mip level, fractional
	std::array<float, 4> m_Gradient{};         // texels the point moves a pixel right (u, v) and down (u, v)
	CVec3 m_vecObjectColor{ 1, 1, 1 };         // the flat colour (Hammer's editor colour), 0-1
	CVec3 m_vecDirection{ 1, 0, 0 };           // from the eye to the point (for the sky)
	bool m_bSelected = false;
	bool m_bInstance = false;                  // a func_instance's: tinted
	bool m_bInvalid = false;                   // a broken solid's: tinted
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
};

// Bilinear between texels, trilinear between mips (or the nearest mip without bMipBlend); u and v
// in texture widths (wrapping).
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

} // namespace xv::map
