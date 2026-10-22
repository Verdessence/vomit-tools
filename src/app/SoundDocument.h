// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <QObject>
#include <QString>

#include "kv1/Kv1.h"
#include "sound/SoundLint.h"
#include "sound/SoundSchema.h"

class QUndoStack;

// An open soundscape or soundscript file.
//
// Every change goes through the undo stack and swaps in a whole new copy of the top-level entry
// it touches, so undoing restores the file byte for byte, comments and spacing included.
class SoundDocument : public QObject {
	Q_OBJECT

public:
	SoundDocument(QString gamePath, xv::sound::FileKind kind, xv::kv1::ParseResult parsed, QObject* parent = nullptr);

	[[nodiscard]] const QString& gamePath() const { return gamePath_; }
	[[nodiscard]] QString fileName() const;
	[[nodiscard]] xv::sound::FileKind kind() const { return kind_; }
	[[nodiscard]] const xv::sound::Schema& schema() const { return *schema_; }
	[[nodiscard]] const xv::kv1::Document& document() const { return document_; }
	[[nodiscard]] const std::vector<xv::kv1::Diagnostic>& parseDiagnostics() const { return diagnostics_; }
	[[nodiscard]] QUndoStack* undoStack() const { return undo_; }
	[[nodiscard]] bool isModified() const;

	// The loose file Save writes to; empty when the file came from a VPK or is new.
	[[nodiscard]] const std::filesystem::path& diskPath() const { return diskPath_; }
	void setDiskPath(std::filesystem::path path) { diskPath_ = std::move(path); }
	// The game or mod folder holding this file's scripts/ (or maps/) folder, for finding its sounds on
	// disk when no game is mounted; empty if it isn't in one.
	[[nodiscard]] std::filesystem::path localRoot() const;
	[[nodiscard]] std::string serialize() const;
	void markSaved();
	void markUnsaved(); // a new document that has never been written
	[[nodiscard]] bool isNew() const { return neverSaved_; }

	[[nodiscard]] const xv::kv1::Node* node(const xv::sound::NodePath& path) const;
	// The schema block for the block at path; nullptr for blocks the schema doesn't know.
	[[nodiscard]] const xv::sound::BlockDef* blockDef(const xv::sound::NodePath& path) const;

	void setKey(const xv::sound::NodePath& path, const QString& key);
	void setValue(const xv::sound::NodePath& path, const QString& value);
	void insertChild(const xv::sound::NodePath& parent, std::size_t index, xv::kv1::Node child, const QString& description);
	void removeNode(const xv::sound::NodePath& path);
	void insertEntry(std::size_t index, xv::kv1::Node entry, const QString& description);

signals:
	void changed();
	void modifiedChanged(bool modified);

private:
	friend class ReplaceEntryCommand;
	friend class InsertEntryCommand;
	friend class RemoveEntryCommand;

	// Applies edit to a copy of top-level entry rootIndex and pushes the swap as one undo step.
	void editEntry(std::size_t rootIndex, const QString& description, const std::function<bool(xv::kv1::Node& entry)>& edit);

	QString gamePath_;
	xv::sound::FileKind kind_;
	const xv::sound::Schema* schema_;
	xv::kv1::Document document_;
	std::vector<xv::kv1::Diagnostic> diagnostics_;
	std::filesystem::path diskPath_;
	QUndoStack* undo_;
	bool neverSaved_ = false;
};
