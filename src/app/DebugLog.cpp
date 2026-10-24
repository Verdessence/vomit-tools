//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Debug log to the terminal and a file.
//
//===========================================================================//
#include "DebugLog.h"

#include <cstdio>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>

Q_LOGGING_CATEGORY( LOG_VOMIT, "vomit", QtWarningMsg )

namespace
{

QMutex g_Mutex;
QFile *g_pFile = nullptr;
QtMessageHandler g_pfnPrevious = nullptr;
bool g_bInstalled = false;

//-----------------------------------------------------------------------------
// Purpose: Every message, timed, to stderr and the file; then whatever handled them before.
//-----------------------------------------------------------------------------
void Handle( QtMsgType nType, const QMessageLogContext &context, const QString &szMessage )
{
	static constexpr const char *LEVELS[] = { "debug", "warning", "critical", "fatal", "info" };
	const QString szLine = QStringLiteral( "%1 [%2] %3\n" ).arg( QTime::currentTime().toString( QStringLiteral( "HH:mm:ss.zzz" ) ), QLatin1String( LEVELS[ nType ] ), szMessage );
	{
		const QMutexLocker lock( &g_Mutex );
		if ( g_pFile )
		{
			g_pFile->write( szLine.toUtf8() );
			g_pFile->flush();
		}
	}
	if ( nType == QtDebugMsg )
	{
		std::fputs( szLine.toLocal8Bit().constData(), stderr );
		return;
	}
	if ( g_pfnPrevious )
	{
		g_pfnPrevious( nType, context, szMessage );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString DebugLogPath()
{
	return QDir{ QStandardPaths::writableLocation( QStandardPaths::AppLocalDataLocation ) }.filePath( QStringLiteral( "debug.log" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetDebugLogging( bool bOn )
{
	const_cast<QLoggingCategory &>( LOG_VOMIT() ).setEnabled( QtDebugMsg, bOn );
	const QMutexLocker lock( &g_Mutex );
	if ( bOn && !g_bInstalled )
	{
		QDir{}.mkpath( QFileInfo{ DebugLogPath() }.absolutePath() );
		g_pFile = new QFile( DebugLogPath() );
		// Each run starts the file afresh, so it stays small.
		if ( !g_pFile->open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
		{
			delete g_pFile;
			g_pFile = nullptr;
		}
		g_pfnPrevious = qInstallMessageHandler( Handle );
		g_bInstalled = true;
	}
	else if ( !bOn && g_bInstalled )
	{
		qInstallMessageHandler( g_pfnPrevious );
		g_pfnPrevious = nullptr;
		g_bInstalled = false;
		delete g_pFile;
		g_pFile = nullptr;
	}
}
