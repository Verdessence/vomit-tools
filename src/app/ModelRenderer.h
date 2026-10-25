//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Studio material resources and GPU skinning, sharing material shading and overlays.
//===========================================================================//
#pragma once
#include "material/Preview.h"
#include "particles/Render.h"
struct ModelMaterial_t
{
	xv::material::CMaterial m_Material;
	xv::material::CTextureSet m_Textures;
};
#ifdef VOMIT_TOOLS_GPU
#include "MaterialRenderer.h"
#include "ParticleRenderer.h"
class CModelRenderer : public IGpuRenderer
{
public:
	struct Frame_t
	{
		const xv::model::CModel *m_pReference = nullptr, *m_pPosed = nullptr;
		const std::vector<ModelMaterial_t> *m_pMaterials = nullptr;
		xv::particles::CCamera m_Camera;
		std::vector<xv::particles::ScreenQuad_t> m_Quads;
	};
	using Source_t = std::function<Frame_t( QSize )>;
	explicit CModelRenderer( Source_t pfnSource );
	~CModelRenderer() override;
	void ForgetTextures();
	void Initialize( QRhi *pRhi, QRhiRenderTarget *pTarget ) override;
	void Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget ) override;
	void Release() override;

private:
	void Prepare( QRhiRenderTarget *pTarget, QRhiResourceUpdateBatch *pUpdates );
	Source_t m_pfnSource;
	Frame_t m_Frame;
	CParticleRenderer m_Overlays;
	QRhi *m_pRhi = nullptr;
	std::unique_ptr<QRhiBuffer> m_pMeshUniforms, m_pVertices;
	std::vector<std::unique_ptr<CMaterialRenderer>> m_Materials;
	struct Run_t
	{
		std::size_t m_nMaterial = 0;
		quint32 m_nFirst = 0, m_nCount = 0;
	};
	std::vector<Run_t> m_Runs;
};
#endif
