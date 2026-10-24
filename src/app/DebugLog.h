//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The debug log (Preferences > Advanced): what goes on in the background, such as
//          mounting, saving, reading files and drawing previews, printed to the terminal and
//          written to a file, for testing. Off, DebugLog() lines cost a check and print nothing.
//
//===========================================================================//
#pragma once

#include <QLoggingCategory>
#include <QString>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY( LOG_VOMIT )

// A line for the debug log: DebugLog() << "mounted" << nFiles << "files";
#define DebugLog() qCDebug( LOG_VOMIT ).noquote()

// Turns the log on or off; on, Qt's own warnings go to the file too.
void SetDebugLogging( bool bOn );
// Where the log is written (in the program's data folder).
[[nodiscard]] QString DebugLogPath();

#include <QElapsedTimer>

// Logs how long the scope took, when the log is on: CDebugTimer timer( "mounting the game" );
class CDebugTimer
{
public:
	explicit CDebugTimer( QString szWhat )
		: m_szWhat( std::move( szWhat ) )
	{
		if ( LOG_VOMIT().isDebugEnabled() )
		{
			m_Timer.start();
		}
	}
	~CDebugTimer()
	{
		if ( m_Timer.isValid() )
		{
			DebugLog() << m_szWhat << "took" << m_Timer.elapsed() << "ms";
		}
	}
	CDebugTimer( const CDebugTimer & ) = delete;
	CDebugTimer &operator=( const CDebugTimer & ) = delete;

private:
	QString m_szWhat;
	QElapsedTimer m_Timer;
};
