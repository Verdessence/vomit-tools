//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The lighting preview's bakes (map/Lighting.h), one at a time on a thread of their
//          own. A new request cancels the bake under way, so edits never wait for one.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>

#include "map/Lighting.h"
#include "map/Scene.h"
#include "vfs/Vfs.h"

class CMapLighting : public QObject
{
	Q_OBJECT

public:
	explicit CMapLighting( QObject *pParent = nullptr );
	~CMapLighting() override;

	// Bakes this scene, dropping any bake not finished; Baked follows (or nothing, if another
	// request comes first), once with the direct light alone while the bake goes on (IsBaking
	// still true) and once when it is done.
	void Bake( std::shared_ptr<const xv::map::CScene> pScene, std::shared_ptr<const std::vector<xv::map::LightMaterial_t>> pMaterials, const xv::map::LightingOptions_t &options, xv::map::LightingExtras_t extras = {} );
	// Where texture lights come from: the game's lights.rad and the map's own <map>.rad beside it
	// (the RAD file article); read on the baking thread when they change.
	void SetRadSources( std::optional<xv::vfs::CGameInfo> game, std::filesystem::path map );
	// Drops the bake under way and the last result (another map).
	void Clear();

	[[nodiscard]] std::shared_ptr<const xv::map::CLighting> GetLighting() const { return m_pLighting; }
	[[nodiscard]] bool IsBaking() const { return m_bBaking; }
	[[nodiscard]] int GetPercent() const { return m_nPercent; }
	[[nodiscard]] const QString &GetStage() const { return m_szStage; }
	// How long the last finished bake took.
	[[nodiscard]] double GetBakeMs() const { return m_flBakeMs; }

signals:
	void Progress( int nPercent, const QString &szStage );
	void Baked();

private:
	struct Request_t
	{
		std::shared_ptr<const xv::map::CScene> m_pScene;
		std::shared_ptr<const std::vector<xv::map::LightMaterial_t>> m_pMaterials;
		xv::map::LightingOptions_t m_Options;
		xv::map::LightingExtras_t m_Extras;
		int m_nGeneration = 0;
		std::optional<xv::vfs::CGameInfo> m_Game;
		std::filesystem::path m_Map;
	};
	void Run();

	std::thread m_Worker;
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::optional<Request_t> m_Pending; // under m_Mutex
	bool m_bQuit = false;               // under m_Mutex
	std::atomic<bool> m_bCancel{ false };
	std::atomic<int> m_nGeneration{ 0 };
	std::optional<xv::vfs::CGameInfo> m_Game; // the main thread's, handed to each request
	std::filesystem::path m_Map;
	// The worker's: the rules read, and from what.
	xv::map::RadRules_t m_Rad;
	std::filesystem::path m_RadGame;
	std::filesystem::path m_RadMap;
	bool m_bRadRead = false;

	// The main thread's.
	std::shared_ptr<const xv::map::CLighting> m_pLighting;
	bool m_bBaking = false;
	int m_nPercent = 0;
	QString m_szStage;
	double m_flBakeMs = 0.0;
};
