//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A picture of a material the way the game would draw it, on a flat square or a ball under
//          one light: its textures, tints and blending, detail, bump, self-illumination,
//          reflections and phong, and HDR skies with an exposure. Everything that needs the map
//          (lightmaps, its cube maps, the screen behind water) has a stand-in.
//
//===========================================================================//
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "material/Material.h"
#include "sky/Sky.h"
#include "texture/Image.h"
#include "texture/Vtf.h"
#include "util/Vec3.h"

namespace xv::material
{

// One texture a material names, as read for the preview.
struct Texture_t
{
	std::string m_szParameter; // as written: "$basetexture"
	std::string m_szValue;     // "brick/brickwall001a"
	std::string m_szPath;      // "materials/brick/brickwall001a.vtf"; empty for env_cubemap and render targets
	TextureKind_t m_nKind = TextureKind_t::TEXTURE_KIND_FILE;
	std::optional<texture::VtfInfo_t> m_Info; // nullopt when it couldn't be read
	std::string m_szProblem;                  // why not
	int m_nMip = 0;                           // the mipmap read: big textures are read smaller
	std::vector<texture::CImage> m_Frames;    // each frame; for a cube map, its faces laid out as a cross
	std::optional<sky::CCube> m_Cube;         // a cube map's faces, as sky faces
	texture::CFloatImage m_Light;             // HDR formats: the first frame's light values
};

class CTextureSet
{
public:
	// Reads every texture the material names through pfnRead. Images wider or taller than nMaxSize
	// are read from a smaller mipmap; at most nMaxFrames frames are read.
	[[nodiscard]] static CTextureSet Load( const CMaterial &material, const Reader_t &pfnRead, int nMaxSize = 1024, int nMaxFrames = 64 );

	[[nodiscard]] const std::vector<Texture_t> &GetTextures() const { return m_Textures; }
	[[nodiscard]] const Texture_t *Find( std::string_view szParameter ) const;
	// Frames of the texture in a parameter, 0 when there is none (for AnimatedTexture).
	[[nodiscard]] int FrameCount( std::string_view szParameter ) const;

private:
	std::vector<Texture_t> m_Textures;
};

enum class Shape_t
{
	SHAPE_FLAT,
	SHAPE_SPHERE,
};

enum class Backdrop_t
{
	BACKDROP_CHECKERS,
	BACKDROP_DARK,
	BACKDROP_LIGHT,
};

struct PreviewOptions_t
{
	int m_nSize = 256;
	Shape_t m_nShape = Shape_t::SHAPE_FLAT;
	CVec3 m_vecLight{ -0.45f, 0.55f, 0.7f }; // towards the light: x right, y up, z out of the screen
	float m_flRepeat = 1.0f;                // times the texture fits across the square (twice that around the ball)
	Backdrop_t m_nBackdrop = Backdrop_t::BACKDROP_CHECKERS;
	bool m_bHdr = false;       // a sky's HDR texture rather than its LDR one
	float m_flExposure = 0.0f; // stops, for HDR
};

// The material (with its proxies' results already in it) drawn into a square image.
[[nodiscard]] texture::CImage RenderPreview( const CMaterial &material, const CTextureSet &textures, const PreviewOptions_t &options );
// Whether the material has an HDR texture the preview can show (HDR skies).
[[nodiscard]] bool HasHdr( const CMaterial &material, const CTextureSet &textures );
// What the preview leaves out or only approximates for this material, a line each.
[[nodiscard]] std::vector<std::string> PreviewNotes( const CMaterial &material, const CTextureSet &textures );

} // namespace xv::material
