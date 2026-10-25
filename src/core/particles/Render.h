//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Drawing a particle simulation the way the game does in broad strokes: textured sprites
//          facing the camera (or lying flat, or stretched into trails and ropes), tinted by each
//          particle's colour and alpha, blended additively or over what's behind.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "particles/Simulation.h"
#include "material/Preview.h"
#include "texture/Image.h"
#include "texture/Sheet.h"
#include "util/Vec3.h"

namespace xv::particles
{

// Sprite sheets live with textures (texture/Sheet.h); the preview reads them from there.
using texture::CSheet;
using texture::ReadSheet;

// A material as the preview draws it.
struct Sprite_t
{
	enum class Blend_t
	{
		BLEND_ALPHA,
		BLEND_ADDITIVE
	};

	std::vector<texture::CImage> m_Mips; // mips[0] full size; a soft round dot when the texture is missing
	CSheet m_Sheet;
	Blend_t m_nBlend = Blend_t::BLEND_ALPHA;
	float m_flOverbright = 1.0f; // SpriteCard's $overbrightfactor
	float m_flAddSelf = 0.0f;    // SpriteCard's $addself: adds the texture on top of blending it
	bool m_bVisible = true;     // false for materials the preview can't draw (refraction)
	std::string m_szProblem;     // why the texture couldn't be used, if it couldn't
};
using FileReader_t = std::function<std::optional<std::vector<std::byte>>( std::string_view szGamePath )>;
// Reads materials/<material>.vmt and its $basetexture through read.
[[nodiscard]] Sprite_t LoadSprite( std::string_view szMaterial, const FileReader_t &pfnRead );
// The soft dot drawn for missing textures.
[[nodiscard]] Sprite_t FallbackSprite();

// An orbiting camera: it looks at target from distance away, turned by yaw (degrees, 0 looking
// along +X) and pitched (degrees, positive looking down).
struct CCamera
{
	CVec3 m_vecTarget;
	float m_flYaw = 135.0f;
	float m_flPitch = 20.0f;
	float m_flDistance = 200.0f;
	float m_flFov = 60.0f; // vertical, degrees
	int m_nWidth = 640;
	int m_nHeight = 480;

	[[nodiscard]] CVec3 GetEye() const;
	[[nodiscard]] CVec3 GetForward() const;
	// Screen position (pixels) and depth along the view; nullopt behind the camera.
	[[nodiscard]] std::optional<std::array<float, 3>> Project( CVec3 vecPoint ) const;
	// Pixels per world unit at a depth.
	[[nodiscard]] float Scale( float flDepth ) const;
	// The direction from the eye through a screen position (pixels).
	[[nodiscard]] CVec3 Ray( float x, float y ) const;
};

// A camera looking at everything a system shows in its first seconds (simulated on a copy), from
// its current direction. Falls back to the system's bounding box when nothing appears.
[[nodiscard]] CCamera Framed( const CSimulation &simulation, CCamera camera, float flSeconds = 1.5f );

// A model's textures, by its material index.
using ModelSpriteLookup_t = std::function<const Sprite_t *( std::size_t nMaterial )>;

struct RenderOptions_t
{
	bool m_bGrid = true;
	ModelSpriteLookup_t m_pfnModelSprites; // unset: the model is drawn plain grey
	std::array<float, 3> m_vecBackground{ 0.13f, 0.14f, 0.15f };
};

// Sprites by definition index, from the simulation's library.
using SpriteLookup_t = std::function<const Sprite_t *( std::size_t nDefinition )>;

// A sprite, a piece of a trail or rope, or a piece of grid line, as it lands on the screen.
struct ScreenQuad_t
{
	// x, y (pixels) and depth of the corners showing the texture's top-left, top-right,
	// bottom-right and bottom-left. The depths let the texture follow the perspective.
	std::array<std::array<float, 3>, 4> m_Corners{};
	std::array<float, 4> m_Rect{ 0, 0, 1, 1 }; // the part of the texture shown
	std::array<float, 3> m_vecColor{ 1, 1, 1 };   // tint, overbright included
	float m_flAlpha = 1.0f;
	float m_flAddSelf = 0.0f;
	Sprite_t::Blend_t m_nBlend = Sprite_t::Blend_t::BLEND_ALPHA;
	const Sprite_t *m_pSprite = nullptr; // nullptr: plain white
	float m_flDepth = 0.0f;             // of its centre
	float m_flPixels = 0.0f;            // how big it is on screen, for picking a mipmap
};
// Everything the particles draw, in drawing order (system by system, back to front in each).
[[nodiscard]] std::vector<ScreenQuad_t> Layout( const CSimulation &simulation, const CCamera &camera, const SpriteLookup_t &pfnSprites );
// The distance between the ground grid's lines, which depends on how far away the camera is.
[[nodiscard]] float GridSpacing( const CCamera &camera );
// A corner of a model's triangle as it lands on the screen: x, y (pixels) and depth; texture
// coordinates; the light on it.
struct ScreenVertex_t
{
	std::array<float, 3> m_vecPosition{};
	std::array<float, 2> m_Uv{};
	std::array<float, 3> m_vecColor{ 1, 1, 1 };
	material::MeshSample_t m_Surface{};
};
// The triangles of one texture (three vertices each); a null sprite is plain grey.
struct ScreenMesh_t
{
	const Sprite_t *m_pSprite = nullptr;
	std::vector<ScreenVertex_t> m_Vertices;
	const material::Shading_t *m_pShading = nullptr;
	material::PreviewOptions_t m_Options{};
};
// The simulation's model, if it has one, lit from over the camera's shoulder. Triangles reaching
// behind the camera are left out.
[[nodiscard]] std::vector<ScreenMesh_t> LayoutModel( const CSimulation &simulation, const CCamera &camera, const ModelSpriteLookup_t &pfnSprites );
// A model's triangles on screen, placed by toWorld, lit from behind the camera.
[[nodiscard]] std::vector<ScreenMesh_t> LayoutMesh( const model::CModel &model, const model::CTransform &toWorld, const CCamera &camera, const ModelSpriteLookup_t &pfnSprites );
// The ground grid, as thin quads.
[[nodiscard]] std::vector<ScreenQuad_t> GridQuads( const CCamera &camera );
// Draws into out (camera.width x camera.height). Rows are split over the available cores.
void Render( const CSimulation &simulation, const CCamera &camera, const SpriteLookup_t &pfnSprites, texture::CImage &out, const RenderOptions_t &options = {} );
// Any scene drawn the same way: meshes (nearest wins, filling the depth), then quads in the order
// given (back to front), each tested against the meshes' depth at its centre's depth.
void RenderScene( const CCamera &camera, std::span<const ScreenMesh_t> meshes, std::span<const ScreenQuad_t> quads, texture::CImage &out, const RenderOptions_t &options = {} );

} // namespace xv::particles
