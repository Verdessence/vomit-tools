//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The settings Edit > Preferences changes, remembered between runs. The simple interface
//          is CInterfaceMode's; the rest live here.
//
//===========================================================================//
#pragma once

#include <QObject>

#include "Graphics.h"

class CPreferences : public QObject
{
	Q_OBJECT

public:
	[[nodiscard]] static CPreferences &Instance();

	// Whether every system in the particle editor's grid plays, rather than only the selected one.
	[[nodiscard]] bool IsGridAnimatingAll() const { return m_bGridAnimatingAll; }
	void SetGridAnimatingAll( bool bAll );

	// What the previews draw with. CGraphics reads it as the app starts, so a change takes effect
	// the next time.
	[[nodiscard]] GraphicsApi_t GetGraphicsApi() const { return m_nGraphicsApi; }
	void SetGraphicsApi( GraphicsApi_t nApi );

	// Whether files opened from outside (a file manager, the command line) join the window
	// already open rather than starting another.
	[[nodiscard]] bool IsSingleWindow() const { return m_bSingleWindow; }
	void SetSingleWindow( bool bSingle );

signals:
	void GridAnimationChanged( bool bAll );

private:
	CPreferences();

	bool m_bGridAnimatingAll = false;
	GraphicsApi_t m_nGraphicsApi = GraphicsApi_t::GRAPHICS_API_AUTO;
	bool m_bSingleWindow = true;
};
