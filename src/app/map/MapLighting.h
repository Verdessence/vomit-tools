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
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>

#include "map/Lighting.h"
#include "map/Scene.h"

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
	};
	void Run();

	std::thread m_Worker;
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::optional<Request_t> m_Pending; // under m_Mutex
	bool m_bQuit = false;               // under m_Mutex
	std::atomic<bool> m_bCancel{ false };
	std::atomic<int> m_nGeneration{ 0 };

	// The main thread's.
	std::shared_ptr<const xv::map::CLighting> m_pLighting;
	bool m_bBaking = false;
	int m_nPercent = 0;
	QString m_szStage;
	double m_flBakeMs = 0.0;
};
