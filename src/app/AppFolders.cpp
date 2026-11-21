//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See AppFolders.h.
//
//===========================================================================//
#include "AppFolders.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The application's name: vomit-tools for the app, the suite's for a test, so tests
//          keep out of the app's folders.
//-----------------------------------------------------------------------------
QString FolderName()
{
	const QString szName = QCoreApplication::applicationName();
	return szName.isEmpty() ? QStringLiteral( "vomit-tools" ) : szName;
}

//-----------------------------------------------------------------------------
// Purpose: Each entry of the old folder renamed into the new one; one already there stays
//          where it was, so nothing is overwritten.
//-----------------------------------------------------------------------------
void MoveUp( const QString &szNew )
{
	const QString szOld = QDir{ szNew }.filePath( FolderName() );
	QDir old{ szOld };
	if ( !old.exists() )
		return;
	for ( const QString &szName : old.entryList( QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot ) )
	{
		const QString szTo = QDir{ szNew }.filePath( szName );
		if ( !QFileInfo::exists( szTo ) )
			QDir{}.rename( old.filePath( szName ), szTo );
	}
	QDir{ szNew }.rmdir( FolderName() ); // only when empty
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString AppDataFolder()
{
	return QDir{ QStandardPaths::writableLocation( QStandardPaths::GenericDataLocation ) }.filePath( FolderName() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString AppCacheFolder()
{
	return QDir{ QStandardPaths::writableLocation( QStandardPaths::GenericCacheLocation ) }.filePath( FolderName() );
}

//-----------------------------------------------------------------------------
// Purpose: Before anything reads or writes the folders. Qt's own caches (the OpenGL shader
//          cache) still go where QStandardPaths' CacheLocation says.
//-----------------------------------------------------------------------------
void MoveOldAppFolders()
{
	MoveUp( AppDataFolder() );
	MoveUp( AppCacheFolder() );
}
