// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundEditor.h"

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "KvCard.h"
#include "SoundDocument.h"
#include "SoundIndex.h"
#include "SoundPlayer.h"

using xv::sound::Issue;
using xv::sound::NodePath;

namespace {

QString qs(std::string_view text) {
	return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QIcon severityIcon(Issue::Severity severity) {
	switch (severity) {
		case Issue::Severity::Error:   return QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical);
		case Issue::Severity::Warning: return QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
		default:                       return QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation);
	}
}

QVariant pathToVariant(const NodePath& path) {
	QVariantList list;
	for (const std::size_t index : path) {
		list << QVariant::fromValue(static_cast<qulonglong>(index));
	}
	return list;
}

NodePath variantToPath(const QVariant& value) {
	NodePath path;
	for (const QVariant& index : value.toList()) {
		path.push_back(static_cast<std::size_t>(index.toULongLong()));
	}
	return path;
}

} // namespace

SoundEditor::SoundEditor(SoundDocument* document, const SoundIndex& index, SoundPlayer* player, QWidget* parent)
		: QWidget(parent)
		, document_(document)
		, index_(index)
		, player_(player) {
	const QString noun = qs(document_->schema().entryNoun);

	auto* toolbar = new QToolBar;
	toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
	if (player_) {
		QAction* play = toolbar->addAction(tr("▶ Play"), this, [this] {
			if (card_) {
				card_->play();
			}
		});
		play->setShortcut(QKeySequence{Qt::CTRL | Qt::Key_Return});
		play->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		play->setToolTip(document_->kind() == xv::sound::FileKind::Soundscape
			? tr("Play the selected soundscape (%1). Positions, distance falloff and DSP aren't simulated.").arg(play->shortcut().toString(QKeySequence::NativeText))
			: tr("Play the selected sound with its volume, pitch and random picks (%1)").arg(play->shortcut().toString(QKeySequence::NativeText)));
		QAction* stop = toolbar->addAction(tr("■ Stop"), player_, &SoundPlayer::stop);
		stop->setShortcut(QKeySequence{Qt::CTRL | Qt::Key_Period});
		stop->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		stop->setToolTip(tr("Stop all sounds (%1)").arg(stop->shortcut().toString(QKeySequence::NativeText)));
		addAction(play);
		addAction(stop);
		toolbar->addSeparator();
	}
	toolbar->addAction(tr("New %1…").arg(noun), this, &SoundEditor::addEntry);
	duplicate_ = toolbar->addAction(tr("Duplicate"), this, &SoundEditor::duplicateEntry);
	delete_ = toolbar->addAction(tr("Delete"), this, &SoundEditor::deleteEntry);
	toolbar->addSeparator();
	addToManifest_ = toolbar->addAction(tr("Add to Manifest"), this, [this] { emit addToManifestRequested(document_); });
	addToManifest_->setToolTip(tr("List this file in %1 so the game loads it").arg(qs(xv::sound::manifestPathFor(document_->kind()))));
	auto* spacer = new QWidget;
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	toolbar->addWidget(spacer);
	summary_ = new QLabel;
	toolbar->addWidget(summary_);

	filter_ = new QLineEdit;
	filter_->setPlaceholderText(tr("Filter %1s…").arg(noun));
	filter_->setClearButtonEnabled(true);
	entries_ = new QListWidget;
	entries_->setObjectName(QStringLiteral("entryList"));
	entries_->setUniformItemSizes(true);
	auto* left = new QWidget;
	auto* leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->addWidget(filter_);
	leftLayout->addWidget(entries_);

	scroll_ = new QScrollArea;
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);

	auto* top = new QSplitter;
	top->addWidget(left);
	top->addWidget(scroll_);
	top->setStretchFactor(1, 3);
	top->setSizes({240, 760});

	issueList_ = new QListWidget;
	issueList_->setObjectName(QStringLiteral("issueList"));
	auto* vertical = new QSplitter(Qt::Vertical);
	vertical->addWidget(top);
	vertical->addWidget(issueList_);
	vertical->setStretchFactor(0, 4);
	vertical->setStretchFactor(1, 1);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(toolbar);
	layout->addWidget(vertical);

	connect(filter_, &QLineEdit::textChanged, this, [this](const QString& text) {
		for (int row = 0; row < entries_->count(); row++) {
			QListWidgetItem* item = entries_->item(row);
			item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
		}
	});
	connect(entries_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
		if (!item) {
			return;
		}
		const auto root = static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
		if (current_ != root) {
			current_ = root;
			showEntry();
		}
	});
	connect(entries_, &QListWidget::itemDoubleClicked, this, [this] {
		if (card_) {
			if (const xv::kv1::Node* node = document_->node({*current_})) {
				bool ok = false;
				const QString name = QInputDialog::getText(this, tr("Rename"), tr("Name:"), QLineEdit::Normal, qs(node->key()), &ok);
				if (ok && !name.isEmpty()) {
					document_->setKey({*current_}, name);
				}
			}
		}
	});
	connect(issueList_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		jumpTo(variantToPath(item->data(Qt::UserRole)));
	});
	connect(document_, &SoundDocument::changed, this, &SoundEditor::scheduleRefresh);

	// Open on the first entry.
	for (std::size_t i = 0; i < document_->document().roots.size(); i++) {
		if (document_->document().roots[i].isBlock()) {
			current_ = i;
			break;
		}
	}
	refresh();
}

void SoundEditor::relint() {
	lint();
	rebuildEntryList();
	if (card_) {
		card_->showIssues(issues_);
	}
}

void SoundEditor::selectEntry(std::size_t root) {
	current_ = root;
	rebuildEntryList();
	showEntry();
}

// Edits arrive while a table is still inside its itemChanged handler; rebuilding the card
// right then would delete the table under it, so the rebuild waits for the event loop.
void SoundEditor::scheduleRefresh() {
	if (!refreshQueued_) {
		refreshQueued_ = true;
		QTimer::singleShot(0, this, &SoundEditor::refresh);
	}
}

void SoundEditor::refresh() {
	refreshQueued_ = false;
	const auto& roots = document_->document().roots;
	if (current_ && *current_ >= roots.size()) {
		current_ = roots.empty() ? std::nullopt : std::optional{roots.size() - 1};
	}
	lint();
	rebuildEntryList();
	showEntry();
}

void SoundEditor::lint() {
	const auto context = index_.lintContext(*document_);
	listedInManifest_ = context.listedInManifest.value_or(true);
	issues_ = xv::sound::lint(document_->document(), document_->kind(), context);
	addToManifest_->setEnabled(!listedInManifest_);
	showIssueList();
}

void SoundEditor::showIssueList() {
	issueList_->clear();
	int counts[3] = {};
	for (const auto& diagnostic : document_->parseDiagnostics()) {
		const auto severity = diagnostic.severity == xv::kv1::Diagnostic::Severity::Error ? Issue::Severity::Error : Issue::Severity::Warning;
		auto* item = new QListWidgetItem(severityIcon(severity), tr("Line %1: %2 (as loaded)").arg(diagnostic.line).arg(qs(diagnostic.message)), issueList_);
		item->setData(Qt::UserRole, pathToVariant({}));
		counts[static_cast<int>(severity)]++;
	}
	for (const Issue& issue : issues_) {
		QString where;
		if (!issue.path.empty()) {
			NodePath prefix;
			for (const std::size_t index : issue.path) {
				prefix.push_back(index);
				if (const xv::kv1::Node* node = document_->node(prefix)) {
					where += (where.isEmpty() ? QString{} : QStringLiteral(" › ")) + qs(node->key());
				}
			}
			where += QStringLiteral(": ");
		}
		auto* item = new QListWidgetItem(severityIcon(issue.severity), where + qs(issue.message), issueList_);
		item->setData(Qt::UserRole, pathToVariant(issue.path));
		counts[static_cast<int>(issue.severity)]++;
	}
	const int errors = counts[static_cast<int>(Issue::Severity::Error)];
	const int warnings = counts[static_cast<int>(Issue::Severity::Warning)];
	const auto count = [](int n, const QString& one, const QString& many) { return (n == 1 ? one : many).arg(n); };
	summary_->setText(errors + warnings == 0
		? tr("No problems found")
		: count(errors, tr("%1 error"), tr("%1 errors")) + QStringLiteral(" · ") + count(warnings, tr("%1 warning"), tr("%1 warnings")));
}

void SoundEditor::rebuildEntryList() {
	const QSignalBlocker blocker{entries_};
	entries_->clear();
	const auto& roots = document_->document().roots;
	for (std::size_t i = 0; i < roots.size(); i++) {
		if (!roots[i].isBlock()) {
			continue;
		}
		auto* item = new QListWidgetItem(qs(roots[i].key()), entries_);
		item->setData(Qt::UserRole, QVariant::fromValue(static_cast<qulonglong>(i)));
		std::optional<Issue::Severity> worst;
		for (const Issue& issue : issues_) {
			if (!issue.path.empty() && issue.path[0] == i && issue.severity != Issue::Severity::Info) {
				worst = std::max(worst.value_or(issue.severity), issue.severity);
			}
		}
		if (worst) {
			item->setIcon(severityIcon(*worst));
		}
		item->setHidden(!item->text().contains(filter_->text(), Qt::CaseInsensitive));
		if (current_ == i) {
			entries_->setCurrentItem(item);
		}
	}
	const bool selected = current_ && *current_ < roots.size() && roots[*current_].isBlock();
	duplicate_->setEnabled(selected);
	delete_->setEnabled(selected);
}

void SoundEditor::showEntry() {
	const int scroll = scroll_->verticalScrollBar()->value();
	const bool sameEntry = card_ && current_ && card_->path() == NodePath{*current_};

	auto* host = new QWidget;
	auto* layout = new QVBoxLayout(host);
	layout->setContentsMargins(12, 12, 12, 12);
	card_ = nullptr;
	const auto& roots = document_->document().roots;
	if (current_ && *current_ < roots.size() && roots[*current_].isBlock()) {
		card_ = new KvCard(*document_, index_, player_, {*current_});
		connect(card_, &KvCard::added, this, [this](const NodePath& path) { pendingEdit_ = path; });
		card_->showIssues(issues_);
		layout->addWidget(card_);
	} else {
		auto* empty = new QLabel(tr("Nothing selected. Use “New %1…” to add one.").arg(qs(document_->schema().entryNoun)));
		empty->setEnabled(false);
		layout->addWidget(empty);
	}
	layout->addStretch();
	scroll_->setWidget(host); // deletes the previous card

	if (sameEntry) {
		QTimer::singleShot(0, this, [this, scroll] { scroll_->verticalScrollBar()->setValue(scroll); });
	}
	if (pendingEdit_ && card_) {
		const NodePath path = *pendingEdit_;
		pendingEdit_.reset();
		QTimer::singleShot(0, this, [this, path] {
			if (!card_) {
				return;
			}
			if (!card_->beginEdit(path)) {
				if (KvCard* added = card_->cardFor(path)) {
					scroll_->ensureWidgetVisible(added);
				}
			}
		});
	}
}

void SoundEditor::jumpTo(const NodePath& path) {
	if (path.empty()) {
		return;
	}
	if (current_ != path[0]) {
		current_ = path[0];
		rebuildEntryList();
		showEntry();
	}
	QTimer::singleShot(0, this, [this, path] {
		if (!card_ || card_->beginEdit(path)) {
			return;
		}
		if (KvCard* card = card_->cardFor(path)) {
			scroll_->ensureWidgetVisible(card);
		}
	});
}

void SoundEditor::addEntry() {
	const QString noun = qs(document_->schema().entryNoun);
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("New %1").arg(noun), tr("Name:"), QLineEdit::Normal, {}, &ok);
	if (!ok || name.isEmpty()) {
		return;
	}
	const std::size_t index = current_ ? *current_ + 1 : document_->document().roots.size();
	document_->insertEntry(index, entryTemplate(*document_, name.toStdString()), tr("New %1").arg(name));
	current_ = index;
}

void SoundEditor::duplicateEntry() {
	if (!current_) {
		return;
	}
	xv::kv1::Node copy = document_->document().roots[*current_];
	const QString name = qs(copy.key()) + QStringLiteral("_copy");
	copy.setKey(name.toStdString());
	document_->insertEntry(*current_ + 1, std::move(copy), tr("Duplicate as %1").arg(name));
	current_ = *current_ + 1;
}

void SoundEditor::deleteEntry() {
	if (!current_) {
		return;
	}
	const QString name = qs(document_->document().roots[*current_].key());
	if (QMessageBox::question(this, tr("Delete"), tr("Delete “%1”?").arg(name)) == QMessageBox::Yes) {
		document_->removeNode({*current_});
	}
}
