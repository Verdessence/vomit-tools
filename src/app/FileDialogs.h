//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The windows that pick files and folders to open or save. On Linux they are the
//          desktop's own file chooser, asked for through the XDG desktop portal, so each user
//          gets the one their desktop is set up with (KDE's, GNOME's, a terminal file
//          manager's...); elsewhere, or with no portal, Qt's. Every one of them starts in the
//          folder the last one finished in, unless it is given a place of its own.
//
//===========================================================================//
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

class QWidget;

enum FileDialogMode_t
{
	FILE_DIALOG_MODE_OPEN,      // one file that exists
	FILE_DIALOG_MODE_OPEN_MANY, // one or more files that exist
	FILE_DIALOG_MODE_SAVE,      // a file to write, new or not
	FILE_DIALOG_MODE_FOLDER,    // a folder that exists
};

struct FileDialog_t
{
	FileDialogMode_t m_nMode = FILE_DIALOG_MODE_OPEN;
	QString m_szTitle;
	// A folder to start in, or a file: its folder, with its name filled in or picked. A bare name
	// starts in the last folder with the name filled in; nothing starts in the last folder.
	QString m_szStart;
	// In Qt's form: "Pictures (*.png *.tga);;All files (*)".
	QString m_szFilters;
	// The filter shown first. The one chosen comes back here.
	QString m_szFilter;
	// More places for Qt's side list. The desktop's chooser shows the user's own places instead.
	QList<QUrl> m_Places;
	// The object name Qt's window gets, so tests can find it.
	QString m_szObjectName;
};

// The files, or the folder, picked; empty when the window is cancelled.
[[nodiscard]] QStringList AskForFiles( QWidget *pParent, FileDialog_t &dialog );

// The usual cases, with m_szStart and m_szFilters as in FileDialog_t.
[[nodiscard]] QString AskOpenFile( QWidget *pParent, const QString &szTitle, const QString &szStart = {}, const QString &szFilters = {} );
[[nodiscard]] QStringList AskOpenFiles( QWidget *pParent, const QString &szTitle, const QString &szStart = {}, const QString &szFilters = {} );
[[nodiscard]] QString AskSaveFile( QWidget *pParent, const QString &szTitle, const QString &szStart = {}, const QString &szFilters = {}, QString *pFilter = nullptr );
[[nodiscard]] QString AskFolder( QWidget *pParent, const QString &szTitle, const QString &szStart = {} );

// The folder the last of these windows finished in, or szFallback before any has been used or
// once that folder is gone.
[[nodiscard]] QString LastFolderOr( const QString &szFallback );

// Whether these windows are the desktop's chooser this run. Preferences > Interface turns it off;
// VOMIT_TOOLS_FILE_DIALOG=qt or =portal overrides both ways.
[[nodiscard]] bool IsUsingDesktopChooser();
