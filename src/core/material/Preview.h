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

#include <array>
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
	bool m_bAmbientCube = false; // model lighting from six broad directions
	bool m_bHdr = false;       // a sky's HDR texture rather than its LDR one
	float m_flExposure = 0.0f; // stops, for HDR
};

using Rgba_t = std::array<float, 4>;

// How a shader draws, as far as the preview goes.
enum class Look_t
{
	LOOK_LIT,
	LOOK_UNLIT,
	LOOK_SPRITE,
	LOOK_MODULATE,
	LOOK_SKY,
	LOOK_WINDOW, // WindowImposter: a cube map seen through the surface
	LOOK_REFRACT,
	LOOK_WATER,
	LOOK_SCREEN, // screen-space effects, which have no surface of their own
};

// Reads one texture of the material at texture coordinates: its frame, transform and wrapping.
struct Sampler_t
{
	const texture::CImage *m_pImage = nullptr;
	const texture::CFloatImage *m_pLight = nullptr;
	sky::CUvTransform m_Transform;
	float m_flScaleU = 1.0f;
	float m_flScaleV = 1.0f;
	bool m_bClampS = false;
	bool m_bClampT = false;
	bool m_bPoint = false;

	[[nodiscard]] bool IsValid() const { return m_pImage && !m_pImage->Empty(); }

	// 0-1 per channel, as stored.
	[[nodiscard]] Rgba_t At( float u, float v ) const;
	// An HDR texture's light values, nearest texel.
	[[nodiscard]] CVec3 LightAt( float u, float v ) const;
};

// Everything about a material the preview's shading needs, read once per picture. RenderPreview
// draws with it, and so does the graphics card's preview (MaterialRenderer in the app).
struct Shading_t
{
	Look_t m_nLook = Look_t::LOOK_LIT;
	std::string m_szKind;
	Sampler_t m_Base;
	Sampler_t m_Base2;
	Sampler_t m_Detail;
	Sampler_t m_Bump;
	Sampler_t m_EnvMask;
	Sampler_t m_SelfIllumMask;
	Sampler_t m_PhongExponent;
	Sampler_t m_RefractTint;
	Sampler_t m_Hdr;
	bool m_bHdrCompressed = false;
	float m_flHdrScale = 1.0f;
	const Texture_t *m_pEnvMap = nullptr;
	bool m_bEnvMap = false;
	CVec3 m_vecColor{ 1, 1, 1 };
	CVec3 m_vecColor2{ 1, 1, 1 };
	float m_flAlpha = 1.0f;
	bool m_bTranslucent = false;
	bool m_bAdditive = false;
	bool m_bAlphaTest = false;
	float m_flAlphaReference = 0.7f;
	bool m_bSsBump = false;
	bool m_bHalfLambert = false;
	bool m_bSelfIllum = false;
	CVec3 m_vecSelfIllumTint{ 1, 1, 1 };
	bool m_bBaseAlphaEnvMask = false;
	bool m_bNormalAlphaEnvMask = false;
	CVec3 m_vecEnvTint{ 1, 1, 1 };
	float m_flEnvContrast = 0.0f;
	CVec3 m_vecEnvSaturation{ 1, 1, 1 };
	int m_nDetailMode = 0;
	float m_flDetailFactor = 1.0f;
	CVec3 m_vecDetailTint{ 1, 1, 1 };
	bool m_bPhong = false;
	float m_flPhongExponent = 5.0f;
	float m_flPhongBoost = 1.0f;
	CVec3 m_vecPhongFresnel{ 0.05f, 0.5f, 1.0f };
	CVec3 m_vecPhongTint{ 1, 1, 1 };
	bool m_bBaseAlphaPhongMask = false;
	bool m_bBlendTint = false;
	float m_flTintReplace = 0.0f;
	float m_flOverbright = 1.0f;
	float m_flAddSelf = 0.0f;
	bool m_bMod2x = false;
	float m_flRefractAmount = 0.0f;
	CVec3 m_vecRefractTint{ 1, 1, 1 };
	CVec3 m_vecFog{ 0.2f, 0.25f, 0.25f };
	bool m_bTwoTextures = false;    // UnlitTwoTexture: the second multiplies the first
	bool m_bBlendTextures = false;  // WorldVertexTransition: the second fades in across the square
};
[[nodiscard]] Shading_t ReadShading( const CMaterial &material, const CTextureSet &textures, const PreviewOptions_t &options );

// The material (with its proxies' results already in it) drawn into a square image.
[[nodiscard]] texture::CImage RenderPreview( const CMaterial &material, const CTextureSet &textures, const PreviewOptions_t &options );
// Whether the material has an HDR texture the preview can show (HDR skies).
[[nodiscard]] bool HasHdr( const CMaterial &material, const CTextureSet &textures );
// What the preview leaves out or only approximates for this material, a line each.
[[nodiscard]] std::vector<std::string> PreviewNotes( const CMaterial &material, const CTextureSet &textures );

// A mesh surface in the preview's camera coordinates. The eye is at (0,0,3).
struct MeshSample_t
{
	CVec3 m_vecPosition, m_vecNormal, m_vecTangent, m_vecBitangent;
	float m_flU = 0, m_flV = 0;
};
struct MeshFragment_t
{
	CVec3 m_vecColor;
	float m_flAlpha = 1;
	bool m_bDiscard = false, m_bBlend = false, m_bAdditive = false;
};
// Uses the same shading as flat and sphere previews, including material masks and normal maps.
[[nodiscard]] MeshFragment_t ShadeMesh( const Shading_t &shading, const MeshSample_t &surface, const PreviewOptions_t &options );
} // namespace xv::material
