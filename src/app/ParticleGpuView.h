//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The particle preview drawn on the graphics card, through Qt's RHI (Vulkan, Metal,
//          Direct3D or OpenGL, whichever the platform has). It draws the same quads as the
//          software renderer (particles::layout), so the two look alike; the GPU just does it
//          faster and at full size.
//
//===========================================================================//
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QRhiWidget>

#include "particles/Render.h"

class QRhi;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

class CParticleGpuView : public QRhiWidget
{
	Q_OBJECT

public:
	// Fills quads (grid first, then particles) and a model's triangles for a picture of width x
	// height device pixels. The model is drawn first; what it hides isn't drawn over it.
	using Source_t = std::function<void( int nWidth, int nHeight, std::vector<xv::particles::ScreenQuad_t> &quads, std::vector<xv::particles::ScreenMesh_t> &meshes,
		std::array<float, 3> &vecBackground )>;

	explicit CParticleGpuView( Source_t pfnSource, QWidget *pParent = nullptr );
	~CParticleGpuView() override;

	// Sprites were reloaded: textures made from the old ones must go.
	void ForgetTextures();

protected:
	void initialize( QRhiCommandBuffer *pCommands ) override;
	void render( QRhiCommandBuffer *pCommands ) override;
	void releaseResources() override;

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
	std::unique_ptr<QRhiGraphicsPipeline> MakePipeline( Pipeline_t nKind );

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
	quint32 m_nVertexCapacity = 0; // in quads
	quint32 m_nIndexCapacity = 0;
};
