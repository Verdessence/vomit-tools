//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What a crash leaves behind: a log in the data folder's crashlogs (on Linux
//          ~/.local/share/vomit-tools/crashlogs, on Windows %LOCALAPPDATA%\vomit-tools\crashlogs)
//          with the reason, a stack trace, the version and the open tabs, shown at the next
//          start with the debug log's last lines. A C++ exception thrown out of an event is
//          caught instead: logged the same way and shown in a box while the app carries on.
//          After a real crash (a bad pointer) the app can't safely go on, so that box waits for
//          the next start.
//
//===========================================================================//
#pragma once

#include <QApplication>
#include <QString>
#include <QStringList>

// Catches what an event handler throws, so one bad event doesn't take the app down.
class CApplication : public QApplication
{
	Q_OBJECT

public:
	using QApplication::QApplication;

	bool notify( QObject *pReceiver, QEvent *pEvent ) override;
};

// Signal and exception handlers, set up once the application's name is known.
void InstallCrashHandler();
// What is open (one line a tab), written into a crash log; kept up to date by the main window.
void SetCrashContext( const QString &szContext );
[[nodiscard]] QString CrashLogFolder();
// Writes a log for an error the app survived; its path.
QString WriteErrorLog( const QString &szReason );
// Crash logs from earlier runs not yet shown, newest first, each with the debug log's last
// lines added when the log was on (before this run starts the debug log afresh).
[[nodiscard]] QStringList CollectCrashLogs();
// Shows the logs CollectCrashLogs found ("closed unexpectedly"), with Copy and Open Folder.
void ShowCrashLogs( QWidget *pParent, const QStringList &logs );
