//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Every model of the mounted game as a grid of pictures, searchable by name, material,
//          surface property or a texture it uses. A thread of its own reads each .mdl's header
//          first, so searching works straight away, then draws the pictures, the ones on screen
//          first. Pictures are kept on disk by the model's checksum, so the next time only new or
//          changed models are drawn again. The thread mounts the game for itself, so nothing the
//          rest of the window does with its files can change what it reads under it.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "vfs/Vfs.h"
#include "ModelDetails.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListView;
class QSlider;
class CModelList;
class CModelFilter;
class QSplitter;

// One model as the browser knows it.
struct BrowsedModel_t
{
	QString m_szPath;     // "models/props_c17/oildrum001.mdl"
	QString m_szSurface;  // $surfaceprop
	QStringList m_Textures; // as the model names them ("oildrum001")
	QStringList m_Folders;  // $cdmaterials ("models/props_c17/")
	int m_nChecksum = 0;
	int m_nBones = 0;
	int m_nSequences = 0;
	bool m_bHasMesh = false;
	bool m_bRead = false;     // the header has been read
	bool m_bPictured = false; // a picture was drawn, or tried and failed
	int m_nTriangles = -1;
	QImage m_Picture;
	QString m_szProblem;
};

class CModelBrowser : public QWidget
{
	Q_OBJECT

public:
	enum class Search_t
	{
		SEARCH_ANYTHING,
		SEARCH_NAME,
		SEARCH_MATERIAL,
		SEARCH_SURFACE
	};

	explicit CModelBrowser( const xv::vfs::CGameInfo &game, QWidget *pParent = nullptr );
	~CModelBrowser() override;

	// Reads the game again (after it was mounted again, or another game was opened).
	void SetGame( const xv::vfs::CGameInfo &game );
	void SetDetailsReader( xv::model::Reader_t pfnRead, CModelDetails::Location_t pfnWhere );
	[[nodiscard]] CModelDetails *GetDetails() const { return m_pDetails; }
	// Search text and where to look; all the words must match.
	void SetSearch( const QString &szText, Search_t nWhere );

	[[nodiscard]] const std::vector<BrowsedModel_t> &GetModels() const;
	// Models shown with the search as it is.
	[[nodiscard]] QStringList GetShown() const;
	// Every header is read and every model has a picture (or failed to).
	[[nodiscard]] bool IsFinished() const;
	// The folder the pictures are kept in.
	[[nodiscard]] static QString CacheFolder();

signals:
	void OpenModelRequested( const QString &szPath );
	void OpenMaterialRequested( const QString &szPath );
	void ShowInGameBrowser( const QString &szPath );
	// Whenever the count of read headers or drawn pictures changes.
	void Progress();

private:
	void Start( const xv::vfs::CGameInfo &game );
	void Stop();
	void Run( xv::vfs::CGameInfo game );
	void AskForVisible();
	void UpdateCount();

	xv::vfs::CGameInfo m_Game;
	xv::model::Reader_t m_pfnDetailsRead;
	CModelDetails::Location_t m_pfnWhere;
	CModelDetails *m_pDetails = nullptr;
	QSplitter *m_pSplitter = nullptr;
	CModelList *m_pList = nullptr;
	CModelFilter *m_pFilter = nullptr;
	QListView *m_pView = nullptr;
	QLineEdit *m_pSearch = nullptr;
	QComboBox *m_pWhere = nullptr;
	QCheckBox *m_pWithoutMesh = nullptr;
	QSlider *m_pSize = nullptr;
	QLabel *m_pCount = nullptr;

	std::thread m_Worker;
	std::atomic<bool> m_bStop = false;
	std::atomic<bool> m_bFinished = false;
	std::atomic<int> m_nGeneration = 0; // which start the thread's calls belong to
	std::mutex m_Mutex;
	std::deque<int> m_Wanted; // rows to draw first, guarded by m_Mutex
};
