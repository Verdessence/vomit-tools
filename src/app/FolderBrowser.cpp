// SPDX-License-Identifier: GPL-3.0-or-later
#include "FolderBrowser.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

FolderBrowser::FolderBrowser(QWidget* parent)
		: QWidget(parent)
		, model_(new QFileSystemModel(this)) {
	model_->setReadOnly(true);
	model_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

	auto* open = new QToolButton;
	open->setText(tr("Open Folder…"));
	open->setToolButtonStyle(Qt::ToolButtonTextOnly);
	connect(open, &QToolButton::clicked, this, &FolderBrowser::openFolderRequested);
	auto* up = new QToolButton;
	up->setText(QStringLiteral("↑"));
	up->setToolTip(tr("Parent folder"));
	connect(up, &QToolButton::clicked, this, [this] {
		if (!folder_.isEmpty()) {
			const QString parent = QFileInfo{folder_}.dir().absolutePath();
			if (parent != folder_) {
				setFolder(parent);
			}
		}
	});
	path_ = new QLabel;
	path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	path_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	auto* bar = new QHBoxLayout;
	bar->setContentsMargins(0, 0, 0, 0);
	bar->addWidget(open);
	bar->addWidget(up);
	bar->addWidget(path_, 1);

	view_ = new QTreeView;
	view_->setModel(model_);
	view_->setUniformRowHeights(true);
	view_->setSortingEnabled(true);
	view_->sortByColumn(0, Qt::AscendingOrder);
	view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	view_->header()->setStretchLastSection(false);
	view_->setColumnHidden(2, true); // type
	view_->setToolTip(tr("Double-click a file to open it; soundscapes and soundscripts open in the editor"));
	connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
		if (index.isValid() && !model_->isDir(index)) {
			emit fileSelected(model_->filePath(index));
		}
	});
	connect(view_, &QTreeView::activated, this, [this](const QModelIndex& index) {
		if (index.isValid() && !model_->isDir(index)) {
			emit fileActivated(model_->filePath(index));
		}
	});

	auto* empty = new QLabel(tr("Open a folder (a mod, a custom/ folder...) to browse its files, or drop files and folders onto the window."));
	empty->setWordWrap(true);
	empty->setAlignment(Qt::AlignCenter);
	empty->setEnabled(false);
	empty_ = empty;

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(bar);
	layout->addWidget(view_, 1);
	layout->addWidget(empty_, 1);
	view_->hide();
}

void FolderBrowser::setFolder(const QString& folder) {
	folder_ = QDir::cleanPath(QFileInfo{folder}.absoluteFilePath());
	model_->setRootPath(folder_);
	view_->setRootIndex(model_->index(folder_));
	path_->setText(QDir::toNativeSeparators(folder_));
	path_->setToolTip(path_->text());
	view_->show();
	empty_->hide();
}
