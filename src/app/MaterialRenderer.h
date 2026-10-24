//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The material preview drawn on the graphics card: the picture material::RenderPreview
//          makes, a pixel at a time in a shader (material.frag), at the full size of the square.
//          It reads the material with material::ReadShading, as the software preview does.
//
//===========================================================================//
#pragma once

#ifdef VOMIT_TOOLS_GPU

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <tuple>

#include <QColor>
#include <QPoint>

#include "GpuView.h"
#include "material/Preview.h"

class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;

class CMaterialRenderer : public IGpuRenderer
{
public:
	// What to draw: the material (its proxies' results in it), its textures, and the square,
	// options.m_nSize pixels across with its top-left at m_Corner, on m_Background.
	struct Frame_t
	{
		const xv::material::CMaterial *m_pMaterial = nullptr;
		const xv::material::CTextureSet *m_pTextures = nullptr;
		xv::material::PreviewOptions_t m_Options;
		QPoint m_Corner;
		QColor m_Background{ 24, 24, 28 };
	};
	using Source_t = std::function<Frame_t()>;

	explicit CMaterialRenderer( Source_t pfnSource );
	~CMaterialRenderer() override;

	// The textures were read again: what was uploaded from the old ones must go (it is kept by
	// their images' addresses).
	void ForgetTextures();

	void Initialize( QRhi *pRhi, QRhiRenderTarget *pTarget ) override;
	void Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget ) override;
	void Release() override;

private:
	// A texture made from an image (by its address), uploaded once and kept while it is used.
	struct Cached_t
	{
		std::unique_ptr<QRhiTexture> m_pTexture;
		std::uint64_t m_nLastUsed = 0;
	};
	QRhiTexture *TextureFor( const xv::texture::CImage *pImage, QRhiResourceUpdateBatch *pUpdates );
	QRhiTexture *LightFor( const xv::texture::CFloatImage *pLight, QRhiResourceUpdateBatch *pUpdates );
	QRhiTexture *CubeFor( const xv::sky::CCube *pCube, QRhiResourceUpdateBatch *pUpdates );
	// Linear or point, clamped or repeating in s and t, as the texture's flags say.
	QRhiSampler *SamplerFor( bool bClampS, bool bClampT, bool bPoint );
	// Drops textures not used for a while (an animated texture's frames come and go).
	void Trim();

	Source_t m_pfnSource;
	QRhi *m_pRhi = nullptr;
	std::unique_ptr<QRhiBuffer> m_pUniforms;
	std::map<std::tuple<bool, bool, bool>, std::unique_ptr<QRhiSampler>> m_Samplers;
	std::map<const void *, Cached_t> m_Textures;
	std::unique_ptr<QRhiTexture> m_pWhite;      // where a texture is missing
	std::unique_ptr<QRhiTexture> m_pWhiteCube;  // where there is no cube map
	std::unique_ptr<QRhiShaderResourceBindings> m_pBindings;
	std::array<std::pair<QRhiTexture *, QRhiSampler *>, 11> m_Bound{}; // what the bindings hold
	std::unique_ptr<QRhiGraphicsPipeline> m_pPipeline;
	std::uint64_t m_nFrame = 0;
};

#endif
