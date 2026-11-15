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

	// What a bake may start from.
	enum class Start_t
	{
		START_LAST,    // only the scene changed since the last bake asked for (an edit): what the edit can't have touched keeps the last finished bake's light (LightingOptions_t::m_pPrevious)
		START_EARLIER, // more changed (a setting, the materials, the props): not from the last bake, but an earlier bake that read the same as this one will serves (LightingOptions_t::m_Cache)
		START_NOTHING, // everything is baked, whatever was baked before (Bake Lighting)
	};
	// Bakes this scene, dropping any bake not finished; Baked follows (or nothing, if another
	// request comes first), once with the direct light alone while the bake goes on (IsBaking
	// still true) and once when it is done. bSettled: the materials and props have all arrived
	// (the bake that is kept for the map's next opening).
	void Bake( std::shared_ptr<const xv::map::CScene> pScene, std::shared_ptr<const std::vector<xv::map::LightMaterial_t>> pMaterials, const xv::map::LightingOptions_t &options, xv::map::LightingExtras_t extras = {}, Start_t nStart = Start_t::START_EARLIER, bool bSettled = true );
	// Earlier bakes are kept to be found again (an undo, a setting put back: a few in memory;
	// the map opened again: the last one in a file under the cache folder, by the map's path).
	void SetKeepBakes( bool bKeep );
	// The file a map's last bake is kept in; empty for a map with no file, or with
	// VOMIT_TOOLS_LIGHTING_CACHE=off. VOMIT_TOOLS_LIGHTING_CACHE names another folder.
	[[nodiscard]] static std::filesystem::path CacheFileFor( const std::filesystem::path &map );
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
		std::filesystem::path m_CacheFile; // empty: nothing is read or written
		bool m_bUseCache = false;          // the file's bake may be started from
		bool m_bSettled = true;
	};
	void Run();
	// The worker's: reads the map's kept bake once, and writes a finished one.
	void ReadCacheFile( const std::filesystem::path &file );
	void WriteCacheFile( const std::filesystem::path &file, const xv::map::CLighting &lighting );

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
	// The bake read from the map's file, until a settled bake has taken its place; what the
	// file holds (0: nothing known), and a finished bake still to be written.
	std::shared_ptr<const xv::map::CLighting> m_pFromFile;
	std::filesystem::path m_ReadFile;
	std::uint64_t m_nOnDisk = 0;
	std::shared_ptr<const xv::map::CLighting> m_pToWrite;
	std::filesystem::path m_WriteFile;

	// The main thread's.
	std::shared_ptr<const xv::map::CLighting> m_pLighting;
	std::shared_ptr<const xv::map::CLighting> m_pFinished; // the last a bake after an edit can start from: whole, or as far as the one cut short got
	std::vector<std::shared_ptr<const xv::map::CLighting>> m_Earlier; // finished bakes, the newest first
	bool m_bKeepBakes = true;
	bool m_bBaking = false;
	int m_nPercent = 0;
	QString m_szStage;
	double m_flBakeMs = 0.0;
};
