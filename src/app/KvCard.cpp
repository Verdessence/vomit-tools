// SPDX-License-Identifier: GPL-3.0-or-later
#include "KvCard.h"

#include <algorithm>
#include <optional>
#include <random>

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "SoundDocument.h"
#include "SoundIndex.h"
#include "SoundPlayer.h"
#include "SoundValues.h"
#include "sound/SoundPlayback.h"

using xv::kv1::Node;
using xv::sound::Issue;
using xv::sound::NodePath;

namespace {

QString qs(std::string_view text) {
	return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

NodePath childPath(NodePath path, std::size_t child) {
	path.push_back(child);
	return path;
}

// A light tint of the severity colour over the base colour, so it reads in light and dark themes.
// (Translucent brushes are not blended on stylesheet-styled tables, so the mix is done here.)
QColor issueColor(Issue::Severity severity, const QColor& base) {
	QColor tint;
	switch (severity) {
		case Issue::Severity::Error:   tint = QColor{220, 50, 50}; break;
		case Issue::Severity::Warning: tint = QColor{235, 160, 20}; break;
		default:                       tint = QColor{60, 140, 230}; break;
	}
	constexpr double strength = 0.3;
	return QColor::fromRgbF(static_cast<float>(base.redF() + (tint.redF() - base.redF()) * strength),
	                        static_cast<float>(base.greenF() + (tint.greenF() - base.greenF()) * strength),
	                        static_cast<float>(base.blueF() + (tint.blueF() - base.blueF()) * strength));
}

const char* const CARD_STYLE = R"(
QLabel#cardTab {
	border: 1px solid palette(mid);
	border-bottom: none;
	border-top-left-radius: 7px;
	border-top-right-radius: 7px;
	padding: 4px 12px;
	background: palette(button);
	font-weight: 600;
}
QFrame#cardBody {
	border: 1px solid palette(mid);
	border-radius: 7px;
	border-top-left-radius: 0px;
	background: palette(base);
}
QTableWidget#cardTable {
	border: none;
	background: transparent;
	gridline-color: palette(midlight);
}
)";

} // namespace

KvCard::KvCard(SoundDocument& document, const SoundIndex& index, SoundPlayer* player, NodePath path, QWidget* parent)
		: QWidget(parent)
		, document_(document)
		, index_(index)
		, player_(player)
		, path_(std::move(path)) {
	const Node* node = document_.node(path_);
	Q_ASSERT(node && node->isBlock());
	const xv::sound::BlockDef* def = document_.blockDef(path_);
	const bool entry = path_.size() == 1;
	if (entry) {
		setStyleSheet(QString::fromLatin1(CARD_STYLE));
	}

	tab_ = new QLabel(qs(node->key()));
	tab_->setObjectName(QStringLiteral("cardTab"));
	tab_->setToolTip(def ? qs(def->help) : tr("Not a block the engine knows here"));
	if (entry) {
		tab_->setToolTip(tab_->toolTip() + tr("\nDouble-click to rename"));
		tab_->installEventFilter(this);
	}

	auto* add = new QToolButton;
	add->setText(QStringLiteral("+"));
	add->setToolTip(tr("Add a key or block"));
	add->setAutoRaise(true);
	add->setPopupMode(QToolButton::InstantPopup);
	auto* addMenu = new QMenu(add);
	connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] { fillAddMenu(addMenu); });
	add->setMenu(addMenu);

	auto* header = new QHBoxLayout;
	header->setContentsMargins(0, 0, 0, 0);
	header->setSpacing(2);
	header->addWidget(tab_, 0, Qt::AlignBottom);
	if (player_ && playable()) {
		auto* play = new QToolButton;
		play->setText(QStringLiteral("▶"));
		play->setAutoRaise(true);
		play->setToolTip(entry && document_.kind() == xv::sound::FileKind::Soundscape
			? tr("Play this soundscape: its loops, random sounds and nested soundscapes")
			: tr("Play with these settings (volume, pitch, random picks)"));
		connect(play, &QToolButton::clicked, this, &KvCard::play);
		header->addWidget(play, 0, Qt::AlignBottom);
	}
	header->addStretch();
	header->addWidget(add, 0, Qt::AlignBottom);
	if (!entry) {
		auto* remove = new QToolButton;
		remove->setText(QStringLiteral("✕"));
		remove->setToolTip(tr("Delete this block"));
		remove->setAutoRaise(true);
		connect(remove, &QToolButton::clicked, this, [this] { document_.removeNode(path_); });
		header->addWidget(remove, 0, Qt::AlignBottom);
	}

	auto* body = new QFrame;
	body->setObjectName(QStringLiteral("cardBody"));
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(6, 6, 6, 6);
	bodyLayout->setSpacing(8);

	buildTable(*node);
	bodyLayout->addWidget(table_);
	table_->setVisible(!rowChild_.empty());

	for (std::size_t i = 0; i < node->children().size(); i++) {
		if (node->child(i).isBlock()) {
			auto* card = new KvCard(document_, index_, player_, childPath(path_, i));
			connect(card, &KvCard::added, this, &KvCard::added);
			children_.push_back(card);
			bodyLayout->addWidget(card);
		}
	}
	if (rowChild_.empty() && children_.empty()) {
		auto* empty = new QLabel(tr("Empty. Use + to add keys or blocks."));
		empty->setEnabled(false);
		bodyLayout->addWidget(empty);
	}

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addLayout(header);
	layout->addWidget(body);
}

void KvCard::buildTable(const Node& node) {
	const xv::sound::BlockDef* def = document_.blockDef(path_);

	table_ = new QTableWidget(0, 3);
	table_->setObjectName(QStringLiteral("cardTable"));
	table_->horizontalHeader()->hide();
	table_->verticalHeader()->hide();
	table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
	table_->setWordWrap(false);
	table_->setItemDelegate(new SoundValueDelegate(def, index_, document_, table_));
	table_->horizontalHeader()->setSectionResizeMode(SoundValueDelegate::KeyColumn, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(SoundValueDelegate::ValueColumn, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(SoundValueDelegate::MeaningColumn, QHeaderView::ResizeToContents);
	table_->setContextMenuPolicy(Qt::CustomContextMenu);

	filling_ = true;
	QFont meaningFont = table_->font();
	meaningFont.setItalic(true);
	const QColor meaningColor = palette().color(QPalette::PlaceholderText);
	for (std::size_t i = 0; i < node.children().size(); i++) {
		const Node& child = node.child(i);
		if (!child.isPair()) {
			continue;
		}
		const int row = table_->rowCount();
		table_->insertRow(row);
		rowChild_.push_back(i);

		const QString key = qs(child.key());
		const QString value = qs(child.value());
		const auto kind = SoundValueDelegate::kindOf(def, key);

		auto* keyItem = new QTableWidgetItem(key);
		const xv::sound::KeyDef* keyDef = def ? xv::sound::findKey(*def, key.toStdString()) : nullptr;
		keyItem->setToolTip(keyDef ? qs(keyDef->help) : tr("Not a key the engine reads here"));
		auto* valueItem = new QTableWidgetItem(value);
		auto* meaningItem = new QTableWidgetItem(describeValue(kind, value));
		meaningItem->setFlags(Qt::ItemIsEnabled);
		meaningItem->setFont(meaningFont);
		meaningItem->setForeground(meaningColor);

		table_->setItem(row, SoundValueDelegate::KeyColumn, keyItem);
		table_->setItem(row, SoundValueDelegate::ValueColumn, valueItem);
		table_->setItem(row, SoundValueDelegate::MeaningColumn, meaningItem);

		const std::string gamePath = kind == xv::sound::ValueKind::Wave ? xv::sound::waveGamePath(child.value()) : std::string{};
		if (player_ && !gamePath.empty()) {
			auto* cell = new QWidget;
			auto* cellLayout = new QHBoxLayout(cell);
			cellLayout->setContentsMargins(2, 0, 4, 0);
			cellLayout->setSpacing(4);
			auto* play = new QToolButton;
			play->setText(QStringLiteral("▶"));
			play->setAutoRaise(true);
			play->setToolTip(tr("Play %1 as it is").arg(QString::fromStdString(gamePath)));
			connect(play, &QToolButton::clicked, this, [this, gamePath] { player_->playGameSound(gamePath, document_.localRoot()); });
			auto* meaning = new QLabel(meaningItem->text());
			meaning->setFont(meaningFont);
			meaning->setStyleSheet(QStringLiteral("color: %1;").arg(meaningColor.name()));
			cellLayout->addWidget(play);
			cellLayout->addWidget(meaning);
			cellLayout->addStretch();
			meaningItem->setText({});
			table_->setCellWidget(row, SoundValueDelegate::MeaningColumn, cell);
		}
	}
	filling_ = false;
	// Exactly as tall as its rows: the card grows instead of the table scrolling.
	table_->resizeRowsToContents();
	table_->setFixedHeight(table_->verticalHeader()->length() + 2 * table_->frameWidth());

	connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (filling_ || item->row() >= static_cast<int>(rowChild_.size())) {
			return;
		}
		const NodePath target = childPath(path_, rowChild_[static_cast<std::size_t>(item->row())]);
		if (item->column() == SoundValueDelegate::KeyColumn) {
			document_.setKey(target, item->text());
		} else if (item->column() == SoundValueDelegate::ValueColumn) {
			document_.setValue(target, item->text());
		}
	});

	auto* remove = new QAction(tr("Delete Key"), table_);
	remove->setShortcut(QKeySequence::Delete);
	remove->setShortcutContext(Qt::WidgetShortcut);
	connect(remove, &QAction::triggered, this, [this] { removeRow(table_->currentRow()); });
	table_->addAction(remove);

	connect(table_, &QTableWidget::customContextMenuRequested, this, [this, remove](const QPoint& position) {
		QMenu menu;
		const int row = table_->rowAt(position.y());
		if (row >= 0) {
			table_->setCurrentCell(row, SoundValueDelegate::ValueColumn);
			remove->setText(tr("Delete \"%1\"").arg(table_->item(row, SoundValueDelegate::KeyColumn)->text()));
			menu.addAction(remove);
			menu.addSeparator();
		}
		fillAddMenu(&menu);
		menu.exec(table_->viewport()->mapToGlobal(position));
	});
}

void KvCard::fillAddMenu(QMenu* menu) {
	menu->clear();
	const Node* node = document_.node(path_);
	const xv::sound::BlockDef* def = document_.blockDef(path_);
	if (!node || !def) {
		menu->addAction(tr("Custom Key…"), this, [this] {
			const QString key = QInputDialog::getText(this, tr("Add Key"), tr("Key:"));
			if (!key.isEmpty()) {
				addKey(key);
			}
		});
		return;
	}

	menu->addSection(tr("Keys"));
	for (const xv::sound::KeyDef& key : def->keys) {
		// Only rndwave's "wave" may repeat; other keys are offered once.
		const bool repeatable = xv::kv1::equalsIgnoreCase(def->key, "rndwave");
		if (!repeatable && node->find(key.key)) {
			continue;
		}
		QAction* action = menu->addAction(qs(key.key), this, [this, name = qs(key.key)] { addKey(name); });
		action->setToolTip(qs(key.help));
	}
	menu->addAction(tr("Custom Key…"), this, [this] {
		const QString key = QInputDialog::getText(this, tr("Add Key"), tr("Key:"));
		if (!key.isEmpty()) {
			addKey(key);
		}
	});
	if (!def->blocks.empty()) {
		menu->addSection(tr("Blocks"));
		for (const std::string_view block : def->blocks) {
			const xv::sound::BlockDef* blockDef = document_.schema().block(block);
			QAction* action = menu->addAction(qs(block), this, [this, name = qs(block)] { addBlock(name); });
			action->setToolTip(blockDef ? qs(blockDef->help) : QString{});
		}
	}
	menu->setToolTipsVisible(true);
}

std::size_t KvCard::pairInsertIndex() const {
	// New keys go after the existing keys, above any sub-blocks.
	const Node* node = document_.node(path_);
	std::size_t index = 0;
	for (std::size_t i = 0; i < node->children().size(); i++) {
		if (node->child(i).isPair()) {
			index = i + 1;
		}
	}
	return index;
}

void KvCard::addKey(const QString& key) {
	const std::size_t index = pairInsertIndex();
	const std::string name = key.toStdString();
	document_.insertChild(path_, index, Node::makePair(name, defaultValue(document_, name).toStdString()), tr("Add %1").arg(key));
	emit added(childPath(path_, index));
}

void KvCard::addBlock(const QString& key) {
	const Node* node = document_.node(path_);
	const std::size_t index = node->children().size();
	document_.insertChild(path_, index, blockTemplate(document_, key.toStdString()), tr("Add %1").arg(key));
	emit added(childPath(path_, index));
}

void KvCard::removeRow(int row) {
	if (row >= 0 && row < static_cast<int>(rowChild_.size())) {
		document_.removeNode(childPath(path_, rowChild_[static_cast<std::size_t>(row)]));
	}
}

void KvCard::rename() {
	const Node* node = document_.node(path_);
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Rename"), tr("Name:"), QLineEdit::Normal, qs(node->key()), &ok);
	if (ok && !name.isEmpty()) {
		document_.setKey(path_, name);
	}
}

bool KvCard::eventFilter(QObject* watched, QEvent* event) {
	if (watched == tab_ && event->type() == QEvent::MouseButtonDblClick) {
		rename();
		return true;
	}
	return QWidget::eventFilter(watched, event);
}

void KvCard::showIssues(const std::vector<Issue>& issues) {
	QString tabProblems;
	std::optional<Issue::Severity> tabSeverity;
	for (const Issue& issue : issues) {
		if (issue.path == path_) {
			tabProblems += (tabProblems.isEmpty() ? QString{} : QStringLiteral("\n")) + QString::fromStdString(issue.message);
			tabSeverity = std::max(tabSeverity.value_or(issue.severity), issue.severity);
			continue;
		}
		if (issue.path.size() != path_.size() + 1 || !std::equal(path_.begin(), path_.end(), issue.path.begin())) {
			continue;
		}
		const auto row = std::ranges::find(rowChild_, issue.path.back());
		if (row == rowChild_.end()) {
			continue;
		}
		const int r = static_cast<int>(row - rowChild_.begin());
		for (const int column : {SoundValueDelegate::KeyColumn, SoundValueDelegate::ValueColumn}) {
			QTableWidgetItem* item = table_->item(r, column);
			item->setBackground(issueColor(issue.severity, palette().color(QPalette::Base)));
			item->setToolTip(QString::fromStdString(issue.message));
		}
	}
	if (tabSeverity) {
		tab_->setToolTip(tabProblems);
		tab_->setStyleSheet(QStringLiteral("background: %1;").arg(issueColor(*tabSeverity, palette().color(QPalette::Button)).name()));
	}
	for (KvCard* child : children_) {
		child->showIssues(issues);
	}
}

bool KvCard::beginEdit(const NodePath& path) {
	if (path.size() == path_.size() + 1 && std::equal(path_.begin(), path_.end(), path.begin())) {
		const auto row = std::ranges::find(rowChild_, path.back());
		if (row != rowChild_.end()) {
			const int r = static_cast<int>(row - rowChild_.begin());
			table_->setFocus();
			table_->setCurrentCell(r, SoundValueDelegate::ValueColumn);
			table_->editItem(table_->item(r, SoundValueDelegate::ValueColumn));
			return true;
		}
	}
	return std::ranges::any_of(children_, [&path](KvCard* child) { return child->beginEdit(path); });
}

bool KvCard::playable() const {
	const Node* node = document_.node(path_);
	if (!node) {
		return false;
	}
	if (path_.size() == 1) {
		return true;
	}
	const std::string key = xv::sound::toLower(node->key());
	if (key == "rndwave") {
		return true;
	}
	return document_.kind() == xv::sound::FileKind::Soundscape && (key == "playlooping" || key == "playrandom" || key == "playsoundscape");
}

void KvCard::play() {
	const Node* node = document_.node(path_);
	if (!player_ || !node) {
		return;
	}
	const auto lookup = [this](std::string_view name) { return index_.findSoundscape(name, &document_); };
	const bool soundscape = document_.kind() == xv::sound::FileKind::Soundscape;
	if (soundscape && path_.size() == 1) {
		player_->playSoundscape(xv::sound::planSoundscape(*node, lookup), qs(node->key()), document_.localRoot());
		return;
	}
	if (soundscape && xv::kv1::equalsIgnoreCase(node->key(), "playsoundscape")) {
		const Node* name = node->find("name");
		const Node* target = name ? lookup(name->value()) : nullptr;
		if (!target) {
			emit player_->failed(tr("No soundscape named “%1”").arg(name ? qs(name->value()) : QString{}));
			return;
		}
		player_->playSoundscape(xv::sound::planSoundscape(*target, lookup), qs(target->key()), document_.localRoot());
		return;
	}

	static std::mt19937 random{std::random_device{}()};
	NodePath parentPath = path_;
	parentPath.pop_back();
	const Node* parent = path_.size() > 1 ? document_.node(parentPath) : nullptr;
	if (const auto request = xv::sound::playBlock(document_.kind(), *node, parent, random)) {
		player_->play(*request, document_.localRoot());
	} else {
		emit player_->failed(tr("“%1” has no sound to play").arg(qs(node->key())));
	}
}

KvCard* KvCard::cardFor(const NodePath& path) {
	if (path == path_) {
		return this;
	}
	for (KvCard* child : children_) {
		if (KvCard* card = child->cardFor(path)) {
			return card;
		}
	}
	return nullptr;
}

QString defaultValue(const SoundDocument& document, std::string_view key) {
	using xv::kv1::equalsIgnoreCase;
	const bool script = document.kind() == xv::sound::FileKind::Soundscript;
	if (equalsIgnoreCase(key, "channel")) return QStringLiteral("CHAN_AUTO");
	if (equalsIgnoreCase(key, "volume")) return script ? QStringLiteral("VOL_NORM") : QStringLiteral("1");
	if (equalsIgnoreCase(key, "pitch")) return script ? QStringLiteral("PITCH_NORM") : QStringLiteral("100");
	if (equalsIgnoreCase(key, "soundlevel")) return QStringLiteral("SNDLVL_NORM");
	if (equalsIgnoreCase(key, "attenuation") || equalsIgnoreCase(key, "compatibilityattenuation")) return QStringLiteral("ATTN_NORM");
	if (equalsIgnoreCase(key, "dsp") || equalsIgnoreCase(key, "dsp_volume")) return QStringLiteral("1");
	if (equalsIgnoreCase(key, "time")) return QStringLiteral("10,20");
	if (equalsIgnoreCase(key, "origin")) return QStringLiteral("0 0 0");
	if (equalsIgnoreCase(key, "delay_msec")) return QStringLiteral("0");
	if (equalsIgnoreCase(key, "position") || equalsIgnoreCase(key, "positionoverride") || equalsIgnoreCase(key, "ambientpositionoverride")) return QStringLiteral("0");
	if (equalsIgnoreCase(key, "play_to_owner_only") || equalsIgnoreCase(key, "suppress_on_restore")) return QStringLiteral("1");
	return {};
}

Node blockTemplate(const SoundDocument& document, std::string_view key) {
	using xv::kv1::equalsIgnoreCase;
	Node block = Node::makeBlock(key);
	const auto add = [&](std::string_view name) {
		block.appendChild(Node::makePair(name, defaultValue(document, name).toStdString()));
	};
	if (equalsIgnoreCase(key, "playlooping")) {
		add("volume");
		add("pitch");
		add("wave");
	} else if (equalsIgnoreCase(key, "playrandom")) {
		add("time");
		add("volume");
		add("pitch");
		block.appendChild(blockTemplate(document, "rndwave"));
	} else if (equalsIgnoreCase(key, "playsoundscape")) {
		add("name");
		add("volume");
	} else if (equalsIgnoreCase(key, "rndwave")) {
		add("wave");
	}
	return block;
}

Node entryTemplate(const SoundDocument& document, std::string_view name) {
	Node entry = Node::makeBlock(name);
	const auto add = [&](std::string_view key) {
		entry.appendChild(Node::makePair(key, defaultValue(document, key).toStdString()));
	};
	if (document.kind() == xv::sound::FileKind::Soundscape) {
		add("dsp");
	} else {
		add("channel");
		add("volume");
		add("pitch");
		add("soundlevel");
		add("wave");
	}
	return entry;
}
