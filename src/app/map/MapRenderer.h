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

// What the last picture cost, for the statistics overlay (O) and the debug log.
struct MapRenderStats_t
{
	int m_nFrames = 0;            // pictures drawn since the renderer was made
	double m_flIntervalMs = 0.0;  // between this picture and the one before
	double m_flFrameMs = 0.0;     // the main thread's time in Render(): the three below and the rest
	double m_flSnapshotMs = 0.0;  // making the frame from the document (MakeFrame)
	double m_flUploadMs = 0.0;    // meshes, state and materials that changed
	double m_flRecordMs = 0.0;    // recording the draw calls
	double m_flGpuMs = -1.0;      // the card's own time, when the QRhi can say (timestamps on)
	int m_nDrawCalls = 0;
	int m_nTriangles = 0;
	int m_nUploads = 0;           // mesh buffers made this picture (0 once a map is up)
};

#ifdef VOMIT_TOOLS_GPU

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <QElapsedTimer>
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
	[[nodiscard]] const MapRenderStats_t &GetStats() const { return m_Stats; }

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
	std::array<std::unique_ptr<QRhiSampler>, 4> m_NearestMipSamplers; // the same, from the nearest mip (bilinear filtering)
	bool m_bMipBlend = true; // trilinear, else bilinear: the materials' bindings follow it
	std::unique_ptr<QRhiSampler> m_pNearest;
	const void *m_pStateFlags = nullptr;
	const void *m_pStateColors = nullptr;
	const void *m_pStateLines = nullptr;
	int m_nStateWidth = 1024;
	std::unique_ptr<QRhiGraphicsPipeline> m_pOpaque;
	std::unique_ptr<QRhiGraphicsPipeline> m_pBlended;
	std::unique_ptr<QRhiGraphicsPipeline> m_pAdditive;
	std::unique_ptr<QRhiGraphicsPipeline> m_pLines;
	std::unique_ptr<QRhiGraphicsPipeline> m_pSkyFaces; // the world's sky faces over a 3D skybox
	std::unique_ptr<QRhiGraphicsPipeline> m_pAlpha;    // makes the picture opaque afterwards
	std::unique_ptr<QRhiShaderResourceBindings> m_pNoBindings;
	quint32 m_nFrameStride = 0; // the world's frame uniforms, then the 3D skybox's
	int m_nDrawCalls = 0;
	MapRenderStats_t m_Stats;
	QElapsedTimer m_Interval;
	// The last frame, held so nothing it points to is freed (and its address reused) while the
	// uploads above are still compared with it.
	MapFrame_t m_Held;
};

#endif
