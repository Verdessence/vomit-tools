//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Main application window, file routing and editor lifecycle.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <QMainWindow>
#include <QPointer>

#include "ChoiceBoxes.h"
#include "ManifestDialog.h"
#include "ParticleEditor.h"
#include "SoundIndex.h"
#include "audio/Audio.h"
#include "material/Material.h"
#include "sky/Sky.h"
#include "sound/SoundSchema.h"
#include "vfs/Steam.h"
#include "vfs/Vfs.h"

class CMapEditor;
class CModelBrowser;
class CModelEditor;
class CDetailDocument;
class CDetailEditor;
class CFolderBrowser;
class CParticleDocument;
class CParticleSearch;
class CPreferencesDialog;
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
class CMaterialEditor;
class CTextureDocument;
class CTextureEditor;
class CVfsModel;
namespace xv::bsp
{
class CBsp;
}
namespace xv::vfs
{
class CPackage;
}
class CVpkEditor;

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
	CModelEditor *OpenModelFile( const QString &szPath );
	// Opens a Hammer map (.vmf), from disk (an absolute path) or the mounted game, in the map
	// viewer; the tab it already has, if open.
	CMapEditor *OpenMapFile( const QString &szPath );
	// File > New > Map.
	CMapEditor *NewMap();
	[[nodiscard]] CMapEditor *GetCurrentMap() const;
	// The tab of every model in the game; null without a game.
	CModelBrowser *OpenModelBrowser();
	// Opens a soundscape or soundscript from the mounted game in an editor tab.
	CDetailEditor *OpenDetailFile( const QString &szGamePath );
	CDetailEditor *NewDetailFile();
	CSoundEditor *OpenSoundFile( const QString &szGamePath );
	// A new, empty skybox editor tab (or one for the given document).
	CSkyEditor *NewSky( CSkyDocument *pDocument = nullptr );
	// Opens a sky the mounted game has (its materials/skybox/<name>rt.vmt and so on).
	CSkyEditor *OpenSky( const QString &szName );
	// Opens a sky saved as files on disk (found from one of its faces with xv::sky::findSkyFiles).
	CSkyEditor *OpenSkyFiles( const xv::sky::CSkyFiles &files );
	// Opens a particle file from the mounted game ("particles/fire_01.pcf") in an editor tab.
	CParticleEditor *OpenParticleFile( const QString &szGamePath );
	// Opens a texture from the mounted game ("materials/x.vtf") in an editor tab, with the
	// material of the same name beside it unless bFindMaterial is false.
	CTextureEditor *OpenTextureFile( const QString &szGamePath, bool bFindMaterial = true );
	// Makes a new texture from pictures on disk, after asking how (File > Create VTF).
	CTextureEditor *NewTextureFromFiles( const QStringList &files );
	// Opens a material from the mounted game ("materials/x.vmt"): in the tab of the texture it
	// uses when that is found, else in a tab of its own.
	CMaterialEditor *OpenMaterialFile( const QString &szGamePath );
	// A new, unsaved material for a texture, in its editor, after asking which shader and so on.
	CMaterialEditor *MakeMaterialFor( CTextureEditor *pEditor );
	// Opens an archive on disk (a VPK, GCF...) in a tab to browse, preview and export from.
	CVpkEditor *OpenPackage( const QString &szPath );
	// Game > Manifests: the window generating one manifest, not shown yet (null without a game). For
	// a map's <map>_particles.txt szMap names the map; its entities and packed files (pMap,
	// pPacked) tick the particle files it needs.
	CManifestDialog *MakeManifestDialog( CManifestDialog::Format_t nFormat, const QString &szMap = {}, const xv::bsp::CBsp *pMap = nullptr, const xv::vfs::CPackage *pPacked = nullptr );
	// Shows that window and writes the manifest when it's saved.
	void GenerateManifest( CManifestDialog::Format_t nFormat, const QString &szMap = {}, const xv::bsp::CBsp *pMap = nullptr, const xv::vfs::CPackage *pPacked = nullptr );
	// Opens a file of an archive tab in its editor as an unsaved copy; returns that editor's tab.
	QWidget *OpenArchiveFile( CVpkEditor *pArchive, const QString &szPath );
	// Shows the search over every particle system, with query typed in.
	CParticleSearch *FindParticleSystem( const QString &szQuery = {} );
	// Edit > Preferences: one window, raised again when it is already open.
	CPreferencesDialog *ShowPreferences();
	// Closes a tab, asking first about unsaved changes. Returns false when the user keeps it.
	bool CloseTab( int nIndex );
	// Lists a particle file in a map's manifest (maps/<map>_particles.txt), written to the write
	// folder unless it is a loose file already. Returns false (after saying why) when it isn't.
	bool AddToMapManifest( CParticleDocument *pDocument, const QString &szMap );

	// What was open when the window last closed: the folder browser's folder first, then each tab,
	// one entry each (empty when reopening is turned off in Preferences).
	[[nodiscard]] static QStringList GetLastSession();
	// The name to show while an entry is reopened.
	[[nodiscard]] static QString SessionEntryName( const QString &szEntry );
	// Reopens one entry; files gone since are skipped without a word.
	void RestoreSessionEntry( const QString &szEntry );
	// After the last entry: the tab and side panel that were showing.
	void FinishRestoringSession();

protected:
	void closeEvent( QCloseEvent *pEvent ) override;
	void dragEnterEvent( QDragEnterEvent *pEvent ) override;
	void dropEvent( QDropEvent *pEvent ) override;

private:
	CDetailEditor *GetCurrentDetails() const;
	// The session entry that reopens tab nIndex; empty for tabs that can't be (new, unsaved files).
	[[nodiscard]] QString SessionEntryFor( int nIndex ) const;
	void SaveSession() const;
	// The folder picked in the side panel's tree (or the one holding the picked file); empty for none.
	[[nodiscard]] std::filesystem::path GetSelectedFolder() const;
	CDetailEditor *DetailEditorFor( const QString &szGamePath, const std::filesystem::path &disk = {} ) const;
	CDetailEditor *AddDetailEditor( CDetailDocument *pDocument );
	void SetUpDetails( CDetailEditor *pEditor );
	bool Save( CDetailEditor *pEditor );
	bool SaveAs( CDetailEditor *pEditor );
	bool WriteDetail( CDetailEditor *pEditor, const std::filesystem::path &target );
	bool ConfirmClose( CDetailEditor *pEditor );
	void BuildMenus( QAction *pSearchPathsToggle );
	void BuildPlaybackBar();
	// "VOMIT TOOLS - FREE EDITION - <version> - <OS> - MADE IN THE PHILIPPINES", bottom right.
	[[nodiscard]] QLabel *BuildFooter();
	void FillGameMenu( QMenu *pMenu );
	void FillRecentMenu( QMenu *pMenu );
	void AddMapTab( CMapEditor *pEditor );
	void FillSkyMenu( QMenu *pMenu );
	// The Map menu shows while a map tab is current.
	void UpdateMapMenu();
	QMenu *m_pMapMenu = nullptr;
	void AddRecent( const QString &szPath );
	void ChooseGameInfo();
	void ChooseFiles();
	void ChooseFolder();
	// Asks which map, then generates its <map>_particles.txt.
	void ChooseMapParticles();
	void AutoMountFor( const QString &szPath );
	CModelEditor *AddModelEditor( const QString &szPath, xv::material::Reader_t pfnRead, const QString &szOrigin = {} );
	void PreviewFile( const QModelIndex &proxyIndex );
	void PreviewGameFile( const QString &szPath, const QString &szSource );
	void PreviewDiskFile( const QString &szPath );
	// The real file behind a game path, when it's a loose file; empty for files in VPKs.
	[[nodiscard]] QString DiskPathForGame( const QString &szGamePath ) const;
	void LinkMaterial( QWidget *pView, const xv::material::Reader_t &pfnRead, const QString &szMaterial, const std::function<void()> &pfnBack );
	[[nodiscard]] QWidget *WithEditButton( QWidget *pView, const QString &szText, const QString &szButton, const std::function<void()> &pfnOpen );
	void NewSoundFile( xv::sound::FileKind_t nKind );
	bool AskAddToManifest( const QString &szText );
	bool Save( CSoundEditor *pEditor );
	bool SaveAs( CSoundEditor *pEditor );
	bool WriteDocument( CSoundEditor *pEditor, const std::filesystem::path &target );
	bool ConfirmClose( CSoundEditor *pEditor );
	bool ConfirmClose( CSkyEditor *pEditor );
	bool ConfirmClose( CParticleEditor *pEditor );
	bool ConfirmClose( CTextureEditor *pEditor );
	CTextureEditor *AddTextureEditor( CTextureDocument *pDocument, bool bFindMaterial = true );
	// Puts a material in the tab of the texture it uses (opening that), else in a tab of its own.
	CMaterialEditor *PlaceMaterial( CMaterialEditor *pMaterial );
	// What a material editor offers: the game's surface properties and texture names.
	void SetUpMaterial( CMaterialEditor *pMaterial );
	// The textures a material in a texture's tab reads: that texture as edited, then beside the
	// material's file (when it has one), then the game.
	[[nodiscard]] xv::material::Reader_t ReaderFor( CTextureEditor *pTexture, const std::filesystem::path &material );
	// Shows a material's editor, in its own tab or its texture's.
	void ShowMaterialEditor( CMaterialEditor *pEditor );
	// Puts a texture's material away, asking first about unsaved changes.
	bool CloseMaterial( CTextureEditor *pEditor );
	bool Save( CTextureEditor *pEditor );
	bool SaveAs( CTextureEditor *pEditor );
	bool WriteTexture( CTextureEditor *pEditor, const std::filesystem::path &target );
	// The Save As window, starting at start, with the game's write and materials folders, the
	// open folder and extra (the pictures' folder...) in its side list. Empty when cancelled;
	// pFilter in: the type picked first, out: the type picked.
	[[nodiscard]] QString AskSavePath( const std::filesystem::path &start, const QString &szFilters, QString *pFilter = nullptr, const std::filesystem::path &extra = {} );
	void ChooseNewTexture();
	void ChooseBatchConvert();
	CMaterialEditor *AddMaterialEditor( CMaterialEditor *pEditor );
	bool Save( CMaterialEditor *pEditor );
	bool SaveAs( CMaterialEditor *pEditor );
	bool WriteMaterial( CMaterialEditor *pEditor, const std::filesystem::path &target );
	bool ConfirmClose( CMaterialEditor *pEditor );
	bool ConfirmClose( CVpkEditor *pEditor );
	bool ConfirmClose( CModelEditor *pEditor );
	bool Save( CModelEditor *pEditor );
	bool SaveAs( CModelEditor *pEditor );
	// Writes the model's files: the changed ones where they came from, or all of them beside
	// mdl (a Save As).
	bool WriteModel( CModelEditor *pEditor, const std::filesystem::path &mdl, bool bAll );
	// The mounted game's .mdl a model's other file (.vvd, .vtx, .phy, .ani) belongs to; empty when none.
	[[nodiscard]] QString GameModelForCompanion( const QString &szPath ) const;
	// The surface properties the mounted game knows ($surfaceprop values), with what each is
	// based on.
	[[nodiscard]] std::vector<Choice_t> SurfaceProps() const;
	// The textures the game has, as materials name them (no materials/, no .vtf).
	[[nodiscard]] QStringList TextureNames() const;
	void NewParticleFile();
	CParticleEditor *AddParticleEditor( CParticleDocument *pDocument );
	bool Save( CParticleEditor *pEditor );
	// Save As also converts: binary 1 or 2, or keyvalues2 text for reading.
	bool SaveAs( CParticleEditor *pEditor );
	bool WriteParticles( CParticleEditor *pEditor, const std::filesystem::path &target );
	void AddToManifest( CParticleDocument *pDocument );
	// Asks which map, then lists the file in its manifest.
	void ChooseMapManifest( CParticleDocument *pDocument );
	// Writes a file the game reads (a manifest): in place when it's loose, else an overriding copy in
	// the write folder. Returns where it went, or nothing (after saying why) on failure.
	std::optional<std::filesystem::path> WriteGameFile( const std::string &szGamePath, std::string_view szBytes, const QString &szTitle );
	// The mounted game's particle operators, materials and manifest, collected when first needed.
	const ParticleGame_t &ParticleGame();
	// Writes the sky's VTFs and VMTs to the write folder, or to a folder the user picks.
	bool ExportSky( CSkyEditor *pEditor, bool bChooseFolder );
	void AddToManifest( CSoundDocument *pDocument );
	void Remount();
	// After saving: the files written join the mounted game without mounting all of it again.
	void FilesWritten( const std::vector<std::filesystem::path> &files );
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
	[[nodiscard]] CTextureEditor *GetCurrentTexture() const;
	[[nodiscard]] CMaterialEditor *GetCurrentMaterial() const;
	[[nodiscard]] CMaterialEditor *MaterialEditorFor( const QString &szGamePath, const std::filesystem::path &disk ) const;
	[[nodiscard]] CTextureEditor *TextureEditorFor( const QString &szGamePath ) const;
	[[nodiscard]] CTextureEditor *TextureEditorForDisk( const std::filesystem::path &file ) const;
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
	QPointer<QWidget> m_pRestoredCurrent; // the tab to show once the last session is reopened
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
	CPreferencesDialog *m_pPreferences = nullptr;
};
