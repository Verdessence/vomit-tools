//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A plain file tree for any folder on disk: a mod folder, a custom/ folder, a download...
//
//===========================================================================//
#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QSortFilterProxyModel;
class QTreeView;

class CFolderBrowser : public QWidget
{
	Q_OBJECT

public:
	explicit CFolderBrowser( QWidget *pParent = nullptr );

	void SetFolder( const QString &szFolder );
	[[nodiscard]] QString GetFolder() const { return m_szFolder; }
	// The file or folder picked in the list; empty for none.
	[[nodiscard]] QString GetSelectedPath() const;

signals:
	void FileSelected( const QString &szPath );  // single click: preview
	void FileActivated( const QString &szPath ); // double click or Enter: open
	void OpenFolderRequested();

private:
	QFileSystemModel *m_pModel = nullptr;
	QTreeView *m_pView = nullptr;
	QLabel *m_pPath = nullptr;
	QWidget *m_pEmpty = nullptr;
	QString m_szFolder;
};
