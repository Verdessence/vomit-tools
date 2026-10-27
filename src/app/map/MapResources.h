//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The textures the 3D views draw a map with, read from the game in the background: a
//          batch of materials at a time, the ones covering most of the map first, so the views
//          fill in while the rest arrive. Also the sky the map names.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <QObject>

#include "map/Materials.h"
#include "map/Raster.h"
#include "map/Scene.h"
#include "sky/Sky.h"
#include "vfs/Vfs.h"

class CMapResources : public QObject
{
	Q_OBJECT

public:
	explicit CMapResources( QObject *pParent = nullptr );
	~CMapResources() override;

	// Reads every material of the scene from the game; without a game, nothing is read and
	// materials draw plain. nMaxSize caps a texture's largest side (smaller mips are kept).
	void Start( std::shared_ptr<const xv::map::CScene> pScene, std::optional<xv::vfs::CGameInfo> game, int nMaxSize );
	void Stop();

	// The map's materials as drawn now (one per material of the map), and what keeps their
	// textures alive; both replaced as more arrive.
	[[nodiscard]] std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> GetMaterials() const { return m_pMaterials; }
	[[nodiscard]] std::shared_ptr<const void> GetTextures() const { return m_pTextures; }
	[[nodiscard]] std::shared_ptr<const xv::sky::CCube> GetSky() const { return m_pSky; }
	[[nodiscard]] int GetLoaded() const { return m_nLoaded; }
	[[nodiscard]] int GetTotal() const { return m_nTotal; }
	[[nodiscard]] bool IsFinished() const { return m_bFinished; }

signals:
	void Arrived();
	void Finished();

private:
	// One material as read on the worker.
	struct Loaded_t
	{
		std::uint32_t m_nMaterial = 0;
		xv::map::MaterialInfo_t m_Info;
		std::shared_ptr<xv::map::RasterTexture_t> m_pBase;
		std::shared_ptr<xv::map::RasterTexture_t> m_pBase2;
		bool m_bMissing = false;
		bool m_bTool = false;
	};

	void Run( std::shared_ptr<const xv::map::CScene> pScene, xv::vfs::CGameInfo game, int nMaxSize, int nGeneration );
	void Take( std::vector<Loaded_t> batch );

	std::thread m_Worker;
	std::atomic<bool> m_bStop{ false };
	int m_nGeneration = 0;
	std::vector<xv::map::RasterMaterial_t> m_Current;
	std::vector<std::shared_ptr<xv::map::RasterTexture_t>> m_Keep;
	std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> m_pMaterials;
	std::shared_ptr<const void> m_pTextures;
	std::shared_ptr<const xv::sky::CCube> m_pSky;
	int m_nLoaded = 0;
	int m_nTotal = 0;
	bool m_bFinished = false;
};

// A texture for drawing from a VTF: mips from the largest within nMaxSize down to the smallest.
[[nodiscard]] std::shared_ptr<xv::map::RasterTexture_t> ReadMapTexture( std::span<const std::byte> vtf, int nMaxSize );
