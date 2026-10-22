// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundDocument.h"

#include <QFileInfo>
#include <QUndoStack>

using xv::kv1::Node;
using xv::sound::NodePath;

namespace {

// The node at path inside a copy of its top-level entry (path[0] names the entry itself).
Node* descend(Node& entry, const NodePath& path) {
	Node* node = &entry;
	for (std::size_t i = 1; i < path.size(); i++) {
		if (!node->isBlock() || path[i] >= node->children().size()) {
			return nullptr;
		}
		node = &node->child(path[i]);
	}
	return node;
}

NodePath parentOf(NodePath path) {
	path.pop_back();
	return path;
}

} // namespace

class ReplaceEntryCommand : public QUndoCommand {
public:
	ReplaceEntryCommand(SoundDocument* document, std::size_t index, Node before, Node after, const QString& text)
			: QUndoCommand(text)
			, document_(document)
			, index_(index)
			, before_(std::move(before))
			, after_(std::move(after)) {}

	void redo() override {
		document_->document_.roots[index_] = after_;
		emit document_->changed();
	}

	void undo() override {
		document_->document_.roots[index_] = before_;
		emit document_->changed();
	}

private:
	SoundDocument* document_;
	std::size_t index_;
	Node before_;
	Node after_;
};

class InsertEntryCommand : public QUndoCommand {
public:
	InsertEntryCommand(SoundDocument* document, std::size_t index, Node entry, const QString& text)
			: QUndoCommand(text)
			, document_(document)
			, index_(index)
			, entry_(std::move(entry)) {}

	void redo() override {
		auto& doc = document_->document_;
		// The first time, lay it out like its neighbours; afterwards reuse exactly that layout.
		if (!laidOut_) {
			entry_ = doc.insertRoot(index_, entry_);
			laidOut_ = true;
		} else {
			doc.insertRoot(index_, entry_, xv::kv1::Layout::Keep);
		}
		emit document_->changed();
	}

	void undo() override {
		document_->document_.takeRoot(index_);
		emit document_->changed();
	}

private:
	SoundDocument* document_;
	std::size_t index_;
	Node entry_;
	bool laidOut_ = false;
};

class RemoveEntryCommand : public QUndoCommand {
public:
	RemoveEntryCommand(SoundDocument* document, std::size_t index, const QString& text)
			: QUndoCommand(text)
			, document_(document)
			, index_(index) {}

	void redo() override {
		removed_ = document_->document_.takeRoot(index_);
		emit document_->changed();
	}

	void undo() override {
		document_->document_.insertRoot(index_, removed_, xv::kv1::Layout::Keep);
		emit document_->changed();
	}

private:
	SoundDocument* document_;
	std::size_t index_;
	Node removed_;
};

SoundDocument::SoundDocument(QString gamePath, xv::sound::FileKind kind, xv::kv1::ParseResult parsed, QObject* parent)
		: QObject(parent)
		, gamePath_(std::move(gamePath))
		, kind_(kind)
		, schema_(xv::sound::schemaFor(kind))
		, document_(std::move(parsed.document))
		, diagnostics_(std::move(parsed.diagnostics))
		, undo_(new QUndoStack(this)) {
	Q_ASSERT(schema_);
	connect(undo_, &QUndoStack::cleanChanged, this, [this](bool clean) {
		emit modifiedChanged(!clean || neverSaved_);
	});
}

QString SoundDocument::fileName() const {
	return QFileInfo{gamePath_}.fileName();
}

std::filesystem::path SoundDocument::localRoot() const {
	if (diskPath_.empty()) {
		return {};
	}
	const auto folder = diskPath_.parent_path();
	const QString name = QString::fromStdU16String(folder.filename().u16string());
	if (name.compare(u"scripts", Qt::CaseInsensitive) == 0 || name.compare(u"maps", Qt::CaseInsensitive) == 0) {
		return folder.parent_path();
	}
	return {};
}

bool SoundDocument::isModified() const {
	return neverSaved_ || !undo_->isClean();
}

std::string SoundDocument::serialize() const {
	return xv::kv1::write(document_);
}

void SoundDocument::markSaved() {
	neverSaved_ = false;
	undo_->setClean();
	emit modifiedChanged(false);
}

void SoundDocument::markUnsaved() {
	neverSaved_ = true;
	emit modifiedChanged(true);
}

const Node* SoundDocument::node(const NodePath& path) const {
	if (path.empty() || path[0] >= document_.roots.size()) {
		return nullptr;
	}
	return descend(const_cast<Node&>(document_.roots[path[0]]), path);
}

const xv::sound::BlockDef* SoundDocument::blockDef(const NodePath& path) const {
	if (path.size() == 1) {
		return schema_->entry;
	}
	const Node* block = node(path);
	return block && block->isBlock() ? schema_->block(block->key()) : nullptr;
}

void SoundDocument::editEntry(std::size_t rootIndex, const QString& description, const std::function<bool(Node& entry)>& edit) {
	if (rootIndex >= document_.roots.size()) {
		return;
	}
	Node after = document_.roots[rootIndex];
	if (!edit(after)) {
		return;
	}
	undo_->push(new ReplaceEntryCommand(this, rootIndex, document_.roots[rootIndex], std::move(after), description));
}

void SoundDocument::setKey(const NodePath& path, const QString& key) {
	const Node* current = node(path);
	if (!current || current->key() == key.toStdString()) {
		return;
	}
	const QString description = path.size() == 1 ? tr("Rename %1").arg(key) : tr("Change key to %1").arg(key);
	editEntry(path[0], description, [&](Node& entry) {
		Node* target = descend(entry, path);
		return target && target->setKey(key.toStdString());
	});
}

void SoundDocument::setValue(const NodePath& path, const QString& value) {
	const Node* current = node(path);
	if (!current || !current->isPair() || current->value() == value.toStdString()) {
		return;
	}
	editEntry(path[0], tr("Set %1").arg(QString::fromStdString(current->key())), [&](Node& entry) {
		Node* target = descend(entry, path);
		return target && target->setValue(value.toStdString());
	});
}

void SoundDocument::insertChild(const NodePath& parent, std::size_t index, Node child, const QString& description) {
	if (parent.empty()) {
		return;
	}
	editEntry(parent[0], description, [&](Node& entry) {
		Node* block = descend(entry, parent);
		if (!block || !block->isBlock()) {
			return false;
		}
		block->insertChild(index, std::move(child));
		return true;
	});
}

void SoundDocument::removeNode(const NodePath& path) {
	const Node* target = node(path);
	if (!target) {
		return;
	}
	const QString description = tr("Delete %1").arg(QString::fromStdString(target->key()));
	if (path.size() == 1) {
		undo_->push(new RemoveEntryCommand(this, path[0], description));
		return;
	}
	editEntry(path[0], description, [&](Node& entry) {
		Node* parent = descend(entry, parentOf(path));
		if (!parent || !parent->isBlock() || path.back() >= parent->children().size()) {
			return false;
		}
		parent->removeChild(path.back());
		return true;
	});
}

void SoundDocument::insertEntry(std::size_t index, Node entry, const QString& description) {
	undo_->push(new InsertEntryCommand(this, std::min(index, document_.roots.size()), std::move(entry), description));
}
