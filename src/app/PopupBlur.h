//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Keeps Kvantum's popup blur from killing the app on Wayland. KWindowSystem before 6.32
//          leaves an inert blur object behind when a window's blur is turned off; once that
//          popup's surface is gone, turning the blur on again sends KWin a request for a destroyed
//          surface ("set_blur_region called for destroyed wl_surface") and KWin cuts the
//          connection. Kvantum turns menu and tooltip blur on when they show and off when they
//          hide, so opening menus in some order hits it. With those versions, popups are left
//          out of Kvantum's blurring: they keep its look, without the blur behind them.
//
//===========================================================================//
#pragma once

#include <optional>
#include <string_view>

class QApplication;

// The KWindowSystem version a line of /proc/self/maps names ("…libKF6WindowSystem.so.6.31.0");
// nullopt for other lines. As major * 10000 + minor * 100 + patch.
[[nodiscard]] std::optional<int> WindowSystemVersionIn( std::string_view szLine );

// Whether the blur has to be kept off popups: Wayland, the Kvantum style, and KWindowSystem
// before 6.32 loaded in this process.
[[nodiscard]] bool NeedsPopupBlurWorkaround();

// Installs the workaround when it's needed; true when installed.
bool InstallPopupBlurWorkaround( QApplication &app );
