// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkyEditor.h"

#include <cmath>
#include <functional>
#include <set>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include "ImageFiles.h"
#include "QtPaths.h"
#include "SkyDocument.h"
#include "SkyTestPattern.h"
#include "SkyView.h"

using xv::sky::Face;
using xv::texture::Orientation;
using Severity = SkyEditor::Issue::Severity;

namespace {

constexpr auto IMAGE_FOLDER_KEY = "skyImageFolder";
constexpr float SEAM_LIMIT = 12.0f; // stock skies stay under 2 on their sides

QString faceName(Face face) {
	return QString::fromUtf8(xv::sky::suffix(face));
}

QString direction(Face face) {
	switch (face) {
		case Face::Rt: return QObject::tr("east · yaw 0");
		case Face::Bk: return QObject::tr("north · yaw 90");
		case Face::Lf: return QObject::tr("west · yaw 180");
		case Face::Ft: return QObject::tr("south · yaw 270");
		case Face::Up: return QObject::tr("up");
		case Face::Dn: return QObject::tr("down");
	}
	return {};
}

QString describeFix(const xv::sky::OrientationFix& fix) {
	QString turn;
	switch (fix.quarterTurns) {
		case 1: turn = QObject::tr("turning it 90° clockwise"); break;
		case 2: turn = QObject::tr("turning it 180°"); break;
		case 3: turn = QObject::tr("turning it 90° counter-clockwise"); break;
		default: break;
	}
	if (fix.mirror) {
		turn = turn.isEmpty() ? QObject::tr("mirroring it") : turn + QObject::tr(" and mirroring it");
	}
	return turn;
}

QString testPatternSource() {
	return QObject::tr("test pattern");
}

QStringList localImageFiles(const QMimeData* mime) {
	QStringList paths;
	for (const QUrl& url : mime->urls()) {
		if (url.isLocalFile() && isImageFile(url.toLocalFile())) {
			paths << url.toLocalFile();
		}
	}
	return paths;
}

QString formatBytes(double bytes) {
	if (bytes >= 1024.0 * 1024.0) {
		return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
	}
	return QStringLiteral("%1 KB").arg(std::lround(bytes / 1024.0));
}

// One face of the cross: its image, or a place to drop one.
class FaceSlot : public QWidget {
public:
	FaceSlot(SkyDocument* document, Face face, std::function<void(Face)> browse, std::function<void(Face, QStringList)> dropped)
			: document_(document)
			, face_(face)
			, browse_(std::move(browse))
			, dropped_(std::move(dropped)) {
		setAcceptDrops(true);
		setMinimumSize(90, 90);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setObjectName(QStringLiteral("face_") + faceName(face));
		setToolTip(tr("%1 (%2): drop an image here, double-click to choose one, right-click to turn or mirror it").arg(faceName(face), direction(face)));
	}

	void refresh() {
		thumbnail_ = {};
		update();
	}

	[[nodiscard]] QSize sizeHint() const override { return {150, 170}; }

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		QFont bold = font();
		bold.setBold(true);
		const int caption = QFontMetrics{bold}.height() + 4; // above the image, so its edge labels stay visible
		const int side = std::max(8, std::min(width(), height() - caption));
		const QRectF square((width() - side) / 2.0 + 1, (height() - side - caption) / 2.0 + caption + 1, side - 2, side - 2);
		const auto& image = document_->cube().face(face_);
		if (image.empty()) {
			painter.setPen(QPen(hover_ ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid), hover_ ? 2 : 1, Qt::DashLine));
			painter.drawRoundedRect(square, 6, 6);
			painter.setPen(palette().color(QPalette::PlaceholderText));
			painter.drawText(square, Qt::AlignCenter | Qt::TextWordWrap, tr("%1\n%2\n\nDrop an image\nor double-click").arg(faceName(face_), direction(face_)));
			return;
		}
		if (thumbnail_.isNull() || thumbnail_.width() != static_cast<int>(square.width())) {
			thumbnail_ = toQImage(image).scaled(square.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		}
		painter.drawImage(square.topLeft(), thumbnail_);
		painter.setPen(QPen(hover_ ? palette().color(QPalette::Highlight) : QColor(0, 0, 0, 120), hover_ ? 3 : 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(square);

		const auto& slot = document_->slot(face_);
		QString text = QStringLiteral("%1 · %2×%3").arg(faceName(face_)).arg(image.width).arg(image.height);
		if (slot.orientation.quarterTurns || slot.orientation.mirror) {
			text += QStringLiteral(" · %1°%2").arg(slot.orientation.quarterTurns * 90).arg(slot.orientation.mirror ? tr(" mirrored") : QString{});
		}
		painter.setFont(bold);
		painter.setPen(palette().color(QPalette::WindowText));
		const QRectF band(square.left(), square.top() - caption, square.width(), caption);
		painter.drawText(band, Qt::AlignVCenter | Qt::AlignLeft, painter.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(band.width())));
	}

	void mouseDoubleClickEvent(QMouseEvent*) override { browse_(face_); }

	void contextMenuEvent(QContextMenuEvent* event) override {
		QMenu menu(this);
		menu.addAction(tr("Choose Image…"), this, [this] { browse_(face_); });
		const auto& slot = document_->slot(face_);
		const bool has = slot.image != nullptr;
		const auto orient = [this](Orientation change, const QString& text) {
			return [this, change, text] { document_->orient(face_, change, text.arg(faceName(face_))); };
		};
		menu.addSeparator();
		menu.addAction(tr("Turn Clockwise"), this, orient({1, false}, tr("Turn %1")))->setEnabled(has);
		menu.addAction(tr("Turn Counter-clockwise"), this, orient({3, false}, tr("Turn %1")))->setEnabled(has);
		menu.addAction(tr("Turn 180°"), this, orient({2, false}, tr("Turn %1")))->setEnabled(has);
		menu.addAction(tr("Mirror"), this, orient({0, true}, tr("Mirror %1")))->setEnabled(has);
		menu.addSeparator();
		menu.addAction(tr("Copy Source Path"), this, [source = slot.source] { QApplication::clipboard()->setText(source); })->setEnabled(has && !slot.source.isEmpty());
		menu.addAction(tr("Clear"), this, [this] { document_->clearFace(face_); })->setEnabled(has);
		menu.exec(event->globalPos());
	}

	void dragEnterEvent(QDragEnterEvent* event) override {
		if (!localImageFiles(event->mimeData()).isEmpty()) {
			event->acceptProposedAction();
			hover_ = true;
			update();
		}
	}
	void dragLeaveEvent(QDragLeaveEvent*) override {
		hover_ = false;
		update();
	}
	void dropEvent(QDropEvent* event) override {
		hover_ = false;
		update();
		dropped_(face_, localImageFiles(event->mimeData()));
		event->acceptProposedAction();
	}

private:
	SkyDocument* document_;
	Face face_;
	std::function<void(Face)> browse_;
	std::function<void(Face, QStringList)> dropped_;
	QImage thumbnail_;
	bool hover_ = false;
};

// Keeps the preview square however the panel is shaped, with its controls right under it.
class SquarePanel : public QWidget {
public:
	SquarePanel(QWidget* view, QWidget* bar)
			: view_(view)
			, bar_(bar) {
		view_->setParent(this);
		bar_->setParent(this);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	[[nodiscard]] QSize sizeHint() const override { return {480, 480 + bar_->sizeHint().height()}; }
	[[nodiscard]] QSize minimumSizeHint() const override {
		return {std::max(240, bar_->minimumSizeHint().width()), 240 + bar_->sizeHint().height()};
	}

protected:
	void resizeEvent(QResizeEvent*) override {
		constexpr int GAP = 4;
		const int barHeight = bar_->sizeHint().height();
		const int side = std::max(0, std::min(width(), height() - barHeight - GAP));
		const int top = (height() - side - GAP - barHeight) / 2;
		view_->setGeometry((width() - side) / 2, top, side, side);
		const int barWidth = std::min(width(), std::max(side, bar_->sizeHint().width()));
		bar_->setGeometry((width() - barWidth) / 2, top + side + GAP, barWidth, barHeight);
	}

private:
	QWidget* view_;
	QWidget* bar_;
};

} // namespace

SkyEditor::SkyEditor(SkyDocument* document, QWidget* parent)
		: QWidget(parent)
		, document_(document) {
	document_->setParent(this);
	setAcceptDrops(true);

	auto* toolbar = new QToolBar;
	toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
	toolbar->addWidget(new QLabel(tr("Name ")));
	name_ = new QLineEdit(document_->name());
	name_->setObjectName(QStringLiteral("skyName"));
	name_->setPlaceholderText(QStringLiteral("sky_mymap"));
	name_->setMaximumWidth(220);
	name_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[a-z0-9_.-]{0,64}")), name_));
	name_->setToolTip(tr("The sky's name: maps use it as their skyname, and its files are materials/skybox/<name>rt.vtf and so on"));
	connect(name_, &QLineEdit::textEdited, this, [this](const QString& text) { document_->setName(text); });
	connect(document_, &SkyDocument::nameChanged, this, [this] {
		if (name_->text() != document_->name()) {
			name_->setText(document_->name());
		}
		relint();
	});
	toolbar->addWidget(name_);
	toolbar->addSeparator();

	format_ = new QComboBox;
	format_->addItem(tr("Uncompressed (BGR888)"), static_cast<int>(xv::texture::VtfFormat::BGR888));
	format_->addItem(tr("Compressed (DXT1)"), static_cast<int>(xv::texture::VtfFormat::DXT1));
	format_->setToolTip(tr("Uncompressed keeps smooth gradients smooth; DXT1 is a sixth of the size but can band"));
	format_->setCurrentIndex(format_->findData(static_cast<int>(document_->format())));
	connect(format_, &QComboBox::currentIndexChanged, this, [this] {
		document_->setFormat(static_cast<xv::texture::VtfFormat>(format_->currentData().toInt()));
	});
	toolbar->addWidget(format_);

	size_ = new QComboBox;
	size_->addItem(tr("Keep size"), 0);
	for (int size : {256, 512, 1024, 2048, 4096}) {
		size_->addItem(QStringLiteral("%1 × %1").arg(size), size);
	}
	size_->setToolTip(tr("Face size to write; the game needs square faces with power-of-two sides"));
	size_->setCurrentIndex(std::max(0, size_->findData(document_->size())));
	connect(size_, &QComboBox::currentIndexChanged, this, [this] { document_->setSize(size_->currentData().toInt()); });
	toolbar->addWidget(size_);

	mipmaps_ = new QCheckBox(tr("Mipmaps"));
	mipmaps_->setChecked(document_->mipmaps());
	mipmaps_->setToolTip(tr("Smaller copies for distant viewing. Valve's newer skies go without (NOMIP), as a sky is never far away"));
	connect(mipmaps_, &QCheckBox::toggled, this, [this](bool on) { document_->setMipmaps(on); });
	connect(document_, &SkyDocument::optionsChanged, this, [this] {
		const QSignalBlocker formatBlocker{format_};
		const QSignalBlocker sizeBlocker{size_};
		const QSignalBlocker mipmapsBlocker{mipmaps_};
		format_->setCurrentIndex(format_->findData(static_cast<int>(document_->format())));
		size_->setCurrentIndex(std::max(0, size_->findData(document_->size())));
		mipmaps_->setChecked(document_->mipmaps());
		relint();
		updateSizeEstimate();
	});
	toolbar->addWidget(mipmaps_);
	estimate_ = new QLabel;
	estimate_->setContentsMargins(8, 0, 8, 0);
	estimate_->setEnabled(false);
	toolbar->addWidget(estimate_);
	toolbar->addSeparator();
	toolbar->addAction(tr("Load Faces…"), this, &SkyEditor::browseFiles)->setToolTip(tr("Choose up to six images named …rt, …bk, …lf, …ft, …up and …dn"));
	toolbar->addAction(tr("Test Pattern"), this, [this] { useTestPattern(); })->setToolTip(tr("Labelled faces for checking a sky's orientation in game"));
	toolbar->addSeparator();
	QAction* exportAction = toolbar->addAction(tr("Export"), this, &SkyEditor::exportRequested);
	exportAction->setToolTip(tr("Write the VTFs and VMTs to the game's write folder (Ctrl+S)"));
	toolbar->addAction(tr("Export To…"), this, &SkyEditor::exportToRequested)->setToolTip(tr("Write them under a folder of your choice (a mod or custom/ folder)"));

	exported_ = new QLabel;
	exported_->setObjectName(QStringLiteral("skyExported"));
	exported_->setWordWrap(true);
	exported_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	exported_->setContentsMargins(8, 2, 8, 2);
	exported_->hide();

	// The cross: as the faces join, with rt in the middle (up's bottom edge and dn's top edge
	// meet rt; going right from rt: ft, lf, bk).
	auto* cross = new QWidget;
	auto* grid = new QGridLayout(cross);
	grid->setSpacing(2);
	const std::array<std::tuple<Face, int, int>, 6> places{{{Face::Up, 0, 1}, {Face::Bk, 1, 0}, {Face::Rt, 1, 1}, {Face::Ft, 1, 2}, {Face::Lf, 1, 3}, {Face::Dn, 2, 1}}};
	for (const auto& [face, row, column] : places) {
		auto* slot = new FaceSlot(document_, face, [this](Face f) { browseFace(f); }, [this](Face f, const QStringList& paths) {
			if (paths.size() == 1) {
				loadFace(f, paths.front());
			} else {
				loadFiles(paths);
			}
		});
		slots_[xv::sky::indexOf(face)] = slot;
		grid->addWidget(slot, row, column);
	}
	for (int i = 0; i < 4; i++) {
		grid->setColumnStretch(i, 1);
	}

	issueList_ = new QListWidget;
	issueList_->setObjectName(QStringLiteral("skyIssues"));
	issueList_->setWordWrap(true);
	issueList_->setMaximumHeight(150);
	connect(issueList_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		const int row = issueList_->row(item);
		if (row >= 0 && row < static_cast<int>(issues_.size()) && issues_[static_cast<std::size_t>(row)].fix) {
			applyFix(*issues_[static_cast<std::size_t>(row)].fix);
		}
	});

	auto* left = new QWidget;
	auto* leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(6, 6, 6, 6);
	leftLayout->addWidget(cross, 1);
	leftLayout->addWidget(issueList_);

	view_ = new SkyView;
	view_->setCube(&document_->cube());
	auto* labels = new QCheckBox(tr("Face names"));
	labels->setChecked(true);
	connect(labels, &QCheckBox::toggled, view_, &SkyView::setShowLabels);
	auto* edges = new QCheckBox(tr("Face edges"));
	connect(edges, &QCheckBox::toggled, view_, &SkyView::setShowEdges);
	auto* copy = new QPushButton(tr("Copy setang"));
	copy->setToolTip(tr("Copies the console command (needs sv_cheats 1) that looks the same way in game"));
	connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(view_->setangCommand()); });
	auto* viewBar = new QWidget;
	auto* viewBarLayout = new QHBoxLayout(viewBar);
	viewBarLayout->setContentsMargins(0, 0, 0, 0);
	viewBarLayout->addWidget(labels);
	viewBarLayout->addWidget(edges);
	viewBarLayout->addStretch();
	viewBarLayout->addWidget(copy);
	auto* right = new QWidget;
	auto* rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(0, 6, 6, 6);
	rightLayout->addWidget(new SquarePanel(view_, viewBar), 1);

	auto* splitter = new QSplitter;
	splitter->addWidget(left);
	splitter->addWidget(right);
	splitter->setStretchFactor(0, 4);
	splitter->setStretchFactor(1, 5);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(toolbar);
	layout->addWidget(exported_);
	layout->addWidget(splitter, 1);

	connect(document_, &SkyDocument::facesChanged, this, &SkyEditor::facesChanged);
	facesChanged();
}

void SkyEditor::facesChanged() {
	view_->setCube(&document_->cube());
	for (QWidget* slot : slots_) {
		static_cast<FaceSlot*>(slot)->refresh();
	}
	relint();
	updateSizeEstimate();
}

QStringList SkyEditor::loadFiles(const QStringList& given) {
	QStringList paths = given;
	// One face dropped into an empty sky brings the rest of its sky along.
	const bool empty = std::ranges::none_of(xv::sky::FACES, [this](Face face) { return document_->slot(face).image != nullptr; });
	if (paths.size() == 1 && empty) {
		if (const auto files = xv::sky::findSkyFiles(toPath(paths.front())); files && files->count() > 1) {
			paths.clear();
			for (const auto& file : files->faces) {
				if (!file.empty() && isImageFile(toQString(file))) {
					paths << toQString(file);
				}
			}
			if (document_->name().isEmpty() && xv::sky::isValidName(files->name)) {
				document_->setName(QString::fromStdString(files->name));
			}
		}
	}
	QStringList problems;
	std::array<std::optional<SkyDocument::Slot>, 6> changes;
	for (const QString& path : paths) {
		const QString name = QFileInfo{path}.fileName();
		const auto face = xv::sky::faceFromFileName(name.toStdString());
		if (!face) {
			problems << tr("%1: the name doesn't end in rt, bk, lf, ft, up or dn, so drop it onto its face instead").arg(name);
			continue;
		}
		QString error;
		auto image = readImageFile(path, &error);
		if (!image) {
			problems << tr("%1: %2").arg(name, error);
			continue;
		}
		changes[xv::sky::indexOf(*face)] = SkyDocument::Slot{std::make_shared<const xv::texture::Image>(std::move(*image)), path, {}, {}};
	}
	if (std::ranges::any_of(changes, [](const auto& change) { return change.has_value(); })) {
		document_->setSlots(changes, tr("Load Faces"));
	}
	if (!problems.isEmpty()) {
		QMessageBox::warning(this, tr("Load Faces"), problems.join(u'\n'));
	}
	return problems;
}

bool SkyEditor::loadFace(Face face, const QString& path) {
	QString error;
	auto image = readImageFile(path, &error);
	if (!image) {
		QMessageBox::warning(this, tr("Load Image"), tr("Could not read %1:\n%2").arg(QDir::toNativeSeparators(path), error));
		return false;
	}
	document_->setSlot(face, {std::make_shared<const xv::texture::Image>(std::move(*image)), path, {}, {}}, tr("Set %1").arg(faceName(face)));
	return true;
}

void SkyEditor::useTestPattern(int size) {
	std::array<std::optional<SkyDocument::Slot>, 6> changes;
	for (Face face : xv::sky::FACES) {
		changes[xv::sky::indexOf(face)] = SkyDocument::Slot{std::make_shared<const xv::texture::Image>(skyTestPattern(face, size)), testPatternSource(), {}, {}};
	}
	document_->setSlots(changes, tr("Test Pattern"));
	if (document_->name().isEmpty()) {
		document_->setName(QStringLiteral("vomit_test"));
	}
}

void SkyEditor::applyFix(const xv::sky::OrientationFix& fix) {
	document_->orient(fix.face, {fix.quarterTurns, fix.mirror}, tr("Fix %1").arg(faceName(fix.face)));
}

bool SkyEditor::canExport() const {
	return std::ranges::none_of(issues_, [](const Issue& issue) { return issue.severity == Severity::Error; });
}

void SkyEditor::showExported(const QString& folder) {
	exported_->setText(tr("Exported to %1. In game, with a map loaded, open the console and enter: sv_skyname %2")
		.arg(QDir::toNativeSeparators(folder), document_->name()));
	exported_->show();
}

void SkyEditor::dragEnterEvent(QDragEnterEvent* event) {
	if (!localImageFiles(event->mimeData()).isEmpty()) {
		event->acceptProposedAction();
	}
}

void SkyEditor::dropEvent(QDropEvent* event) {
	loadFiles(localImageFiles(event->mimeData()));
	event->acceptProposedAction();
}

void SkyEditor::browseFace(Face face) {
	QSettings settings;
	const QString path = QFileDialog::getOpenFileName(this, tr("Image for %1 (%2)").arg(faceName(face), direction(face)), settings.value(IMAGE_FOLDER_KEY).toString(),
		tr("Images (*.png *.jpg *.jpeg *.tga *.bmp *.psd *.hdr *.vtf);;All files (*)"));
	if (!path.isEmpty()) {
		settings.setValue(IMAGE_FOLDER_KEY, QFileInfo{path}.absolutePath());
		loadFace(face, path);
	}
}

void SkyEditor::browseFiles() {
	QSettings settings;
	const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Load Faces (named …rt, …bk, …lf, …ft, …up, …dn)"), settings.value(IMAGE_FOLDER_KEY).toString(),
		tr("Images (*.png *.jpg *.jpeg *.tga *.bmp *.psd *.hdr *.vtf);;All files (*)"));
	if (!paths.isEmpty()) {
		settings.setValue(IMAGE_FOLDER_KEY, QFileInfo{paths.front()}.absolutePath());
		loadFiles(paths);
	}
}

void SkyEditor::relint() {
	issues_.clear();
	const QString name = document_->name();
	if (name.isEmpty()) {
		issues_.push_back({Severity::Error, tr("Name the sky. Maps use the name as their skyname, and the files become materials/skybox/<name>rt.vtf and so on."), {}});
	} else if (!xv::sky::isValidName(name.toStdString())) {
		issues_.push_back({Severity::Error, tr("Sky names may only use lowercase letters, digits, '_', '-' and '.'."), {}});
	}

	// Faces may differ in size (Valve's own skies pair 4x4 bottoms with 512 sides), but each must
	// be a square with power-of-two sides. Half-height stock faces are made square on export.
	const xv::sky::Cube& cube = document_->cube();
	QStringList missing;
	for (Face face : xv::sky::FACES) {
		const auto& image = cube.face(face);
		if (image.empty()) {
			missing << faceName(face);
			continue;
		}
		const bool usable = image.width == image.height && xv::texture::isPowerOfTwo(image.width);
		if (document_->size() == 0 && !usable && cube.transforms[xv::sky::indexOf(face)].isIdentity()) {
			issues_.push_back({Severity::Error, tr("%1 is %2×%3, but the game needs square faces with power-of-two sides. Pick a face size to resize it.")
				.arg(faceName(face)).arg(image.width).arg(image.height), {}});
		}
	}
	if (!missing.isEmpty()) {
		issues_.push_back({Severity::Error, tr("No image yet for: %1.").arg(missing.join(QStringLiteral(", "))), {}});
	}

	// How the faces meet. Turned up/down faces can be fixed; other mismatches only reported. The
	// test pattern's faces aren't meant to meet, so it isn't checked.
	const bool testPattern = std::ranges::all_of(xv::sky::FACES, [this](Face face) { return document_->slot(face).source == testPatternSource(); });
	std::set<Face> fixable;
	for (const auto& fix : testPattern ? std::vector<xv::sky::OrientationFix>{} : xv::sky::suggestOrientation(cube)) {
		fixable.insert(fix.face);
		issues_.push_back({Severity::Warning, tr("%1 looks turned: %2 makes its edges meet the sides (difference %3 → %4). Double-click to fix.")
			.arg(faceName(fix.face), describeFix(fix)).arg(std::lround(fix.before)).arg(std::lround(fix.after)), fix});
	}
	float worstBottom = 0.0f;
	for (const auto& seam : testPattern ? std::vector<xv::sky::Seam>{} : xv::sky::seams(cube)) {
		if (fixable.contains(seam.a) || fixable.contains(seam.b) || seam.difference < SEAM_LIMIT) {
			continue;
		}
		if (seam.a == Face::Dn || seam.b == Face::Dn) {
			worstBottom = std::max(worstBottom, seam.difference);
			continue;
		}
		issues_.push_back({Severity::Warning, tr("%1 and %2 don't meet (difference %3). Check that each image is on the right face.")
			.arg(faceName(seam.a), faceName(seam.b)).arg(std::lround(seam.difference)), {}});
	}
	if (worstBottom > 0.0f) {
		issues_.push_back({Severity::Info, tr("dn doesn't continue the sides (difference up to %1). That's common, and fine: the bottom of a sky is rarely seen.")
			.arg(std::lround(worstBottom)), {}});
	}

	issueList_->clear();
	for (const Issue& issue : issues_) {
		const QStyle::StandardPixmap icon = issue.severity == Severity::Error ? QStyle::SP_MessageBoxCritical
			: issue.severity == Severity::Warning ? QStyle::SP_MessageBoxWarning : QStyle::SP_MessageBoxInformation;
		auto* item = new QListWidgetItem(style()->standardIcon(icon), issue.message);
		if (issue.fix) {
			item->setToolTip(tr("Double-click to apply"));
		}
		issueList_->addItem(item);
	}
	if (issues_.empty()) {
		issueList_->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_DialogApplyButton), tr("Ready to export.")));
	}
}

void SkyEditor::updateSizeEstimate() {
	int side = document_->size();
	if (side == 0) {
		for (const auto& image : document_->cube().faces) {
			side = std::max(side, std::max(image.width, image.height));
		}
	}
	if (side == 0) {
		estimate_->clear();
		return;
	}
	const double pixels = static_cast<double>(side) * side * (document_->mipmaps() ? 4.0 / 3.0 : 1.0);
	const double perFace = pixels * (document_->format() == xv::texture::VtfFormat::DXT1 ? 0.5 : 3.0);
	estimate_->setText(tr("≈ %1").arg(formatBytes(perFace * 6)));
	estimate_->setToolTip(tr("6 faces of %1 each").arg(formatBytes(perFace)));
}
