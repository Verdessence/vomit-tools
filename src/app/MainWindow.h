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

#include "ChoiceBoxes.h"
#include "ParticleEditor.h"
#include "SoundIndex.h"
#include "audio/Audio.h"
#include "material/Material.h"
#include "sky/Sky.h"
#include "sound/SoundSchema.h"
#include "vfs/Steam.h"
#include "vfs/Vfs.h"

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
	// Shows the search over every particle system, with query typed in.
	CParticleSearch *FindParticleSystem( const QString &szQuery = {} );
	// Edit > Preferences: one window, raised again when it is already open.
	CPreferencesDialog *ShowPreferences();
	// Closes a tab, asking first about unsaved changes. Returns false when the user keeps it.
	bool CloseTab( int nIndex );
	// Lists a particle file in a map's manifest (maps/<map>_particles.txt), written to the write
	// folder unless it is a loose file already. Returns false (after saying why) when it isn't.
	bool AddToMapManifest( CParticleDocument *pDocument, const QString &szMap );

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
	void PreviewGameFile( const QString &szPath, const QString &szSource );
	void PreviewDiskFile( const QString &szPath );
	void LinkMaterial( QWidget *pView, const xv::material::Reader_t &pfnRead, const QString &szMaterial, const std::function<void()> &pfnBack );
	[[nodiscard]] QWidget *WithEditButton( QWidget *pView, const QString &szText, const QString &szButton, const std::function<void()> &pfnOpen );
	void NewSoundFile( xv::sound::FileKind_t nKind );
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
