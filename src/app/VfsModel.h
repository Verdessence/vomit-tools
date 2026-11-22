//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Folder tree of every file visible under the "game" path ID, with the mount each one is
//          read from.
//
//===========================================================================//
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QAbstractItemModel>
#include <QCollator>
#include <QSortFilterProxyModel>

namespace xv::vfs
{
class CFileSystem;
class CMount;
} // namespace xv::vfs

// The game tree's sorting and search: folders always above files, names in natural order
// ("map2" before "map10"), sizes by their bytes.
class CVfsSortFilter : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	explicit CVfsSortFilter( QObject *pParent = nullptr );

protected:
	[[nodiscard]] bool lessThan( const QModelIndex &left, const QModelIndex &right ) const override;

private:
	QCollator m_Collator;
};

class CVfsModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	enum Column_t
	{
		COLUMN_NAME_COLUMN,
		COLUMN_TYPE_COLUMN,
		COLUMN_SIZE_COLUMN,
		COLUMN_SOURCE_COLUMN,
		COLUMN_COLUMN_COUNT
	};
	enum Role_t
	{
		ROLE_PATH_ROLE = Qt::UserRole + 1,
		ROLE_IS_DIRECTORY_ROLE,
		ROLE_SORT_ROLE, // what a column sorts by: sizes in bytes, the rest as text
	};

	explicit CVfsModel( QObject *pParent = nullptr );
	~CVfsModel() override;

	void Rebuild( const xv::vfs::CFileSystem &fileSystem );
	// Empty, before the mounts its files point at go (a remount clears them first).
	void Clear();
	[[nodiscard]] int GetFileCount() const { return m_nFileCount; }
	[[nodiscard]] QModelIndex IndexForPath( const std::string &szPath ) const;
	// Adds one file (or changes the mount an existing one is read from), keeping the rest of the tree.
	void AddFile( const std::string &szPath, const xv::vfs::CMount *pMount );

	[[nodiscard]] QModelIndex index( int nRow, int nColumn, const QModelIndex &parent = {} ) const override;
	[[nodiscard]] QModelIndex parent( const QModelIndex &child ) const override;
	[[nodiscard]] int rowCount( const QModelIndex &parent = {} ) const override;
	[[nodiscard]] int columnCount( const QModelIndex &parent = {} ) const override;
	[[nodiscard]] QVariant data( const QModelIndex &index, int nRole = Qt::DisplayRole ) const override;
	[[nodiscard]] QVariant headerData( int nSection, Qt::Orientation nOrientation, int nRole = Qt::DisplayRole ) const override;

private:
	struct Node_t
	{
		QString m_szName;
		std::string m_szPath; // full normalized path; empty for folders
		const xv::vfs::CMount *m_pMount = nullptr;
		Node_t *m_pParent = nullptr;
		int m_nRow = 0;
		std::vector<std::unique_ptr<Node_t>> m_Children;
	};

	[[nodiscard]] Node_t *NodeAt( const QModelIndex &index ) const;

	std::unique_ptr<Node_t> m_pRoot;
	int m_nFileCount = 0;
};
