// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeView>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include "FileViews.h"
#include "FolderBrowser.h"
#include "KvCard.h"
#include "QtPaths.h"
#include "SoundDocument.h"
#include "SoundEditor.h"
#include "SoundPlayer.h"
#include "VfsModel.h"
#include "sound/SoundLint.h"

namespace fs = std::filesystem;
using xv::sound::FileKind;

namespace {

constexpr auto LAST_GAME_KEY = "lastGameInfo";
constexpr auto RECENT_KEY = "recentPaths";
constexpr auto VOLUME_KEY = "volume";
constexpr int RECENT_LIMIT = 12;

QString writeFolderKey(const fs::path& gameInfo) {
	const QByteArray hash = QCryptographicHash::hash(toQString(gameInfo).toUtf8(), QCryptographicHash::Md5).toHex();
	return QStringLiteral("writeFolders/") + QString::fromLatin1(hash);
}

QWidget* placeholder(const QString& text) {
	auto* label = new QLabel(text);
	label->setAlignment(Qt::AlignCenter);
	label->setWordWrap(true);
	return label;
}

// Where Steam may be installed on this platform.
std::vector<fs::path> steamRoots() {
	std::vector<fs::path> roots;
#if defined(Q_OS_WIN)
	const QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
	if (const QString path = registry.value(QStringLiteral("SteamPath")).toString(); !path.isEmpty()) {
		roots.push_back(toPath(path));
	}
	roots.push_back(toPath(QStringLiteral("C:/Program Files (x86)/Steam")));
#elif defined(Q_OS_MACOS)
	roots.push_back(toPath(QDir::homePath() + QStringLiteral("/Library/Application Support/Steam")));
#else
	roots.push_back(toPath(QDir::homePath() + QStringLiteral("/.local/share/Steam")));
	roots.push_back(toPath(QDir::homePath() + QStringLiteral("/.steam/steam")));
	roots.push_back(toPath(QDir::homePath() + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam")));
#endif
	std::vector<fs::path> existing;
	for (const fs::path& root : roots) {
		std::error_code ec;
		if (fs::is_directory(root / "steamapps", ec)) {
			existing.push_back(root);
		}
	}
	return existing;
}

QString kindName(FileKind kind) {
	return kind == FileKind::Soundscape ? QObject::tr("soundscape") : QObject::tr("soundscript");
}

// The gameinfo.txt of the game a file or folder belongs to: in it, above it, or (for a game's
// install folder) one level below it.
QString findGameInfo(const QString& path) {
	const QFileInfo info{path};
	QDir dir = info.isDir() ? QDir{info.absoluteFilePath()} : info.absoluteDir();
	if (info.isDir()) {
		QStringList found;
		for (const QFileInfo& child : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			if (QFileInfo::exists(child.absoluteFilePath() + QStringLiteral("/gameinfo.txt"))) {
				found << child.absoluteFilePath() + QStringLiteral("/gameinfo.txt");
			}
		}
		if (found.size() == 1 && !QFileInfo::exists(dir.filePath(QStringLiteral("gameinfo.txt")))) {
			return found.front();
		}
	}
	for (int depth = 0; depth < 8; depth++) {
		if (QFileInfo::exists(dir.filePath(QStringLiteral("gameinfo.txt")))) {
			return dir.filePath(QStringLiteral("gameinfo.txt"));
		}
		if (!dir.cdUp()) {
			break;
		}
	}
	return {};
}

std::optional<QByteArray> readDiskFile(const QString& path) {
	QFile file{path};
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	return file.readAll();
}

std::span<const std::byte> asBytes(const QByteArray& data) {
	return {reinterpret_cast<const std::byte*>(data.constData()), static_cast<std::size_t>(data.size())};
}

} // namespace

MainWindow::MainWindow(QWidget* parent, xv::audio::Engine::Output audio)
		: QMainWindow(parent)
		, fileSystem_(std::make_unique<xv::vfs::FileSystem>())
		, model_(new VfsModel(this))
		, filter_(new QSortFilterProxyModel(this))
		, undoGroup_(new QUndoGroup(this)) {
	setWindowTitle(QStringLiteral("vomit-tools"));
	resize(1400, 900);
	setAcceptDrops(true);
	player_ = new SoundPlayer(*fileSystem_, this, audio);

	filter_->setSourceModel(model_);
	filter_->setRecursiveFilteringEnabled(true);
	filter_->setFilterCaseSensitivity(Qt::CaseInsensitive);
	filter_->setFilterKeyColumn(VfsModel::NameColumn);

	auto* search = new QLineEdit;
	search->setPlaceholderText(tr("Filter files…"));
	search->setClearButtonEnabled(true);
	connect(search, &QLineEdit::textChanged, filter_, &QSortFilterProxyModel::setFilterFixedString);

	tree_ = new QTreeView;
	tree_->setModel(filter_);
	tree_->setUniformRowHeights(true);
	tree_->header()->setSectionResizeMode(VfsModel::NameColumn, QHeaderView::Stretch);
	tree_->header()->setStretchLastSection(false);
	tree_->setToolTip(tr("The game's files as the engine sees them. Double-click a soundscape or soundscript to edit it"));
	connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::previewFile);
	connect(tree_, &QTreeView::activated, this, [this](const QModelIndex& proxyIndex) {
		const QString path = filter_->mapToSource(proxyIndex).data(VfsModel::PathRole).toString();
		if (xv::sound::fileKindForPath(path.toStdString()) != FileKind::Unknown) {
			openSoundFile(path);
		}
	});

	gameBrowser_ = new QWidget;
	auto* browserLayout = new QVBoxLayout(gameBrowser_);
	browserLayout->setContentsMargins(0, 0, 0, 0);
	browserLayout->addWidget(search);
	browserLayout->addWidget(tree_);

	folders_ = new FolderBrowser;
	connect(folders_, &FolderBrowser::openFolderRequested, this, &MainWindow::chooseFolder);
	connect(folders_, &FolderBrowser::fileSelected, this, &MainWindow::previewDiskFile);
	connect(folders_, &FolderBrowser::fileActivated, this, &MainWindow::openDiskFile);

	sides_ = new QTabWidget;
	sides_->setDocumentMode(true);
	sides_->addTab(gameBrowser_, tr("Game"));
	sides_->addTab(folders_, tr("Folder"));

	previewHost_ = new QWidget;
	auto* previewLayout = new QVBoxLayout(previewHost_);
	previewLayout->setContentsMargins(0, 0, 0, 0);
	previewLayout->addWidget(placeholder(tr("Open a game, a folder or a file from the File menu, or drop files and folders here.\n\n"
	                                        "Soundscapes (soundscapes_*.txt) and soundscripts (game_sounds_*.txt) open in the editor; "
	                                        "sounds, particles and other files show here.")));

	tabs_ = new QTabWidget;
	tabs_->setDocumentMode(true);
	tabs_->setTabsClosable(true);
	tabs_->addTab(previewHost_, tr("Preview"));
	tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
	tabs_->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
	connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
	connect(tabs_, &QTabWidget::currentChanged, this, [this] {
		SoundEditor* editor = currentEditor();
		undoGroup_->setActiveStack(editor ? editor->document()->undoStack() : nullptr);
		updateTitle();
	});

	auto* splitter = new QSplitter;
	splitter->addWidget(sides_);
	splitter->addWidget(tabs_);
	splitter->setStretchFactor(1, 3);
	splitter->setSizes({320, 1080});
	setCentralWidget(splitter);

	mounts_ = new QTableWidget(0, 4);
	mounts_->setHorizontalHeaderLabels({tr("Status"), tr("Search path"), tr("Path IDs"), tr("Mounted from")});
	mounts_->horizontalHeader()->setStretchLastSection(true);
	mounts_->verticalHeader()->setVisible(false);
	mounts_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	auto* mountsDock = new QDockWidget(tr("Search Paths"), this);
	mountsDock->setObjectName(QStringLiteral("searchPathsDock"));
	mountsDock->setWidget(mounts_);
	addDockWidget(Qt::BottomDockWidgetArea, mountsDock);
	mountsDock->hide();

	buildPlaybackBar();
	gameLabel_ = new QLabel;
	statusBar()->addPermanentWidget(gameLabel_);
	updateGameLabel();

	buildMenus();
	menuBar()->actions().constLast()->menu()->addAction(mountsDock->toggleViewAction());
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus() {
	auto* file = menuBar()->addMenu(tr("&File"));
	file->addAction(tr("&Open File…"), QKeySequence::Open, this, &MainWindow::chooseFiles);
	file->addAction(tr("Open &Folder…"), QKeySequence{Qt::CTRL | Qt::SHIFT | Qt::Key_O}, this, &MainWindow::chooseFolder);
	auto* games = file->addMenu(tr("Open &Game"));
	connect(games, &QMenu::aboutToShow, this, [this, games] { fillGameMenu(games); });
	auto* recent = file->addMenu(tr("Open &Recent"));
	connect(recent, &QMenu::aboutToShow, this, [this, recent] { fillRecentMenu(recent); });
	file->addSeparator();
	file->addAction(tr("New &Soundscape File…"), this, [this] { newSoundFile(FileKind::Soundscape); });
	file->addAction(tr("New Sound&script File…"), this, [this] { newSoundFile(FileKind::Soundscript); });
	file->addSeparator();
	file->addAction(tr("&Save"), QKeySequence::Save, this, [this] {
		if (SoundEditor* editor = currentEditor()) {
			save(editor);
		}
	});
	file->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, [this] {
		if (SoundEditor* editor = currentEditor()) {
			saveAs(editor);
		}
	});
	file->addAction(tr("&Close Tab"), QKeySequence::Close, this, [this] {
		if (currentEditor()) {
			closeTab(tabs_->currentIndex());
		}
	});
	file->addSeparator();
	file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

	auto* edit = menuBar()->addMenu(tr("&Edit"));
	QAction* undo = undoGroup_->createUndoAction(this, tr("&Undo"));
	undo->setShortcut(QKeySequence::Undo);
	QAction* redo = undoGroup_->createRedoAction(this, tr("&Redo"));
	redo->setShortcut(QKeySequence::Redo);
	edit->addAction(undo);
	edit->addAction(redo);

	auto* game = menuBar()->addMenu(tr("&Game"));
	game->addAction(tr("&Reload Files"), QKeySequence::Refresh, this, &MainWindow::remount);
	game->addAction(tr("Set &Write Folder…"), this, &MainWindow::chooseWriteFolder);
	game->addAction(tr("&Open Write Folder"), this, [this] {
		if (game_) {
			std::error_code ec;
			fs::create_directories(writeFolder(), ec);
			QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(writeFolder())));
		}
	});
	game->addSeparator();
}

void MainWindow::buildPlaybackBar() {
	auto* stop = new QToolButton;
	stop->setText(QStringLiteral("■"));
	stop->setToolTip(tr("Stop all sounds"));
	stop->setAutoRaise(true);
	stop->setEnabled(false);
	connect(stop, &QToolButton::clicked, player_, &SoundPlayer::stop);

	auto* volume = new QSlider(Qt::Horizontal);
	volume->setRange(0, 100);
	volume->setFixedWidth(90);
	volume->setToolTip(tr("Preview volume"));
	volume->setValue(QSettings{}.value(VOLUME_KEY, 80).toInt());
	player_->setVolume(static_cast<float>(volume->value()) / 100.0f);
	connect(volume, &QSlider::valueChanged, this, [this](int value) {
		player_->setVolume(static_cast<float>(value) / 100.0f);
		QSettings{}.setValue(VOLUME_KEY, value);
	});

	nowPlaying_ = new QLabel;
	nowPlaying_->setFixedWidth(360);

	auto* bar = new QWidget;
	auto* layout = new QHBoxLayout(bar);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(stop);
	layout->addWidget(volume);
	layout->addWidget(nowPlaying_, 1);
	// Permanent, so status messages show beside it instead of on top of it.
	statusBar()->addPermanentWidget(bar);

	connect(player_, &SoundPlayer::started, this, [this, stop](const QString& description) {
		stop->setEnabled(true);
		const QString text = QStringLiteral("♪ ") + description;
		nowPlaying_->setText(nowPlaying_->fontMetrics().elidedText(text, Qt::ElideMiddle, nowPlaying_->width()));
		nowPlaying_->setToolTip(description);
	});
	connect(player_, &SoundPlayer::stopped, this, [this, stop] {
		stop->setEnabled(false);
		nowPlaying_->clear();
	});
	connect(player_, &SoundPlayer::failed, this, [this](const QString& message) {
		statusBar()->showMessage(message, 6000);
	});
}

void MainWindow::fillGameMenu(QMenu* menu) {
	menu->clear();
	if (!steamGames_) {
		std::vector<fs::path> libraries;
		for (const fs::path& root : steamRoots()) {
			for (const fs::path& library : xv::vfs::steamLibraries(root)) {
				if (std::ranges::find(libraries, library) == libraries.end()) {
					libraries.push_back(library);
				}
			}
		}
		steamGames_ = xv::vfs::findSourceGames(libraries);
	}
	if (steamGames_->empty()) {
		menu->addAction(tr("No Source games found in Steam libraries"))->setEnabled(false);
	}
	for (const auto& found : *steamGames_) {
		const QString path = toQString(found.gameInfo);
		QAction* action = menu->addAction(QStringLiteral("%1  (%2)").arg(QString::fromStdString(found.name), toQString(found.gameInfo.parent_path().filename())),
		                                  this, [this, path] { openGame(path); });
		action->setToolTip(path);
	}
	menu->setToolTipsVisible(true);
	menu->addSeparator();
	menu->addAction(tr("&Browse for gameinfo.txt…"), this, &MainWindow::chooseGameInfo);
}

void MainWindow::fillRecentMenu(QMenu* menu) {
	menu->clear();
	const QStringList recent = QSettings{}.value(RECENT_KEY).toStringList();
	if (recent.isEmpty()) {
		menu->addAction(tr("Nothing yet"))->setEnabled(false);
		return;
	}
	for (const QString& path : recent) {
		const QFileInfo info{path};
		const QString label = info.fileName().compare(u"gameinfo.txt", Qt::CaseInsensitive) == 0
			? tr("%1 (game)").arg(QDir::toNativeSeparators(info.absolutePath()))
			: QDir::toNativeSeparators(path);
		QAction* action = menu->addAction(label, this, [this, path] { openPath(path); });
		action->setEnabled(info.exists());
	}
	menu->addSeparator();
	menu->addAction(tr("Clear"), this, [] { QSettings{}.remove(RECENT_KEY); });
}

void MainWindow::addRecent(const QString& path) {
	QSettings settings;
	QStringList recent = settings.value(RECENT_KEY).toStringList();
	const QString clean = QDir::cleanPath(QFileInfo{path}.absoluteFilePath());
	recent.removeAll(clean);
	recent.prepend(clean);
	while (recent.size() > RECENT_LIMIT) {
		recent.removeLast();
	}
	settings.setValue(RECENT_KEY, recent);
}

void MainWindow::chooseGameInfo() {
	const QString start = QSettings{}.value(LAST_GAME_KEY).toString();
	const QString file = QFileDialog::getOpenFileName(this, tr("Open gameinfo.txt"), start, tr("Game info (gameinfo.txt);;All files (*)"));
	if (!file.isEmpty()) {
		openGame(file);
	}
}

void MainWindow::chooseFiles() {
	const QString start = folders_->folder().isEmpty() && game_ ? toQString(game_->gameDir) : folders_->folder();
	const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open File"), start,
		tr("Soundscapes and soundscripts (*.txt);;Sounds (*.wav *.mp3);;Particles (*.pcf *.dmx);;"
		   "KeyValues (*.txt *.vmt *.vdf *.res);;Game info (gameinfo.txt);;All files (*)"));
	for (const QString& file : files) {
		openPath(file);
	}
}

void MainWindow::chooseFolder() {
	const QString start = !folders_->folder().isEmpty() ? folders_->folder() : game_ ? toQString(game_->gameDir) : QDir::homePath();
	const QString folder = QFileDialog::getExistingDirectory(this, tr("Open Folder"), start);
	if (!folder.isEmpty()) {
		openFolder(folder);
	}
}

void MainWindow::openPath(const QString& path) {
	const QFileInfo info{path};
	if (!info.exists()) {
		QMessageBox::warning(this, tr("Not found"), tr("%1 does not exist.").arg(QDir::toNativeSeparators(path)));
		return;
	}
	if (info.isDir()) {
		openFolder(path);
	} else if (info.fileName().compare(u"gameinfo.txt", Qt::CaseInsensitive) == 0) {
		openGame(path);
	} else {
		openDiskFile(path);
	}
}

// Files opened from disk still get the game's context (sound lookups, lint, manifests) when they
// belong to a game and none is mounted yet.
void MainWindow::autoMountFor(const QString& path) {
	if (game_) {
		return;
	}
	const QString gameInfo = findGameInfo(path);
	if (!gameInfo.isEmpty() && openGame(gameInfo)) {
		statusBar()->showMessage(tr("Mounted %1 from %2").arg(QString::fromStdString(game_->game), QDir::toNativeSeparators(gameInfo)), 8000);
	}
}

void MainWindow::openFolder(const QString& folder) {
	folders_->setFolder(folder);
	sides_->setCurrentWidget(folders_);
	addRecent(folder);
	autoMountFor(folder);
}

bool MainWindow::openGame(const QString& gameInfoPath) {
	std::string error;
	auto info = xv::vfs::GameInfo::load(toPath(gameInfoPath), &error);
	if (!info) {
		QMessageBox::warning(this, tr("Cannot open game"), tr("%1\n\n%2").arg(gameInfoPath, QString::fromStdString(error)));
		return false;
	}
	// Files opened from a VPK belong to the game they came from; files on disk stay open.
	if (game_ && game_->file != info->file) {
		for (int i = tabs_->count() - 1; i >= 0; i--) {
			auto* editor = qobject_cast<SoundEditor*>(tabs_->widget(i));
			if (editor && editor->document()->diskPath().empty() && !editor->document()->isNew() && !closeTab(i)) {
				return false;
			}
		}
	}
	game_ = std::move(info);
	QSettings{}.setValue(LAST_GAME_KEY, gameInfoPath);
	addRecent(gameInfoPath);
	remount();
	sides_->setCurrentWidget(gameBrowser_);
	return true;
}

void MainWindow::remount() {
	if (!game_) {
		return;
	}
	const QString selected = filter_->mapToSource(tree_->currentIndex()).data(VfsModel::PathRole).toString();

	QApplication::setOverrideCursor(Qt::WaitCursor);
	fileSystem_->mount(*game_);
	model_->rebuild(*fileSystem_);
	soundIndex_.rebuild(*fileSystem_);
	QApplication::restoreOverrideCursor();

	for (int i = 0; i < tabs_->count(); i++) {
		if (auto* editor = qobject_cast<SoundEditor*>(tabs_->widget(i))) {
			editor->relint();
		}
	}
	if (!selected.isEmpty()) {
		const QModelIndex index = filter_->mapFromSource(model_->indexForPath(selected.toStdString()));
		const QSignalBlocker blocker{tree_->selectionModel()};
		tree_->setCurrentIndex(index);
		tree_->scrollTo(index);
	}
	showMounts();
	updateGameLabel();
	updateTitle();
}

void MainWindow::updateGameLabel() {
	if (!game_) {
		gameLabel_->setText(tr("No game mounted"));
		gameLabel_->setToolTip(tr("Files open without a game too; open a game for sound lookups, linting against it and manifests"));
		return;
	}
	gameLabel_->setText(tr("%1 · %2 files · writing to %3")
		.arg(QString::fromStdString(game_->game))
		.arg(model_->fileCount())
		.arg(toQString(writeFolder())));
	gameLabel_->setToolTip(toQString(game_->file));
}

void MainWindow::showMounts() {
	const auto& records = fileSystem_->records();
	mounts_->setRowCount(static_cast<int>(records.size()));
	for (int row = 0; row < static_cast<int>(records.size()); row++) {
		const auto& record = records[static_cast<std::size_t>(row)];
		QString status;
		switch (record.status) {
			case xv::vfs::MountRecord::Status::Mounted:
				status = tr("%n file(s)", nullptr, record.mount ? static_cast<int>(record.mount->fileCount()) : 0);
				break;
			case xv::vfs::MountRecord::Status::Missing:
				status = tr("missing");
				break;
			case xv::vfs::MountRecord::Status::Failed:
				status = tr("failed: %1").arg(QString::fromStdString(record.message));
				break;
		}
		QStringList ids;
		for (const auto& id : record.ids) {
			ids << QString::fromStdString(id);
		}
		mounts_->setItem(row, 0, new QTableWidgetItem(status));
		mounts_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(record.searchPath)));
		mounts_->setItem(row, 2, new QTableWidgetItem(ids.join(u'+')));
		mounts_->setItem(row, 3, new QTableWidgetItem(toQString(record.location)));
	}
	mounts_->resizeColumnsToContents();
}

void MainWindow::setPreview(QWidget* view) {
	QLayout* layout = previewHost_->layout();
	while (QLayoutItem* item = layout->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	layout->addWidget(view);
}

QWidget* MainWindow::withEditButton(QWidget* view, FileKind kind, const std::function<void()>& open) {
	auto* wrapper = new QWidget;
	auto* bar = new QHBoxLayout;
	bar->setContentsMargins(6, 6, 6, 0);
	bar->addWidget(new QLabel(tr("This is a %1 file.").arg(kindName(kind))));
	auto* edit = new QPushButton(tr("Open in Editor"));
	connect(edit, &QPushButton::clicked, this, open);
	bar->addWidget(edit);
	bar->addStretch();
	auto* layout = new QVBoxLayout(wrapper);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(bar);
	layout->addWidget(view);
	return wrapper;
}

void MainWindow::previewFile(const QModelIndex& proxyIndex) {
	const QModelIndex index = filter_->mapToSource(proxyIndex);
	if (!index.isValid() || index.data(VfsModel::IsDirectoryRole).toBool()) {
		return;
	}
	const QString path = index.data(VfsModel::PathRole).toString();
	const QString source = index.siblingAtColumn(VfsModel::SourceColumn).data(Qt::ToolTipRole).toString();
	const auto bytes = fileSystem_->read(path.toStdString());
	if (!bytes) {
		setPreview(placeholder(tr("Could not read %1").arg(path)));
		return;
	}
	QWidget* view = createFileView(path, source, *bytes, player_);
	if (const FileKind kind = xv::sound::fileKindForPath(path.toStdString()); kind != FileKind::Unknown) {
		view = withEditButton(view, kind, [this, path] { openSoundFile(path); });
	}
	setPreview(view);
	tabs_->setCurrentWidget(previewHost_);
}

void MainWindow::previewDiskFile(const QString& path) {
	const auto data = readDiskFile(path);
	if (!data) {
		setPreview(placeholder(tr("Could not read %1").arg(QDir::toNativeSeparators(path))));
		return;
	}
	const QFileInfo info{path};
	QWidget* view = createFileView(info.fileName(), QDir::toNativeSeparators(info.absolutePath()), asBytes(*data), player_);
	if (info.suffix().compare(u"txt", Qt::CaseInsensitive) == 0) {
		const FileKind kind = xv::sound::guessFileKind(info.fileName().toStdString(), xv::kv1::parse(data->toStdString()).document);
		if (kind != FileKind::Unknown) {
			view = withEditButton(view, kind, [this, path] { openDiskFile(path); });
		}
	}
	setPreview(view);
	tabs_->setCurrentWidget(previewHost_);
}

void MainWindow::openDiskFile(const QString& path) {
	const QFileInfo info{path};
	if (info.fileName().compare(u"gameinfo.txt", Qt::CaseInsensitive) == 0) {
		openGame(path);
		return;
	}
	autoMountFor(path);
	addRecent(path);
	const fs::path file = toPath(info.absoluteFilePath());
	if (SoundEditor* editor = editorForDisk(file)) {
		tabs_->setCurrentWidget(editor);
		return;
	}
	const auto data = readDiskFile(path);
	if (!data) {
		QMessageBox::warning(this, tr("Cannot open"), tr("Could not read %1.").arg(QDir::toNativeSeparators(path)));
		return;
	}
	if (info.suffix().compare(u"txt", Qt::CaseInsensitive) == 0) {
		auto parsed = xv::kv1::parse(data->toStdString());
		const FileKind kind = xv::sound::guessFileKind(info.fileName().toStdString(), parsed.document);
		if (kind != FileKind::Unknown) {
			auto* document = new SoundDocument(gamePathForDisk(file), kind, std::move(parsed));
			document->setDiskPath(file);
			addEditor(document);
			return;
		}
	}
	previewDiskFile(path);
}

QString MainWindow::gamePathForDisk(const fs::path& file) const {
	const fs::path normal = file.lexically_normal();
	for (const auto& record : fileSystem_->records()) {
		if (!record.mount || record.mount->kind() != xv::vfs::Mount::Kind::Directory) {
			continue;
		}
		const fs::path relative = normal.lexically_relative(record.location.lexically_normal());
		if (!relative.empty() && *relative.begin() != "..") {
			return QString::fromStdString(xv::vfs::normalizePath(xv::vfs::toUtf8(relative)));
		}
	}
	const QString folder = QString::fromStdU16String(file.parent_path().filename().u16string()).toLower();
	const QString name = QString::fromStdU16String(file.filename().u16string());
	return folder == u"scripts" || folder == u"maps" ? folder + u'/' + name : name;
}

SoundEditor* MainWindow::editorFor(const QString& gamePath) const {
	const std::string wanted = xv::vfs::normalizePath(gamePath.toStdString());
	for (int i = 0; i < tabs_->count(); i++) {
		if (auto* editor = qobject_cast<SoundEditor*>(tabs_->widget(i))) {
			if (xv::vfs::normalizePath(editor->document()->gamePath().toStdString()) == wanted) {
				return editor;
			}
		}
	}
	return nullptr;
}

SoundEditor* MainWindow::editorForDisk(const fs::path& file) const {
	for (int i = 0; i < tabs_->count(); i++) {
		if (auto* editor = qobject_cast<SoundEditor*>(tabs_->widget(i))) {
			std::error_code ec;
			const fs::path& open = editor->document()->diskPath();
			if (!open.empty() && fs::equivalent(open, file, ec)) {
				return editor;
			}
		}
	}
	return nullptr;
}

SoundEditor* MainWindow::currentEditor() const {
	return qobject_cast<SoundEditor*>(tabs_->currentWidget());
}

SoundEditor* MainWindow::openSoundFile(const QString& gamePath) {
	if (SoundEditor* editor = editorFor(gamePath)) {
		tabs_->setCurrentWidget(editor);
		return editor;
	}
	const std::string path = xv::vfs::normalizePath(gamePath.toStdString());
	const FileKind kind = xv::sound::fileKindForPath(path);
	const auto text = fileSystem_->readText(path);
	if (kind == FileKind::Unknown || !text) {
		QMessageBox::warning(this, tr("Cannot edit"), tr("%1 is not a soundscape or soundscript file in the mounted game.").arg(gamePath));
		return nullptr;
	}
	auto* document = new SoundDocument(QString::fromStdString(path), kind, xv::kv1::parse(*text));
	if (const xv::vfs::Mount* mount = fileSystem_->resolve(path)) {
		if (auto disk = mount->diskPath(path)) {
			document->setDiskPath(*disk);
		}
	}
	return addEditor(document);
}

SoundEditor* MainWindow::addEditor(SoundDocument* document) {
	auto* editor = new SoundEditor(document, soundIndex_, player_);
	document->setParent(editor);
	undoGroup_->addStack(document->undoStack());

	const int index = tabs_->addTab(editor, document->fileName());
	tabs_->setTabToolTip(index, document->diskPath().empty() ? document->gamePath() : toQString(document->diskPath()));
	const auto updateTab = [this, editor] {
		const int i = tabs_->indexOf(editor);
		const SoundDocument* doc = editor->document();
		tabs_->setTabText(i, (doc->isModified() ? QStringLiteral("*") : QString{}) + doc->fileName());
		tabs_->setTabToolTip(i, doc->diskPath().empty() ? doc->gamePath() : toQString(doc->diskPath()));
		updateTitle();
	};
	connect(document, &SoundDocument::modifiedChanged, this, updateTab);
	connect(editor, &SoundEditor::addToManifestRequested, this, &MainWindow::addToManifest);
	updateTab();
	tabs_->setCurrentWidget(editor);
	return editor;
}

void MainWindow::newSoundFile(FileKind kind) {
	const QString prefix = kind == FileKind::Soundscape ? QStringLiteral("soundscapes_") : QStringLiteral("game_sounds_");
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("New %1 File").arg(kindName(kind)),
		tr("File name (scripts/%1<name>.txt), e.g. your map's name:").arg(prefix), QLineEdit::Normal, {}, &ok).trimmed().toLower();
	if (!ok || name.isEmpty()) {
		return;
	}
	if (!QRegularExpression(QStringLiteral("^[a-z0-9_.-]+$")).match(name).hasMatch()) {
		QMessageBox::warning(this, tr("Invalid name"), tr("Use letters, digits, '_', '-' and '.' only."));
		return;
	}
	const QString gamePath = QStringLiteral("scripts/%1%2.txt").arg(prefix, name);
	if (game_ && fileSystem_->resolve(gamePath.toStdString())) {
		if (QMessageBox::question(this, tr("File exists"), tr("%1 already exists in the game. Open it instead?").arg(gamePath)) == QMessageBox::Yes) {
			openSoundFile(gamePath);
		}
		return;
	}
	auto* document = new SoundDocument(gamePath, kind, xv::kv1::parse(""));
	document->markUnsaved();
	SoundEditor* editor = addEditor(document);
	const QString entry = kind == FileKind::Soundscape ? name + QStringLiteral(".default") : name + QStringLiteral(".sound");
	document->insertEntry(0, entryTemplate(*document, entry.toStdString()), tr("New %1").arg(entry));
	editor->selectEntry(0);
}

fs::path MainWindow::writeFolder() const {
	if (!game_) {
		return {};
	}
	const QString stored = QSettings{}.value(writeFolderKey(game_->file)).toString();
	return stored.isEmpty() ? game_->gameDir / "custom" / "vomit-tools" : toPath(stored);
}

fs::path MainWindow::writeTarget(const QString& gamePath) const {
	return writeFolder() / xv::vfs::fromUtf8(gamePath.toStdString());
}

void MainWindow::chooseWriteFolder() {
	if (!game_) {
		QMessageBox::information(this, tr("No game open"), tr("The write folder belongs to a game; open one first."));
		return;
	}
	const QString folder = QFileDialog::getExistingDirectory(this, tr("Write Folder"), toQString(writeFolder()));
	if (folder.isEmpty()) {
		return;
	}
	QSettings{}.setValue(writeFolderKey(game_->file), folder);
	QMessageBox::information(this, tr("Write Folder"),
		tr("Files edited from VPKs and new files are now saved under\n%1\n\n"
		   "The game only sees them if that folder is one of its search paths, "
		   "such as a folder inside custom/. The game scans custom/ when it starts.").arg(folder));
	remount();
}

bool MainWindow::writeDocument(SoundEditor* editor, const fs::path& target) {
	SoundDocument* document = editor->document();
	std::error_code ec;
	fs::create_directories(target.parent_path(), ec);
	QSaveFile file(toQString(target));
	const std::string bytes = document->serialize();
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()) || !file.commit()) {
		QMessageBox::critical(this, tr("Save failed"), tr("Could not write %1:\n%2").arg(toQString(target), file.errorString()));
		return false;
	}
	document->setDiskPath(target);
	document->markSaved();
	statusBar()->showMessage(tr("Saved %1").arg(toQString(target)), 8000);
	return true;
}

bool MainWindow::save(SoundEditor* editor) {
	SoundDocument* document = editor->document();
	if (document->diskPath().empty() && !game_) {
		return saveAs(editor);
	}
	const fs::path target = document->diskPath().empty() ? writeTarget(document->gamePath()) : document->diskPath();
	if (!writeDocument(editor, target)) {
		return false;
	}
	if (game_ && !editor->listedInManifest() && !editor->property("manifestOffered").toBool()) {
		editor->setProperty("manifestOffered", true);
		const auto answer = QMessageBox::question(this, tr("Add to manifest?"),
			tr("%1 is not listed in %2, so the game won't load it.\n\nAdd it now?")
				.arg(document->gamePath(), QString::fromUtf8(xv::sound::manifestPathFor(document->kind()))));
		if (answer == QMessageBox::Yes) {
			addToManifest(document); // remounts
			return true;
		}
	}
	remount();
	return true;
}

bool MainWindow::saveAs(SoundEditor* editor) {
	SoundDocument* document = editor->document();
	const fs::path current = !document->diskPath().empty() ? document->diskPath()
		: game_ ? writeTarget(document->gamePath())
		: toPath(QDir::homePath()) / xv::vfs::fromUtf8(document->fileName().toStdString());
	const QString file = QFileDialog::getSaveFileName(this, tr("Save As"), toQString(current), tr("Text files (*.txt);;All files (*)"));
	if (file.isEmpty() || !writeDocument(editor, toPath(file))) {
		return false;
	}
	remount();
	return true;
}

void MainWindow::addToManifest(SoundDocument* document) {
	if (!game_) {
		QMessageBox::information(this, tr("Manifest"), tr("Manifests belong to a game; open the game first."));
		return;
	}
	const FileKind kind = document->kind();
	const std::string manifestPath{xv::sound::manifestPathFor(kind)};
	auto parsed = xv::kv1::parse(fileSystem_->readText(manifestPath).value_or(std::string{}));
	if (std::ranges::none_of(parsed.document.roots, &xv::kv1::Node::isBlock)) {
		parsed.document.appendRoot(xv::kv1::Node::makeBlock(kind == FileKind::Soundscape ? "soundscapes_manifest" : "game_sounds_manifest"));
	}
	if (!xv::sound::addToManifest(parsed.document, kind, document->gamePath().toStdString())) {
		QMessageBox::information(this, tr("Manifest"), tr("%1 is already listed.").arg(document->gamePath()));
		return;
	}

	// Edit the loose manifest in place; a manifest inside a VPK gets an overriding copy in the write folder.
	fs::path target = writeTarget(QString::fromStdString(manifestPath));
	if (const xv::vfs::Mount* mount = fileSystem_->resolve(manifestPath)) {
		if (auto disk = mount->diskPath(manifestPath)) {
			target = *disk;
		}
	}
	std::error_code ec;
	fs::create_directories(target.parent_path(), ec);
	QSaveFile file(toQString(target));
	const std::string bytes = xv::kv1::write(parsed.document);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()) || !file.commit()) {
		QMessageBox::critical(this, tr("Manifest"), tr("Could not write %1:\n%2").arg(toQString(target), file.errorString()));
		return;
	}
	remount();
	statusBar()->showMessage(tr("Added %1 to %2").arg(document->gamePath(), toQString(target)), 8000);
}

bool MainWindow::confirmClose(SoundEditor* editor) {
	if (!editor->document()->isModified()) {
		return true;
	}
	tabs_->setCurrentWidget(editor);
	const auto answer = QMessageBox::question(this, tr("Unsaved changes"),
		tr("Save changes to %1?").arg(editor->document()->fileName()),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (answer == QMessageBox::Save) {
		return save(editor);
	}
	return answer == QMessageBox::Discard;
}

bool MainWindow::closeTab(int index) {
	auto* editor = qobject_cast<SoundEditor*>(tabs_->widget(index));
	if (!editor) {
		return true;
	}
	if (!confirmClose(editor)) {
		return false;
	}
	undoGroup_->removeStack(editor->document()->undoStack());
	tabs_->removeTab(tabs_->indexOf(editor));
	editor->deleteLater();
	return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
	for (int i = 0; i < tabs_->count(); i++) {
		if (auto* editor = qobject_cast<SoundEditor*>(tabs_->widget(i)); editor && !confirmClose(editor)) {
			event->ignore();
			return;
		}
	}
	player_->stop();
	event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
	const auto urls = event->mimeData()->urls();
	if (!urls.isEmpty() && std::ranges::all_of(urls, &QUrl::isLocalFile)) {
		event->acceptProposedAction();
	}
}

void MainWindow::dropEvent(QDropEvent* event) {
	for (const QUrl& url : event->mimeData()->urls()) {
		openPath(url.toLocalFile());
	}
	event->acceptProposedAction();
}

void MainWindow::updateTitle() {
	QString title = QStringLiteral("vomit-tools");
	if (game_) {
		title.prepend(QString::fromStdString(game_->game) + QStringLiteral(" — "));
	}
	if (SoundEditor* editor = currentEditor()) {
		const SoundDocument* document = editor->document();
		title.prepend((document->isModified() ? QStringLiteral("*") : QString{}) + document->fileName() + QStringLiteral(" — "));
	}
	setWindowTitle(title);
}
