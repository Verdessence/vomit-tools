//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See FileDialogs.h. The portal's side is in PortalFileChooser.cpp.
//
//===========================================================================//
#include "FileDialogs.h"

#include <utility>

#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QWidget>

#include "Preferences.h"

#ifdef VOMIT_TOOLS_PORTAL
#include "PortalFileChooser.h"
#endif

namespace
{

constexpr auto FOLDER_KEY = "fileDialogFolder";

//-----------------------------------------------------------------------------
// Purpose: Where a window starts: its folder, and a file name to fill in or pick (or none).
//-----------------------------------------------------------------------------
std::pair<QString, QString> SplitStart( const FileDialog_t &dialog )
{
	const QString szStart = QDir::fromNativeSeparators( dialog.m_szStart );
	const QString szLast = LastFolderOr( QDir::homePath() );
	const bool bFolderMode = dialog.m_nMode == FILE_DIALOG_MODE_FOLDER;
	if ( szStart.isEmpty() )
	{
		return { szLast, {} };
	}
	if ( !szStart.contains( u'/' ) )
	{
		return { szLast, bFolderMode ? QString{} : szStart };
	}
	const QFileInfo info{ szStart };
	if ( info.isDir() )
	{
		return { info.absoluteFilePath(), {} };
	}
	// A file, or a place not made yet: the nearest folder above it that is there.
	QString szFolder = info.absolutePath();
	while ( !QFileInfo{ szFolder }.isDir() )
	{
		const QString szUp = QFileInfo{ szFolder }.absolutePath();
		if ( szUp == szFolder )
		{
			return { szLast, {} };
		}
		szFolder = szUp;
	}
	const bool bKeepName = !bFolderMode && ( dialog.m_nMode == FILE_DIALOG_MODE_SAVE || info.exists() );
	return { szFolder, bKeepName ? info.fileName() : QString{} };
}

//-----------------------------------------------------------------------------
// Purpose: Remembers the folder a window finished in, for the next one to start there.
//-----------------------------------------------------------------------------
void RememberFolder( FileDialogMode_t nMode, const QStringList &picked )
{
	if ( picked.isEmpty() )
	{
		return;
	}
	const QFileInfo info{ picked.front() };
	QSettings{}.setValue( FOLDER_KEY, nMode == FILE_DIALOG_MODE_FOLDER ? info.absoluteFilePath() : info.absolutePath() );
}

//-----------------------------------------------------------------------------
// Purpose: Qt's file window: the platform's own where Qt has one, else Qt's widgets.
//-----------------------------------------------------------------------------
QStringList AskQt( QWidget *pParent, FileDialog_t &dialog, const QString &szFolder, const QString &szName )
{
	// A file named with the folder is picked by the window as it starts.
	QFileDialog fileDialog( pParent, dialog.m_szTitle, szName.isEmpty() ? szFolder : QDir{ szFolder }.filePath( szName ), dialog.m_szFilters );
	if ( !dialog.m_szObjectName.isEmpty() )
	{
		fileDialog.setObjectName( dialog.m_szObjectName );
	}
	switch ( dialog.m_nMode )
	{
		case FILE_DIALOG_MODE_OPEN:
			fileDialog.setFileMode( QFileDialog::ExistingFile );
			break;
		case FILE_DIALOG_MODE_OPEN_MANY:
			fileDialog.setFileMode( QFileDialog::ExistingFiles );
			break;
		case FILE_DIALOG_MODE_SAVE:
			fileDialog.setAcceptMode( QFileDialog::AcceptSave );
			fileDialog.setFileMode( QFileDialog::AnyFile );
			break;
		case FILE_DIALOG_MODE_FOLDER:
			fileDialog.setFileMode( QFileDialog::Directory );
			fileDialog.setOption( QFileDialog::ShowDirsOnly );
			break;
	}
	if ( !dialog.m_szFilter.isEmpty() )
	{
		fileDialog.selectNameFilter( dialog.m_szFilter );
	}
	if ( !dialog.m_Places.isEmpty() )
	{
		fileDialog.setSidebarUrls( dialog.m_Places );
	}
	if ( fileDialog.exec() != QDialog::Accepted )
	{
		return {};
	}
	dialog.m_szFilter = fileDialog.selectedNameFilter();
	return fileDialog.selectedFiles();
}

//-----------------------------------------------------------------------------
// Purpose: A window's settings for the usual cases.
//-----------------------------------------------------------------------------
FileDialog_t MakeDialog( FileDialogMode_t nMode, const QString &szTitle, const QString &szStart, const QString &szFilters = {} )
{
	FileDialog_t dialog;
	dialog.m_nMode = nMode;
	dialog.m_szTitle = szTitle;
	dialog.m_szStart = szStart;
	dialog.m_szFilters = szFilters;
	return dialog;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The files, or the folder, picked; empty when the window is cancelled.
//-----------------------------------------------------------------------------
QStringList AskForFiles( QWidget *pParent, FileDialog_t &dialog )
{
	const auto [ szFolder, szName ] = SplitStart( dialog );
	QStringList picked;
	bool bAsked = false;
#ifdef VOMIT_TOOLS_PORTAL
	if ( IsUsingDesktopChooser() )
	{
		if ( std::optional<QStringList> files = AskPortalChooser( pParent, dialog, szFolder, szName ) )
		{
			picked = std::move( *files );
			bAsked = true;
		}
	}
#endif
	if ( !bAsked )
	{
		picked = AskQt( pParent, dialog, szFolder, szName );
	}
	RememberFolder( dialog.m_nMode, picked );
	return picked;
}

//-----------------------------------------------------------------------------
// Purpose: One file to open.
//-----------------------------------------------------------------------------
QString AskOpenFile( QWidget *pParent, const QString &szTitle, const QString &szStart, const QString &szFilters )
{
	FileDialog_t dialog = MakeDialog( FILE_DIALOG_MODE_OPEN, szTitle, szStart, szFilters );
	const QStringList files = AskForFiles( pParent, dialog );
	return files.isEmpty() ? QString{} : files.front();
}

//-----------------------------------------------------------------------------
// Purpose: Files to open.
//-----------------------------------------------------------------------------
QStringList AskOpenFiles( QWidget *pParent, const QString &szTitle, const QString &szStart, const QString &szFilters )
{
	FileDialog_t dialog = MakeDialog( FILE_DIALOG_MODE_OPEN_MANY, szTitle, szStart, szFilters );
	return AskForFiles( pParent, dialog );
}

//-----------------------------------------------------------------------------
// Purpose: A file to write. pFilter, when given, is the filter shown first and gets the one
//          chosen.
//-----------------------------------------------------------------------------
QString AskSaveFile( QWidget *pParent, const QString &szTitle, const QString &szStart, const QString &szFilters, QString *pFilter )
{
	FileDialog_t dialog = MakeDialog( FILE_DIALOG_MODE_SAVE, szTitle, szStart, szFilters );
	if ( pFilter )
	{
		dialog.m_szFilter = *pFilter;
	}
	const QStringList files = AskForFiles( pParent, dialog );
	if ( files.isEmpty() )
	{
		return {};
	}
	if ( pFilter )
	{
		*pFilter = dialog.m_szFilter;
	}
	return files.front();
}

//-----------------------------------------------------------------------------
// Purpose: A folder.
//-----------------------------------------------------------------------------
QString AskFolder( QWidget *pParent, const QString &szTitle, const QString &szStart )
{
	FileDialog_t dialog = MakeDialog( FILE_DIALOG_MODE_FOLDER, szTitle, szStart );
	const QStringList folders = AskForFiles( pParent, dialog );
	return folders.isEmpty() ? QString{} : folders.front();
}

//-----------------------------------------------------------------------------
// Purpose: The folder the last window finished in, while it is still there.
//-----------------------------------------------------------------------------
QString LastFolderOr( const QString &szFallback )
{
	const QString szFolder = QSettings{}.value( FOLDER_KEY ).toString();
	return !szFolder.isEmpty() && QFileInfo{ szFolder }.isDir() ? szFolder : szFallback;
}

//-----------------------------------------------------------------------------
// Purpose: Whether the windows go to the desktop's chooser first this run.
//-----------------------------------------------------------------------------
bool IsUsingDesktopChooser()
{
#ifdef VOMIT_TOOLS_PORTAL
	const QByteArray choice = qgetenv( "VOMIT_TOOLS_FILE_DIALOG" ).toLower();
	if ( choice == "qt" || choice == "portal" )
	{
		return choice == "portal";
	}
	// Test runs and other platforms without a screen have no desktop to ask.
	const QString szPlatform = QGuiApplication::platformName();
	if ( szPlatform == u"offscreen" || szPlatform == u"minimal" )
	{
		return false;
	}
	return CPreferences::Instance().IsDesktopFileChooser();
#else
	return false;
#endif
}
