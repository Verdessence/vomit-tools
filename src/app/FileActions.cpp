//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See FileActions.h.
//
//===========================================================================//
#include "FileActions.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>
#include <QWidget>

#ifdef Q_OS_WIN
// windows.h first: the shell headers need it.
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace
{

#ifdef Q_OS_WIN
//-----------------------------------------------------------------------------
// Purpose: Explorer's own menu for a file or folder, as its right-click shows it. False when the
//          shell can't give one.
//-----------------------------------------------------------------------------
bool ShowShellMenu( QWidget *pParent, const QPoint &globalAt, const QString &szPath )
{
	const QString szNative = QDir::toNativeSeparators( QFileInfo{ szPath }.absoluteFilePath() );
	PIDLIST_ABSOLUTE pidl = nullptr;
	if ( FAILED( SHParseDisplayName( reinterpret_cast<LPCWSTR>( szNative.utf16() ), nullptr, &pidl, 0, nullptr ) ) )
	{
		return false;
	}
	bool bShown = false;
	IShellFolder *pFolder = nullptr;
	PCUITEMID_CHILD pChild = nullptr;
	if ( SUCCEEDED( SHBindToParent( pidl, IID_IShellFolder, reinterpret_cast<void **>( &pFolder ), &pChild ) ) )
	{
		const HWND hWnd = reinterpret_cast<HWND>( pParent->window()->winId() );
		IContextMenu *pMenu = nullptr;
		if ( SUCCEEDED( pFolder->GetUIObjectOf( hWnd, 1, &pChild, IID_IContextMenu, nullptr, reinterpret_cast<void **>( &pMenu ) ) ) )
		{
			HMENU hMenu = CreatePopupMenu();
			if ( hMenu && SUCCEEDED( pMenu->QueryContextMenu( hMenu, 0, 1, 0x7FFF, CMF_NORMAL ) ) )
			{
				// The shell works in the screen's own pixels.
				const QPoint at = ( QPointF( globalAt ) * pParent->devicePixelRatioF() ).toPoint();
				const UINT nCommand = TrackPopupMenuEx( hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x(), at.y(), hWnd, nullptr );
				if ( nCommand > 0 )
				{
					CMINVOKECOMMANDINFOEX info{};
					info.cbSize = sizeof( info );
					info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
					info.hwnd = hWnd;
					info.lpVerb = MAKEINTRESOURCEA( nCommand - 1 );
					info.lpVerbW = MAKEINTRESOURCEW( nCommand - 1 );
					info.nShow = SW_SHOWNORMAL;
					info.ptInvoke = { at.x(), at.y() };
					pMenu->InvokeCommand( reinterpret_cast<LPCMINVOKECOMMANDINFO>( &info ) );
				}
				bShown = true;
			}
			if ( hMenu )
			{
				DestroyMenu( hMenu );
			}
			pMenu->Release();
		}
		pFolder->Release();
	}
	CoTaskMemFree( pidl );
	return bShown;
}
#endif

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Opens the folder holding a file in the system's file manager, the file selected.
//-----------------------------------------------------------------------------
void RevealInFileManager( const QString &szPath )
{
	const QFileInfo info{ szPath };
	const QString szAbsolute = info.absoluteFilePath();
#if defined( Q_OS_WIN )
	QProcess::startDetached( QStringLiteral( "explorer.exe" ), { QStringLiteral( "/select," ) + QDir::toNativeSeparators( szAbsolute ) } );
#elif defined( Q_OS_MACOS )
	QProcess::startDetached( QStringLiteral( "open" ), { QStringLiteral( "-R" ), szAbsolute } );
#else
	// The freedesktop file manager interface (Dolphin, Nautilus, Thunar, Nemo...) selects it;
	// without one, the folder opens.
	QProcess process;
	process.start( QStringLiteral( "dbus-send" ), { QStringLiteral( "--session" ), QStringLiteral( "--print-reply" ), QStringLiteral( "--dest=org.freedesktop.FileManager1" ), QStringLiteral( "--type=method_call" ), QStringLiteral( "/org/freedesktop/FileManager1" ), QStringLiteral( "org.freedesktop.FileManager1.ShowItems" ), QStringLiteral( "array:string:" ) + QUrl::fromLocalFile( szAbsolute ).toString( QUrl::FullyEncoded ), QStringLiteral( "string:" ) } );
	if ( process.waitForFinished( 3000 ) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 )
	{
		return;
	}
	QDesktopServices::openUrl( QUrl::fromLocalFile( info.isDir() ? szAbsolute : info.absolutePath() ) );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: The right-click menu for a file or folder on disk.
//-----------------------------------------------------------------------------
void ShowFileMenu( QWidget *pParent, const QPoint &globalAt, const QString &szPath, const std::function<void()> &pfnOpen )
{
#ifdef Q_OS_WIN
	if ( ShowShellMenu( pParent, globalAt, szPath ) )
	{
		return;
	}
#endif
	const QFileInfo info{ szPath };
	QMenu menu( pParent );
	if ( pfnOpen )
	{
		menu.addAction( info.isDir() ? QObject::tr( "Open Folder" ) : QObject::tr( "Open" ), pfnOpen );
	}
	menu.addAction( QObject::tr( "Open with Default App" ), [ szPath ] { QDesktopServices::openUrl( QUrl::fromLocalFile( szPath ) ); } );
	menu.addAction( QObject::tr( "Show in File Manager" ), [ szPath ] { RevealInFileManager( szPath ); } );
	menu.addSeparator();
	menu.addAction( QObject::tr( "Copy Path" ), [ szPath ] { QGuiApplication::clipboard()->setText( QDir::toNativeSeparators( szPath ) ); } );
	menu.addAction( QObject::tr( "Copy Name" ), [ info ] { QGuiApplication::clipboard()->setText( info.fileName() ); } );
	menu.addSeparator();
	menu.addAction( QObject::tr( "Rename…" ), [ pParent, info ] {
		bool bOk = false;
		const QString szName = QInputDialog::getText( pParent, QObject::tr( "Rename" ), QObject::tr( "New name:" ), QLineEdit::Normal, info.fileName(), &bOk ).trimmed();
		if ( !bOk || szName.isEmpty() || szName == info.fileName() )
		{
			return;
		}
		if ( szName.contains( u'/' ) || szName.contains( u'\\' ) || !QDir{ info.absolutePath() }.rename( info.fileName(), szName ) )
		{
			QMessageBox::warning( pParent, QObject::tr( "Rename" ), QObject::tr( "Couldn't rename %1 to %2." ).arg( info.fileName(), szName ) );
		}
	} );
	menu.addAction( QObject::tr( "Move to Trash" ), [ pParent, info ] {
		if ( QMessageBox::question( pParent, QObject::tr( "Move to Trash" ), QObject::tr( "Move %1 to the trash?" ).arg( info.fileName() ) ) != QMessageBox::Yes )
		{
			return;
		}
		if ( !QFile::moveToTrash( info.absoluteFilePath() ) )
		{
			QMessageBox::warning( pParent, QObject::tr( "Move to Trash" ), QObject::tr( "Couldn't move %1 to the trash." ).arg( info.fileName() ) );
		}
	} );
	menu.exec( globalAt );
}
