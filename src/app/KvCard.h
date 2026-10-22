// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include <QWidget>

#include "kv1/Kv1.h"
#include "sound/SoundLint.h"

class QLabel;
class QMenu;
class QTableWidget;
class SoundDocument;
class SoundIndex;
class SoundPlayer;

// One block drawn as a card: a tab with its name, a key/value grid under it, and its
// sub-blocks as nested cards. Edits go straight to the document as undoable commands.
class KvCard : public QWidget {
	Q_OBJECT

public:
	// player may be null: no play buttons then.
	KvCard(SoundDocument& document, const SoundIndex& index, SoundPlayer* player, xv::sound::NodePath path, QWidget* parent = nullptr);

	[[nodiscard]] const xv::sound::NodePath& path() const { return path_; }

	// Tints the rows and tabs that issues point at, here and in nested cards.
	void showIssues(const std::vector<xv::sound::Issue>& issues);
	// Opens the value editor of the pair at path, if it is in this card or a nested one.
	bool beginEdit(const xv::sound::NodePath& path);
	// The card for the block at path, or nullptr.
	[[nodiscard]] KvCard* cardFor(const xv::sound::NodePath& path);
	// Plays this block with its settings; a soundscape entry plays as a whole soundscape.
	void play();
	[[nodiscard]] bool playable() const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

signals:
	// A key or block was added; the editor reopens it for editing once the cards are rebuilt.
	void added(const xv::sound::NodePath& path);

private:
	void buildTable(const xv::kv1::Node& node);
	void fillAddMenu(QMenu* menu);
	void addKey(const QString& key);
	void addBlock(const QString& key);
	void removeRow(int row);
	void rename();
	[[nodiscard]] std::size_t pairInsertIndex() const;

	SoundDocument& document_;
	const SoundIndex& index_;
	SoundPlayer* player_;
	xv::sound::NodePath path_;
	QLabel* tab_ = nullptr;
	QTableWidget* table_ = nullptr;
	std::vector<std::size_t> rowChild_; // table row -> child index in the block
	std::vector<KvCard*> children_;
	bool filling_ = false;
};

// Starting content for new blocks and entries, so they are valid as soon as they are added.
[[nodiscard]] xv::kv1::Node blockTemplate(const SoundDocument& document, std::string_view key);
[[nodiscard]] xv::kv1::Node entryTemplate(const SoundDocument& document, std::string_view name);
[[nodiscard]] QString defaultValue(const SoundDocument& document, std::string_view key);
