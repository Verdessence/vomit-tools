//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <QMainWindow>

#include "ParticleEditor.h"
#include "SoundIndex.h"
#include "audio/Audio.h"
#include "sky/Sky.h"
#include "sound/SoundSchema.h"
#include "vfs/Steam.h"
#include "vfs/Vfs.h"

class CFolderBrowser;
class CParticleDocument;
class CParticleSearch;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QTabWidget;
class QTableWidget;
class QTreeView;
class QUndoGroup;
class CSkyDocument;
class CSkyEditor;
class CSoundDocument;
class CSoundEditor;
class CSoundPlayer;
class CVfsModel;

class CMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	// Tests pass Output::Offline so nothing is played aloud.
	explicit CMainWindow( QWidget *pParent = nullptr, xv::audio::CEngine::Output_t nAudio = xv::audio::CEngine::Output_t::OUTPUT_DEVICE );
	~CMainWindow() override;

	// Opens whatever is at path: a gameinfo.txt mounts its game, a folder opens in the folder
	// browser, a soundscape or soundscript opens in an editor, anything else in the preview.
	void OpenPath( const QString &szPath );
	// Opens several paths; the faces of one sky among them open that sky once.
	void OpenPaths( const QStringList &paths );
	// Mounts the game whose gameinfo.txt is given. Returns false (and says why) on failure.
	bool OpenGame( const QString &szGameInfoPath );
	void OpenFolder( const QString &szFolder );
	void OpenDiskFile( const QString &szPath );
	// Opens a soundscape or soundscript from the mounted game in an editor tab.
	CSoundEditor *OpenSoundFile( const QString &szGamePath );
	// A new, empty skybox editor tab (or one for the given document).
	CSkyEditor *NewSky( CSkyDocument *pDocument = nullptr );
	// Opens a sky the mounted game has (its materials/skybox/<name>rt.vmt and so on).
	CSkyEditor *OpenSky( const QString &szName );
	// Opens a sky saved as files on disk (found from one of its faces with xv::sky::findSkyFiles).
	CSkyEditor *OpenSkyFiles( const xv::sky::CSkyFiles &files );
	// Opens a particle file from the mounted game ("particles/fire_01.pcf") in an editor tab.
	CParticleEditor *OpenParticleFile( const QString &szGamePath );
	// Shows the search over every particle system, with query typed in.
	CParticleSearch *FindParticleSystem( const QString &szQuery = {} );
	// Closes a tab, asking first about unsaved changes. Returns false when the user keeps it.
	bool CloseTab( int nIndex );

protected:
	void closeEvent( QCloseEvent *pEvent ) override;
	void dragEnterEvent( QDragEnterEvent *pEvent ) override;
	void dropEvent( QDropEvent *pEvent ) override;

private:
	void BuildMenus( QAction *pSearchPathsToggle );
	void BuildPlaybackBar();
	// "VOMIT TOOLS - FREE EDITION - <version> - <OS> - MADE IN THE PHILIPPINES", bottom right.
	[[nodiscard]] QLabel *BuildFooter();
	void FillGameMenu( QMenu *pMenu );
	void FillRecentMenu( QMenu *pMenu );
	void FillSkyMenu( QMenu *pMenu );
	void AddRecent( const QString &szPath );
	void ChooseGameInfo();
	void ChooseFiles();
	void ChooseFolder();
	void AutoMountFor( const QString &szPath );
	void PreviewFile( const QModelIndex &proxyIndex );
	void PreviewDiskFile( const QString &szPath );
	[[nodiscard]] QWidget *WithEditButton( QWidget *pView, const QString &szText, const QString &szButton, const std::function<void()> &pfnOpen );
	void NewSoundFile( xv::sound::FileKind_t nKind );
	bool Save( CSoundEditor *pEditor );
	bool SaveAs( CSoundEditor *pEditor );
	bool WriteDocument( CSoundEditor *pEditor, const std::filesystem::path &target );
	bool ConfirmClose( CSoundEditor *pEditor );
	bool ConfirmClose( CSkyEditor *pEditor );
	bool ConfirmClose( CParticleEditor *pEditor );
	void NewParticleFile();
	CParticleEditor *AddParticleEditor( CParticleDocument *pDocument );
	bool Save( CParticleEditor *pEditor );
	// Save As also converts: binary 1 or 2, or keyvalues2 text for reading.
	bool SaveAs( CParticleEditor *pEditor );
	bool WriteParticles( CParticleEditor *pEditor, const std::filesystem::path &target );
	void AddToManifest( CParticleDocument *pDocument );
	// Writes a file the game reads (a manifest): in place when it's loose, else an overriding copy in
	// the write folder. Returns where it went, or nothing (after saying why) on failure.
	std::optional<std::filesystem::path> WriteGameFile( const std::string &szGamePath, std::string_view szBytes, const QString &szTitle );
	// The mounted game's particle operators, materials and manifest, collected when first needed.
	const ParticleGame_t &ParticleGame();
	// Writes the sky's VTFs and VMTs to the write folder, or to a folder the user picks.
	bool ExportSky( CSkyEditor *pEditor, bool bChooseFolder );
	void AddToManifest( CSoundDocument *pDocument );
	void Remount();
	void ChooseWriteFolder();
	[[nodiscard]] std::filesystem::path GetWriteFolder() const;
	[[nodiscard]] std::filesystem::path WriteTarget( const QString &szGamePath ) const;
	// Where a file on disk sits in the game ("scripts/soundscapes_x.txt"), as far as can be told.
	[[nodiscard]] QString GamePathForDisk( const std::filesystem::path &file ) const;
	void ShowMounts();
	void UpdateTitle();
	void UpdateGameLabel();
	[[nodiscard]] CSoundEditor *GetCurrentEditor() const;
	[[nodiscard]] CSkyEditor *GetCurrentSky() const;
	[[nodiscard]] CParticleEditor *GetCurrentParticles() const;
	[[nodiscard]] CParticleEditor *ParticleEditorFor( const QString &szGamePath ) const;
	[[nodiscard]] CParticleEditor *ParticleEditorForDisk( const std::filesystem::path &file ) const;
	[[nodiscard]] CSoundEditor *EditorFor( const QString &szGamePath ) const;
	[[nodiscard]] CSoundEditor *EditorForDisk( const std::filesystem::path &file ) const;
	void SetPreview( QWidget *pView );
	CSoundEditor *AddEditor( CSoundDocument *pDocument );

	std::optional<xv::vfs::CGameInfo> m_Game;
	std::unique_ptr<xv::vfs::CFileSystem> m_pFileSystem;
	CSoundIndex m_SoundIndex;
	CSoundPlayer *m_pPlayer = nullptr;
	CVfsModel *m_pModel = nullptr;
	QSortFilterProxyModel *m_pFilter = nullptr;
	QTreeView *m_pTree = nullptr;
	QTabWidget *m_pSides = nullptr;
	QWidget *m_pGameBrowser = nullptr;
	CFolderBrowser *m_pFolders = nullptr;
	QTabWidget *m_pTabs = nullptr;
	QWidget *m_pPreviewHost = nullptr;
	QTableWidget *m_pMounts = nullptr;
	QLabel *m_pGameLabel = nullptr;
	QLabel *m_pNowPlaying = nullptr;
	QUndoGroup *m_pUndoGroup = nullptr;
	std::optional<std::vector<xv::vfs::SteamGame_t>> m_SteamGames;
	std::optional<ParticleGame_t> m_ParticleGame;
	CParticleSearch *m_pParticleSearch = nullptr;
};
