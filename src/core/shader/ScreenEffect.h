//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A screenspace_general material (an env_screenoverlay's, a post effect's) as the game
//          draws it over the frame: its pixel shader (shaders/fxc/<$pixshader>.vcs, the _ps20b
//          one for a _ps20 name, as cards with ps_2_b load), its four textures in samplers s0 to
//          s3 (_rt_FullFrameFB the frame so far), $c0_x to $c3_w in c0 to c3, each texture's
//          texel size in c4 to c7, and how the result goes onto the frame ($additive,
//          $alpha_blend, $alpha_blend_color_overlay, $copyalpha; $linearread_*, $linearwrite).
//          Run here in software over a picture of the frame.
//
//===========================================================================//
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "material/Material.h"
#include "shader/Interpreter.h"
#include "texture/Image.h"

namespace xv::shader
{

enum class ScreenBlend_t
{
	SCREEN_BLEND_NONE,          // the shader's colour in the frame's place
	SCREEN_BLEND_ADDITIVE,      // $additive: added
	SCREEN_BLEND_COLOR_OVERLAY, // $alpha_blend_color_overlay: added over what its alpha leaves
	SCREEN_BLEND_ALPHA,         // $alpha_blend: over the frame by its alpha
};

struct ScreenEffect_t
{
	std::string m_szPixelShader;           // as the material names it
	std::array<std::string, 4> m_Textures; // $basetexture, $texture1, $texture2, $texture3
	std::array<bool, 4> m_LinearRead{};    // each read as it is (else sRGB, made linear)
	bool m_bLinearWrite = false;           // written as it is (else made sRGB again)
	bool m_bCopyAlpha = false;
	ScreenBlend_t m_nBlend = ScreenBlend_t::SCREEN_BLEND_NONE;
	std::array<Vec4_t, 4> m_Constants{}; // c0 to c3
};

// Whether a material is drawn this way (its shader screenspace_general).
[[nodiscard]] bool IsScreenEffect( const material::CMaterial &material );
[[nodiscard]] ScreenEffect_t ReadScreenEffect( const material::CMaterial &material );
// Where its pixel shader is looked for, in order: the _ps20b file for a _ps20 name, then the name.
[[nodiscard]] std::vector<std::string> PixelShaderPaths( std::string_view szPixelShader );
// Whether a texture name is one of the frame's own pictures (_rt_FullFrameFB, _rt_SmallFB0...).
[[nodiscard]] bool IsFrameTexture( std::string_view szTexture );

// The effect over frame (sRGB, as shown): textures by sampler (null: black; the frame's own
// pictures are frame itself, the caller passing it); nullopt, with why, when the shader can't run.
[[nodiscard]] std::optional<texture::CImage> ApplyScreenEffect( const CPixelShader &shader, const ScreenEffect_t &effect, const texture::CImage &frame, const std::array<const texture::CImage *, 4> &textures, std::string *pError = nullptr );

} // namespace xv::shader
