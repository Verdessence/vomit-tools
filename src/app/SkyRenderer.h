//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The sky view drawn on the graphics card: the same picture as sky::Render, a pixel at a
//          time in a shader, with the faces as textures.
//
//===========================================================================//
#pragma once

#ifdef VOMIT_TOOLS_GPU

#include <array>
#include <functional>
#include <memory>

#include "GpuView.h"
#include "sky/Sky.h"

class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;

class CSkyRenderer : public IGpuRenderer
{
public:
	// What to draw. The faces are uploaded again when the cube or its generation changes.
	struct Frame_t
	{
		const xv::sky::CCube *m_pCube = nullptr;
		int m_nGeneration = 0;
		xv::sky::Angles_t m_Angles;
		float m_flFov = 90.0f;
		bool m_bEdges = false;
	};
	using Source_t = std::function<Frame_t()>;

	explicit CSkyRenderer( Source_t pfnSource );
	~CSkyRenderer() override;

	void Initialize( QRhi *pRhi, QRhiRenderTarget *pTarget ) override;
	void Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget ) override;
	void Release() override;

private:
	// Uploads the cube's faces and binds them, each with the sampler its transform asks for.
	void Upload( const xv::sky::CCube *pCube, QRhiResourceUpdateBatch *pUpdates );

	Source_t m_pfnSource;
	QRhi *m_pRhi = nullptr;
	std::unique_ptr<QRhiBuffer> m_pUniforms;
	std::array<std::unique_ptr<QRhiSampler>, 4> m_Samplers; // clamped or repeating in u, v
	std::array<std::unique_ptr<QRhiTexture>, 6> m_Faces;
	std::unique_ptr<QRhiTexture> m_pBlack; // where a face has no image
	std::unique_ptr<QRhiShaderResourceBindings> m_pBindings;
	std::unique_ptr<QRhiGraphicsPipeline> m_pPipeline;
	const xv::sky::CCube *m_pUploaded = nullptr;
	int m_nUploadedGeneration = -1;
};

#endif
