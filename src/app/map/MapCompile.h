//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Runs a compile sequence as Hammer's Run Map does: each command in turn (the
//          compilers waited for, their output streamed; the game started and left running;
//          Hammer's specials done here: a folder change, a copy, a delete, a rename), stopping
//          at a failure, at a leak (as Hammer++ does) or when a file a command must leave
//          isn't there. What the output said is reported at the end.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <QObject>
#include <QProcessEnvironment>
#include <QString>

#include "SourceTools.h"
#include "map/Compile.h"

class CCompileRunner : public QObject
{
	Q_OBJECT

public:
	explicit CCompileRunner( QObject *pParent = nullptr );

	// Runs the commands from the map's folder (where the compilers write beside the VMF).
	bool Start( std::vector<xv::map::ResolvedCommand_t> commands, const QString &szDirectory, const QString &szGameDirectory );
	void Cancel();
	[[nodiscard]] bool IsRunning() const { return m_bRunning; }
	[[nodiscard]] const QString &GetLog() const { return m_szLog; }
	[[nodiscard]] std::size_t GetCurrent() const { return m_nCurrent; }
	// Stops after a vbsp that reported a leak (Hammer++'s way); off, the sequence goes on as
	// Hammer's does.
	void SetStopOnLeak( bool bStop ) { m_bStopOnLeak = bStop; }

signals:
	void Output( const QString &szText );
	// The command at nIndex (into what Start was given) is about to run.
	void CommandStarted( std::size_t nIndex, const QString &szShown );
	void Finished( bool bSuccess, const xv::map::CompileReport_t &report );

private:
	void RunNext();
	void CommandDone( bool bSuccess );
	void Append( const QString &szText );
	void End( bool bSuccess );
	// The game started and left to itself, what it prints going into the log.
	void StartGame( const QString &szProgram, const QStringList &arguments );
	// The environment for the game: this process's without what an AppImage's AppRun put in it
	// (its own libraries and Qt would be found before the game's).
	[[nodiscard]] static QProcessEnvironment GameEnvironment();

	std::vector<xv::map::ResolvedCommand_t> m_Commands;
	std::size_t m_nCurrent = 0;
	QString m_szDirectory;
	QString m_szGameDirectory;
	QString m_szLog;
	CSourceToolRunner *m_pTool = nullptr;
	bool m_bRunning = false;
	bool m_bCancelled = false;
	bool m_bStopOnLeak = true;
};
