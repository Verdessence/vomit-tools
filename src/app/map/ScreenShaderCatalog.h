//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the Screenspace Shaders panel picks from: every screenspace_general material in
//          a game's files, with the pixel shader each names and whether that .vcs is there, and
//          every .vcs under shaders/fxc. Reading every VMT takes seconds, so it is read once a
//          game, on a thread of its own.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <QString>
#include <QStringList>

namespace xv::vfs
{
struct CGameInfo;
}

struct ScreenMaterial_t
{
	QString m_szMaterial;    // under materials/, no extension ("effects/shaders/example_glitch")
	QString m_szPixelShader; // as its $pixshader names it
	QString m_szShaderPath;  // the .vcs found for it; empty when the game hasn't it
};

struct ScreenCatalog_t
{
	std::vector<ScreenMaterial_t> m_Materials; // by name
	QStringList m_Shaders;                     // every .vcs under shaders/fxc, by name
};

// Reads the catalog for a game (stopping early, with what it has, when bStop turns true).
[[nodiscard]] ScreenCatalog_t ReadScreenCatalog( const xv::vfs::CGameInfo &game, const std::atomic<bool> &bStop );
// The catalog for a game, read on a thread the first time and kept for the run: pfnDone gets it
// on the main thread (at once when it was read before). pReceiver's going away drops the call.
void RequestScreenCatalog( const xv::vfs::CGameInfo &game, const class QObject *pReceiver, std::function<void( std::shared_ptr<const ScreenCatalog_t> )> pfnDone );
