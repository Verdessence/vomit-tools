// SPDX-License-Identifier: GPL-3.0-or-later
#include "VfsModel.h"

#include <algorithm>
#include <unordered_map>

#include <QApplication>
#include <QStyle>

#include "vfs/Vfs.h"

VfsModel::VfsModel(QObject* parent)
		: QAbstractItemModel(parent)
		, root_(std::make_unique<Node>()) {}

VfsModel::~VfsModel() = default;

void VfsModel::rebuild(const xv::vfs::FileSystem& fileSystem) {
	beginResetModel();
	root_ = std::make_unique<Node>();
	fileCount_ = 0;

	std::unordered_map<std::string, Node*> folders{{"", root_.get()}};
	for (const auto& [path, mount] : fileSystem.files("game")) {
		Node* parent = root_.get();
		std::size_t start = 0;
		for (auto slash = path.find('/'); slash != std::string::npos; slash = path.find('/', start)) {
			const std::string folder = path.substr(0, slash);
			auto [it, inserted] = folders.try_emplace(folder, nullptr);
			if (inserted) {
				auto node = std::make_unique<Node>();
				node->name = QString::fromStdString(path.substr(start, slash - start));
				node->parent = parent;
				it->second = parent->children.emplace_back(std::move(node)).get();
			}
			parent = it->second;
			start = slash + 1;
		}
		auto file = std::make_unique<Node>();
		file->name = QString::fromStdString(path.substr(start));
		file->path = path;
		file->mount = mount;
		file->parent = parent;
		parent->children.push_back(std::move(file));
		fileCount_++;
	}

	// Folders first, then by name; then record each node's row.
	const auto arrange = [](auto& self, Node& node) -> void {
		std::ranges::sort(node.children, [](const auto& a, const auto& b) {
			const bool aFolder = a->path.empty();
			const bool bFolder = b->path.empty();
			return aFolder != bFolder ? aFolder : a->name < b->name;
		});
		for (int row = 0; row < static_cast<int>(node.children.size()); row++) {
			node.children[static_cast<std::size_t>(row)]->row = row;
			self(self, *node.children[static_cast<std::size_t>(row)]);
		}
	};
	arrange(arrange, *root_);
	endResetModel();
}

QModelIndex VfsModel::indexForPath(const std::string& path) const {
	Node* node = root_.get();
	std::size_t start = 0;
	while (start <= path.size()) {
		const auto slash = path.find('/', start);
		const QString name = QString::fromStdString(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
		const auto it = std::ranges::find_if(node->children, [&name](const auto& child) { return child->name == name; });
		if (it == node->children.end()) {
			return {};
		}
		node = it->get();
		if (slash == std::string::npos) {
			break;
		}
		start = slash + 1;
	}
	return node == root_.get() ? QModelIndex{} : createIndex(node->row, 0, node);
}

VfsModel::Node* VfsModel::nodeAt(const QModelIndex& index) const {
	return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

QModelIndex VfsModel::index(int row, int column, const QModelIndex& parent) const {
	const Node* node = nodeAt(parent);
	if (row < 0 || column < 0 || column >= ColumnCount || row >= static_cast<int>(node->children.size())) {
		return {};
	}
	return createIndex(row, column, node->children[static_cast<std::size_t>(row)].get());
}

QModelIndex VfsModel::parent(const QModelIndex& child) const {
	const Node* node = nodeAt(child);
	if (!child.isValid() || node->parent == root_.get()) {
		return {};
	}
	return createIndex(node->parent->row, 0, node->parent);
}

int VfsModel::rowCount(const QModelIndex& parent) const {
	if (parent.column() > 0) {
		return 0;
	}
	return static_cast<int>(nodeAt(parent)->children.size());
}

int VfsModel::columnCount(const QModelIndex&) const {
	return ColumnCount;
}

QVariant VfsModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid()) {
		return {};
	}
	const Node* node = nodeAt(index);
	const bool folder = node->path.empty();
	switch (role) {
		case Qt::DisplayRole:
			if (index.column() == NameColumn) {
				return node->name;
			}
			return node->mount ? QString::fromStdString(node->mount->location().filename().string()) : QVariant{};
		case Qt::ToolTipRole:
			return node->mount ? QString::fromStdString(node->mount->location().string()) : QVariant{};
		case Qt::DecorationRole:
			if (index.column() == NameColumn) {
				return QApplication::style()->standardIcon(folder ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
			}
			return {};
		case PathRole:
			return QString::fromStdString(node->path);
		case IsDirectoryRole:
			return folder;
		default:
			return {};
	}
}

QVariant VfsModel::headerData(int section, Qt::Orientation orientation, int role) const {
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
		return {};
	}
	return section == NameColumn ? tr("Name") : tr("Read from");
}
