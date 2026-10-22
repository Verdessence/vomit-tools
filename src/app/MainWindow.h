// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <QMainWindow>

#include "SoundIndex.h"
#include "audio/Audio.h"
#include "sky/Sky.h"
#include "sound/SoundSchema.h"
#include "vfs/Steam.h"
#include "vfs/Vfs.h"

class FolderBrowser;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QTabWidget;
class QTableWidget;
class QTreeView;
class QUndoGroup;
class SkyDocument;
class SkyEditor;
class SoundDocument;
class SoundEditor;
class SoundPlayer;
class VfsModel;

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	// Tests pass Output::Offline so nothing is played aloud.
	explicit MainWindow(QWidget* parent = nullptr, xv::audio::Engine::Output audio = xv::audio::Engine::Output::Device);
	~MainWindow() override;

	// Opens whatever is at path: a gameinfo.txt mounts its game, a folder opens in the folder
	// browser, a soundscape or soundscript opens in an editor, anything else in the preview.
	void openPath(const QString& path);
	// Opens several paths; the faces of one sky among them open that sky once.
	void openPaths(const QStringList& paths);
	// Mounts the game whose gameinfo.txt is given. Returns false (and says why) on failure.
	bool openGame(const QString& gameInfoPath);
	void openFolder(const QString& folder);
	void openDiskFile(const QString& path);
	// Opens a soundscape or soundscript from the mounted game in an editor tab.
	SoundEditor* openSoundFile(const QString& gamePath);
	// A new, empty skybox editor tab (or one for the given document).
	SkyEditor* newSky(SkyDocument* document = nullptr);
	// Opens a sky the mounted game has (its materials/skybox/<name>rt.vmt and so on).
	SkyEditor* openSky(const QString& name);
	// Opens a sky saved as files on disk (found from one of its faces with xv::sky::findSkyFiles).
	SkyEditor* openSkyFiles(const xv::sky::SkyFiles& files);

protected:
	void closeEvent(QCloseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	void buildMenus(QAction* searchPathsToggle);
	void buildPlaybackBar();
	void fillGameMenu(QMenu* menu);
	void fillRecentMenu(QMenu* menu);
	void fillSkyMenu(QMenu* menu);
	void addRecent(const QString& path);
	void chooseGameInfo();
	void chooseFiles();
	void chooseFolder();
	void autoMountFor(const QString& path);
	void previewFile(const QModelIndex& proxyIndex);
	void previewDiskFile(const QString& path);
	[[nodiscard]] QWidget* withEditButton(QWidget* view, const QString& text, const QString& button, const std::function<void()>& open);
	void newSoundFile(xv::sound::FileKind kind);
	bool save(SoundEditor* editor);
	bool saveAs(SoundEditor* editor);
	bool writeDocument(SoundEditor* editor, const std::filesystem::path& target);
	bool closeTab(int index);
	bool confirmClose(SoundEditor* editor);
	bool confirmClose(SkyEditor* editor);
	// Writes the sky's VTFs and VMTs to the write folder, or to a folder the user picks.
	bool exportSky(SkyEditor* editor, bool chooseFolder);
	void addToManifest(SoundDocument* document);
	void remount();
	void chooseWriteFolder();
	[[nodiscard]] std::filesystem::path writeFolder() const;
	[[nodiscard]] std::filesystem::path writeTarget(const QString& gamePath) const;
	// Where a file on disk sits in the game ("scripts/soundscapes_x.txt"), as far as can be told.
	[[nodiscard]] QString gamePathForDisk(const std::filesystem::path& file) const;
	void showMounts();
	void updateTitle();
	void updateGameLabel();
	[[nodiscard]] SoundEditor* currentEditor() const;
	[[nodiscard]] SkyEditor* currentSky() const;
	[[nodiscard]] SoundEditor* editorFor(const QString& gamePath) const;
	[[nodiscard]] SoundEditor* editorForDisk(const std::filesystem::path& file) const;
	void setPreview(QWidget* view);
	SoundEditor* addEditor(SoundDocument* document);

	std::optional<xv::vfs::GameInfo> game_;
	std::unique_ptr<xv::vfs::FileSystem> fileSystem_;
	SoundIndex soundIndex_;
	SoundPlayer* player_ = nullptr;
	VfsModel* model_ = nullptr;
	QSortFilterProxyModel* filter_ = nullptr;
	QTreeView* tree_ = nullptr;
	QTabWidget* sides_ = nullptr;
	QWidget* gameBrowser_ = nullptr;
	FolderBrowser* folders_ = nullptr;
	QTabWidget* tabs_ = nullptr;
	QWidget* previewHost_ = nullptr;
	QTableWidget* mounts_ = nullptr;
	QLabel* gameLabel_ = nullptr;
	QLabel* nowPlaying_ = nullptr;
	QUndoGroup* undoGroup_ = nullptr;
	std::optional<std::vector<xv::vfs::SteamGame>> steamGames_;
};
