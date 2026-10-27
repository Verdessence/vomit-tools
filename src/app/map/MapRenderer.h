//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map's 3D view on the graphics card. The meshes go up once per map, in buffers
//          that never change; each object's colours and flags go in small textures, so selecting
//          and hiding only rewrite those. It draws what xv::map::RenderView draws: see
//          shaders/map.frag, which follows core/map/Shade.cpp.
//
//===========================================================================//
#pragma once

#ifdef VOMIT_TOOLS_GPU

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <QSize>

#include "GpuView.h"
#include "map/MapRenderData.h"

class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;

class CMapRenderer : public IGpuRenderer
{
public:
	// Gives the frame to draw for a size in pixels.
	using Source_t = std::function<MapFrame_t( QSize )>;

	explicit CMapRenderer( Source_t pfnSource );
	~CMapRenderer() override;

	void Initialize( QRhi *pRhi, QRhiRenderTarget *pTarget ) override;
	void Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget ) override;
	void Release() override;

	// Draw calls and triangles of the last picture, for the statistics.
	[[nodiscard]] int GetDrawCalls() const { return m_nDrawCalls; }

private:
	struct Mesh_t
	{
		const xv::map::CMapMesh *m_pFor = nullptr;
		std::unique_ptr<QRhiBuffer> m_pVertices;
		std::unique_ptr<QRhiBuffer> m_pIndices;
		std::unique_ptr<QRhiBuffer> m_pLines;
	};
	struct Material_t
	{
		const xv::map::RasterTexture_t *m_pBaseFor = nullptr;
		const xv::map::RasterTexture_t *m_pBase2For = nullptr;
		std::unique_ptr<QRhiTexture> m_pBase;
		std::unique_ptr<QRhiTexture> m_pBase2;
		std::unique_ptr<QRhiBuffer> m_pUniforms;
		std::unique_ptr<QRhiShaderResourceBindings> m_pBindings;
	};

	void UploadMeshes( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates );
	void UploadState( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates );
	void UploadMaterials( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates );
	[[nodiscard]] QRhiTexture *Upload( const xv::map::RasterTexture_t &texture, std::unique_ptr<QRhiTexture> &pTexture, QRhiResourceUpdateBatch *pUpdates );
	[[nodiscard]] QRhiSampler *SamplerFor( const xv::map::RasterTexture_t *pTexture ) const;
	void MakeBindings( Material_t &material, QRhiTexture *pBase, QRhiTexture *pBase2, const xv::map::RasterTexture_t *pBaseTexture, const xv::map::RasterTexture_t *pBase2Texture );
	void MakePipelines( QRhiRenderTarget *pTarget );
	bool UploadSky( const MapFrame_t &frame, QRhiResourceUpdateBatch *pUpdates );
	[[nodiscard]] QRhiTexture *SkyFace( std::size_t nFace ) const;
	[[nodiscard]] QRhiSampler *SkySampler( std::size_t nFace ) const;

	Source_t m_pfnSource;
	QRhi *m_pRhi = nullptr;
	std::vector<Mesh_t> m_Meshes;
	std::vector<Material_t> m_Materials;
	const void *m_pMaterialsFor = nullptr;
	Material_t m_LinesAll;       // the wireframe's edges
	Material_t m_LinesSelected;  // the selection's
	std::unique_ptr<QRhiBuffer> m_pFrame;
	std::unique_ptr<QRhiTexture> m_pState;
	std::unique_ptr<QRhiTexture> m_pLineColors;
	std::unique_ptr<QRhiTexture> m_pWhite;
	std::unique_ptr<QRhiBuffer> m_pSkyUniforms;
	std::array<std::unique_ptr<QRhiTexture>, 6> m_SkyFaces;
	const xv::sky::CCube *m_pSkyFor = nullptr;
	std::array<std::unique_ptr<QRhiSampler>, 4> m_Samplers; // repeat or clamp in s and t
	std::array<std::unique_ptr<QRhiSampler>, 4> m_TopSamplers; // the same, top level only: OpenGL reads a texture without mips as black through a mip filter
	std::unique_ptr<QRhiSampler> m_pNearest;
	const void *m_pStateFlags = nullptr;
	const void *m_pStateColors = nullptr;
	const void *m_pStateLines = nullptr;
	int m_nStateWidth = 1024;
	std::unique_ptr<QRhiGraphicsPipeline> m_pOpaque;
	std::unique_ptr<QRhiGraphicsPipeline> m_pBlended;
	std::unique_ptr<QRhiGraphicsPipeline> m_pAdditive;
	std::unique_ptr<QRhiGraphicsPipeline> m_pLines;
	int m_nDrawCalls = 0;
	// The last frame, held so nothing it points to is freed (and its address reused) while the
	// uploads above are still compared with it.
	MapFrame_t m_Held;
};

#endif
