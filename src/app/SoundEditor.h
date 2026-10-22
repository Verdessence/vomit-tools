// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <vector>

#include <QWidget>

#include "sound/SoundLint.h"

class KvCard;
class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class SoundDocument;
class SoundIndex;
class SoundPlayer;

// Editor for one soundscape or soundscript file: the list of entries on the left, the selected
// entry as a card on the right, and the linter's findings underneath.
class SoundEditor : public QWidget {
	Q_OBJECT

public:
	// player may be null: no playback then.
	SoundEditor(SoundDocument* document, const SoundIndex& index, SoundPlayer* player = nullptr, QWidget* parent = nullptr);

	[[nodiscard]] SoundDocument* document() const { return document_; }
	[[nodiscard]] const std::vector<xv::sound::Issue>& issues() const { return issues_; }
	[[nodiscard]] bool listedInManifest() const { return listedInManifest_; }

	// Re-run the linter, e.g. after the game was remounted.
	void relint();
	void selectEntry(std::size_t root);

signals:
	void addToManifestRequested(SoundDocument* document);

private:
	void scheduleRefresh();
	void refresh();
	void rebuildEntryList();
	void showEntry();
	void lint();
	void showIssueList();
	void jumpTo(const xv::sound::NodePath& path);
	void addEntry();
	void duplicateEntry();
	void deleteEntry();

	SoundDocument* document_;
	const SoundIndex& index_;
	SoundPlayer* player_;
	QLineEdit* filter_ = nullptr;
	QListWidget* entries_ = nullptr;
	QScrollArea* scroll_ = nullptr;
	QListWidget* issueList_ = nullptr;
	QLabel* summary_ = nullptr;
	QAction* duplicate_ = nullptr;
	QAction* delete_ = nullptr;
	QAction* addToManifest_ = nullptr;
	KvCard* card_ = nullptr;

	std::optional<std::size_t> current_;
	std::vector<xv::sound::Issue> issues_;
	std::optional<xv::sound::NodePath> pendingEdit_;
	bool listedInManifest_ = true;
	bool refreshQueued_ = false;
};
