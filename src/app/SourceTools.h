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
	SOURCE_TOOL_BSPZIP,
	SOURCE_TOOL_VBSP,
	SOURCE_TOOL_VVIS,
	SOURCE_TOOL_VRAD,
	SOURCE_TOOL_GAME, // what runs the game on a map: cstrike.sh, hl2.sh, hl2.exe
};
inline constexpr SourceTool_t SOURCE_TOOLS[] = { SourceTool_t::SOURCE_TOOL_STUDIOMDL, SourceTool_t::SOURCE_TOOL_BSPZIP, SourceTool_t::SOURCE_TOOL_VBSP, SourceTool_t::SOURCE_TOOL_VVIS, SourceTool_t::SOURCE_TOOL_VRAD, SourceTool_t::SOURCE_TOOL_GAME };
// The tool's name as Preferences shows it ("vbsp / VBSP++"), and its base file name.
[[nodiscard]] QString SourceToolLabel( SourceTool_t nTool );
[[nodiscard]] QString SourceToolBaseName( SourceTool_t nTool );
// An override from Preferences wins; else the ++ and stock names beside the game (its bin
// folders, BIN too), or the game's own launcher; else PATH; else what the game's Hammer
// configuration names, when that file exists.
[[nodiscard]] QString FindSourceTool( SourceTool_t nTool, const xv::vfs::CGameInfo &game, const QString &szOverride = {} );
[[nodiscard]] QString SourceToolSetting( SourceTool_t nTool );
// Where compiled maps go (Hammer's $bspdir): the setting, else the game's maps folder.
[[nodiscard]] QString BspDirSetting();
[[nodiscard]] QString FindBspDir( const xv::vfs::CGameInfo &game, const QString &szOverride = {} );
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
