// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QSortFilterProxyModel;
class QTreeView;

// A plain file tree for any folder on disk: a mod folder, a custom/ folder, a download...
class FolderBrowser : public QWidget {
	Q_OBJECT

public:
	explicit FolderBrowser(QWidget* parent = nullptr);

	void setFolder(const QString& folder);
	[[nodiscard]] QString folder() const { return folder_; }

signals:
	void fileSelected(const QString& path);  // single click: preview
	void fileActivated(const QString& path); // double click or Enter: open
	void openFolderRequested();

private:
	QFileSystemModel* model_ = nullptr;
	QTreeView* view_ = nullptr;
	QLabel* path_ = nullptr;
	QWidget* empty_ = nullptr;
	QString folder_;
};
