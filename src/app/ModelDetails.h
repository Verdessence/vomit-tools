//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Debounced, read-only model browser details with a private loading thread and live view.
//===========================================================================//
#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <QWidget>
#include "model/Model.h"
#include "vfs/Vfs.h"
class CModelView;
class QLabel;
class QComboBox;
class QTimer;
class QVBoxLayout;
struct ModelDetailsData_t;
class CModelDetails : public QWidget
{
	Q_OBJECT
public:
	using Location_t = std::function<QString( std::string_view )>;
	explicit CModelDetails( QWidget *pParent = nullptr );
	~CModelDetails() override;
	void SetGame( const xv::vfs::CGameInfo &game, xv::model::Reader_t pfnRead = {}, Location_t pfnWhere = {} );
	void SelectModel( const QString &szPath );
	[[nodiscard]] const QString &GetPath() const { return m_szPath; }
	[[nodiscard]] bool IsLoading() const { return m_bLoading; }
	[[nodiscard]] int GetLoadCount() const { return m_nLoads; }
	[[nodiscard]] CModelView *GetView() const { return m_pView; }
	[[nodiscard]] QString GetWarnings() const;
	[[nodiscard]] QStringList GetMaterials() const;
signals:
	void OpenModelRequested( const QString &szPath );
	void OpenMaterialRequested( const QString &szPath );
	void ShowInGameBrowser( const QString &szPath );
	void Loaded( const QString &szPath );

protected:
	void showEvent( QShowEvent *pEvent ) override;
	void hideEvent( QHideEvent *pEvent ) override;

private:
	void StartLoad();
	void Run();
	void Present( std::shared_ptr<ModelDetailsData_t> pData, int nGeneration );
	void Clear();
	xv::vfs::CGameInfo m_Game;
	xv::model::Reader_t m_pfnRead;
	Location_t m_pfnWhere;
	QString m_szPath;
	CModelView *m_pView = nullptr;
	QLabel *m_pWarning = nullptr, *m_pName = nullptr;
	QComboBox *m_pSkin = nullptr;
	QWidget *m_pBody = nullptr, *m_pFacts = nullptr;
	QVBoxLayout *m_pBodyLayout = nullptr, *m_pFactLayout = nullptr;
	QTimer *m_pDebounce = nullptr, *m_pOrbit = nullptr;
	std::shared_ptr<ModelDetailsData_t> m_pData;
	int m_nGeneration = 0, m_nLoads = 0;
	bool m_bLoading = false;
	struct Job_t
	{
		xv::vfs::CGameInfo m_Game;
		QString m_szPath, m_szLocation;
		xv::model::Selection_t m_Selection;
		std::optional<std::vector<std::byte>> m_Bytes;
		int m_nGeneration = 0;
	};
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::optional<Job_t> m_Job;
	bool m_bStop = false;
	std::thread m_Worker;
	xv::model::Selection_t m_Selection;
};
