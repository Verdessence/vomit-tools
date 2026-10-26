//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The desktop's file chooser through the XDG desktop portal
//          (org.freedesktop.portal.FileChooser over D-Bus), for FileDialogs on Linux. The portal
//          opens whichever chooser the desktop is set up with.
//
//===========================================================================//
#pragma once

#include <optional>

#include <QString>
#include <QStringList>

#include "FileDialogs.h"

class QWidget;

// Asks the portal's chooser, starting in szFolder with szName filled in for saving. The files or
// folder picked (empty when cancelled), or nothing when the portal can't show a chooser, so Qt's
// window can open instead. The filter chosen goes back into dialog.m_szFilter.
[[nodiscard]] std::optional<QStringList> AskPortalChooser( QWidget *pParent, FileDialog_t &dialog, const QString &szFolder, const QString &szName );
