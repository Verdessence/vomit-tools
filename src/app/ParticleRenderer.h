//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Particles drawn on the graphics card. It draws the same quads as the software renderer
//          (particles::Layout), so the two look alike; the GPU just does it faster and at full
//          size. One pass can hold several pictures side by side (the grid's thumbnails).
//
//===========================================================================//
#pragma once

#ifdef VOMIT_TOOLS_GPU

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QRect>
#include <QSize>

#include "GpuView.h"
#include "particles/Render.h"

class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;

// One picture: grid and particle quads and a model's triangles, in pixels from the top-left of
// m_Rect, its place in the target. The model is drawn first; what it hides isn't drawn over it.
struct ParticlePicture_t
{
	QRect m_Rect;
	std::vector<xv::particles::ScreenQuad_t> m_Quads;
	std::vector<xv::particles::ScreenMesh_t> m_Meshes;
};

class CParticleRenderer : public IGpuRenderer
{
public:
	// Fills the pictures for a target of a size (device pixels), and the colour behind them.
	using Source_t = std::function<void( QSize size, std::vector<ParticlePicture_t> &pictures, std::array<float, 3> &vecBackground )>;

	explicit CParticleRenderer( Source_t pfnSource );
	~CParticleRenderer() override;

	// Sprites were reloaded: textures made from the old ones must go.
	void ForgetTextures();

	void Initialize( QRhi *pRhi, QRhiRenderTarget *pTarget ) override;
	void Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget ) override;
	void Release() override;
	// Adds another mesh pass before the overlay quads, sharing one update batch and depth buffer.
	void RenderWith( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget, const std::function<void( QRhiResourceUpdateBatch * )> &pfnPrepare, const std::function<void()> &pfnDraw );

private:
	struct Texture_t
	{
		std::unique_ptr<QRhiTexture> m_pTexture;
		std::unique_ptr<QRhiShaderResourceBindings> m_pBindings;
	};
	// Particle textures clamp at their edges; a model's (bRepeat) tile.
	Texture_t &TextureFor( const xv::particles::Sprite_t *pSprite, QRhiResourceUpdateBatch *pUpdates, bool bRepeat = false );
	enum class Pipeline_t
	{
		PIPELINE_BLENDED,
		PIPELINE_ADDED,
		PIPELINE_SOLID // the model: opaque, and nearest wins
	};
	std::unique_ptr<QRhiGraphicsPipeline> MakePipeline( Pipeline_t nKind, QRhiRenderTarget *pTarget );

	Source_t m_pfnSource;
	QRhi *m_pRhi = nullptr;
	std::unique_ptr<QRhiBuffer> m_pVertices;
	std::unique_ptr<QRhiBuffer> m_pIndices;
	std::unique_ptr<QRhiBuffer> m_pModelVertices;
	std::unique_ptr<QRhiBuffer> m_pUniforms;
	std::unique_ptr<QRhiSampler> m_pSampler;
	std::unique_ptr<QRhiSampler> m_pRepeatSampler;
	std::unique_ptr<QRhiGraphicsPipeline> m_pSolid;
	std::unique_ptr<QRhiGraphicsPipeline> m_pBlended;
	std::unique_ptr<QRhiGraphicsPipeline> m_pAdded;
	Texture_t m_White; // for grid lines; its bindings also describe the pipelines' layout
	std::map<const xv::particles::Sprite_t *, Texture_t> m_Textures;
	std::map<const xv::particles::Sprite_t *, Texture_t> m_ModelTextures;
	Texture_t m_ModelWhite; // for a model mesh without a texture
	quint32 m_nIndexCapacity = 0;
};

#endif
