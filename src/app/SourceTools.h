//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Discover user-owned Source tools and run them asynchronously without a command shell.
//===========================================================================//
#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
#include "vfs/Vfs.h"
enum class SourceTool_t
{
	SOURCE_TOOL_STUDIOMDL,
	SOURCE_TOOL_BSPZIP
};
[[nodiscard]] QString FindSourceTool( SourceTool_t nTool, const xv::vfs::CGameInfo &game, const QString &szOverride = {} );
[[nodiscard]] QString SourceToolSetting( SourceTool_t nTool );
// A private VProject preserves the real game's mounting information but writes compiled output here.
[[nodiscard]] bool WriteBuildGame( const xv::vfs::CGameInfo &game, const QString &szFolder, QString *pError = nullptr );
class CSourceToolRunner : public QObject
{
	Q_OBJECT
public:
	explicit CSourceToolRunner( QObject *pParent = nullptr );
	~CSourceToolRunner() override;
	bool Start( const QString &szProgram, const QStringList &arguments, const QString &szDirectory, const QString &szGameDirectory = {} );
	void Cancel();
	[[nodiscard]] bool IsRunning() const { return m_Process.state() != QProcess::NotRunning; }
	[[nodiscard]] QString GetLog() const { return m_szLog; }
signals:
	void Output( const QString &szText );
	void Finished( bool bSuccess );

private:
	void Append( const QString &szText );
	QProcess m_Process;
	QString m_szLog;
	bool m_bCancelled = false, m_bReported = false;
};
