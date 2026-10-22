// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QAbstractItemModel>

namespace xv::vfs {
class FileSystem;
class Mount;
}

// Folder tree of every file visible under the "game" path ID, with the mount each one is read from.
class VfsModel : public QAbstractItemModel {
	Q_OBJECT

public:
	enum Column { NameColumn, SourceColumn, ColumnCount };
	enum Role { PathRole = Qt::UserRole + 1, IsDirectoryRole };

	explicit VfsModel(QObject* parent = nullptr);
	~VfsModel() override;

	void rebuild(const xv::vfs::FileSystem& fileSystem);
	[[nodiscard]] int fileCount() const { return fileCount_; }
	[[nodiscard]] QModelIndex indexForPath(const std::string& path) const;

	[[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	[[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	[[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
	struct Node {
		QString name;
		std::string path; // full normalized path; empty for folders
		const xv::vfs::Mount* mount = nullptr;
		Node* parent = nullptr;
		int row = 0;
		std::vector<std::unique_ptr<Node>> children;
	};

	[[nodiscard]] Node* nodeAt(const QModelIndex& index) const;

	std::unique_ptr<Node> root_;
	int fileCount_ = 0;
};
