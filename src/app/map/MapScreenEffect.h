//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A screen effect over the map's 3D views (M16): a screenspace_general material (an
//          env_screenoverlay's, a post effect the map names, or any picked) read with its pixel
//          shader, its textures and its proxies, and drawn over each frame as the game would:
//          in software through the interpreter, on the graphics card as its bytecode written as
//          GLSL (CScreenEffectPass: the scene drawn to a texture first, then the effect from it
//          onto the view).
//
//===========================================================================//
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>

#include <QString>

#include "material/Material.h"
#include "material/Preview.h"
#include "shader/Interpreter.h"
#include "shader/ScreenEffect.h"
#include "texture/Image.h"

#ifdef VOMIT_TOOLS_GPU
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#endif

class CMapScreenEffect
{
public:
	// The material by its name under materials/ ("effects/shaders/example_glitch"), read through
	// pfnRead (game paths); null when it isn't a screenspace_general material or can't be read,
	// with why in pProblem.
	[[nodiscard]] static std::shared_ptr<CMapScreenEffect> Load( const QString &szMaterial, const xv::material::Reader_t &pfnRead, QString *pProblem = nullptr );

	[[nodiscard]] const QString &GetMaterial() const { return m_szMaterial; }
	// Why the effect can't be drawn (its shader missing, unreadable, or not runnable); empty
	// when it can.
	[[nodiscard]] const QString &GetProblem() const { return m_szProblem; }
	[[nodiscard]] bool CanRun() const { return m_Shader && m_Shader->CanRun(); }
	// The pixel shader's path as found, and its profile.
	[[nodiscard]] const QString &GetShaderPath() const { return m_szShaderPath; }
	[[nodiscard]] QString GetProfile() const;
	// Whether a proxy moves it with time (it needs drawing again as time passes).
	[[nodiscard]] bool IsAnimated() const;

	// The constants as the material gives them, its proxies run at flTime, with the overrides over
	// them.
	[[nodiscard]] xv::shader::ScreenEffect_t At( float flTime ) const;
	[[nodiscard]] const std::array<std::optional<xv::shader::Vec4_t>, 4> &GetOverrides() const { return m_Overrides; }
	// A copy with c0..c3 set by hand (nullopt: the material's own again).
	[[nodiscard]] std::shared_ptr<CMapScreenEffect> WithOverrides( std::array<std::optional<xv::shader::Vec4_t>, 4> overrides ) const;

	// The effect over a frame (sRGB, as drawn) in software, as at flTime.
	void Apply( xv::texture::CImage &frame, float flTime ) const;

	// The textures that aren't the frame, by sampler (empty: black, or the frame when
	// IsFrameSampler).
	[[nodiscard]] const xv::texture::CImage &GetTexture( int nSampler ) const { return m_Textures[ static_cast<std::size_t>( nSampler ) ]; }
	[[nodiscard]] bool IsFrameSampler( int nSampler ) const { return m_FrameSamplers[ static_cast<std::size_t>( nSampler ) ]; }
	// The pixel shader as the card takes it, and its own def constants (the uniform block holds them).
	[[nodiscard]] const std::string &GetGlsl() const { return m_szGlsl; }
	[[nodiscard]] const xv::shader::CPixelShader *GetShader() const { return m_Shader ? &*m_Shader : nullptr; }
#ifdef VOMIT_TOOLS_GPU
	// Baked the first time it is asked for; invalid when it doesn't bake (why in GetCardProblem).
	[[nodiscard]] const QShader &GetBaked() const;
	[[nodiscard]] const QString &GetCardProblem() const { return m_szCardProblem; }
#endif

private:
	QString m_szMaterial;
	QString m_szProblem;
	QString m_szShaderPath;
	std::shared_ptr<const xv::material::CMaterial> m_pMaterial;
	std::shared_ptr<const xv::material::CProxies> m_pProxies;
	std::shared_ptr<const xv::material::CTextureSet> m_pTextureSet;
	std::optional<xv::shader::CPixelShader> m_Shader;
	std::string m_szGlsl;
	std::array<xv::texture::CImage, 4> m_Textures;
	std::array<bool, 4> m_FrameSamplers{};
	std::array<std::optional<xv::shader::Vec4_t>, 4> m_Overrides;
#ifdef VOMIT_TOOLS_GPU
	mutable std::shared_ptr<QShader> m_pBaked;
	mutable QString m_szCardProblem;
#endif
};

#ifdef VOMIT_TOOLS_GPU

// The card's side: a texture the scene is drawn to (the view's size), and the effect drawn from
// it onto the view's own target.
class CScreenEffectPass
{
public:
	// Where the scene goes this frame: a texture of pFinal's size, made again when that changes.
	[[nodiscard]] QRhiRenderTarget *SceneTarget( QRhi *pRhi, QRhiRenderTarget *pFinal );
	// The effect from that texture onto pFinal, as at flTime; false when it can't be drawn (the
	// scene is then left in the texture, and the caller draws again without it).
	bool Draw( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pFinal, const std::shared_ptr<const CMapScreenEffect> &pEffect, float flTime );

private:
	QRhi *m_pRhi = nullptr;
	QSize m_Size;
	std::unique_ptr<QRhiTexture> m_pScene;
	std::unique_ptr<QRhiRenderBuffer> m_pDepth;
	std::unique_ptr<QRhiTextureRenderTarget> m_pTarget;
	std::unique_ptr<QRhiRenderPassDescriptor> m_pPass;
	std::unique_ptr<QRhiBuffer> m_pUniforms;
	std::unique_ptr<QRhiSampler> m_pSampler;
	std::array<std::unique_ptr<QRhiTexture>, 4> m_Textures;
	std::unique_ptr<QRhiTexture> m_pBlack;
	std::unique_ptr<QRhiShaderResourceBindings> m_pBindings;
	std::unique_ptr<QRhiGraphicsPipeline> m_pPipeline;
	// What the pipeline and bindings were made for.
	std::shared_ptr<const CMapScreenEffect> m_pFor;
	QRhiRenderPassDescriptor *m_pPipelinePass = nullptr;
	QRhiTexture *m_pBoundScene = nullptr;
};

#endif
