//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The settings Edit > Preferences changes, remembered between runs. The simple interface
//          is CInterfaceMode's; the rest live here.
//
//===========================================================================//
#pragma once

#include <QObject>
#include <QStringList>

#include "Graphics.h"
#include "map/MapConstants.h"

// The map viewer's settings (Preferences > Maps). The defaults are Hammer++'s own.
struct MapPreferences_t
{
	QStringList m_Fgds;                                   // game data, in order; empty: the game's Hammer configuration's
	double m_flFov = mapstyle::FOV;                       // horizontal, in degrees
	double m_flSensitivity = mapstyle::MOUSE_SENSITIVITY; // mouse look
	double m_flForwardSpeed = mapstyle::MAX_FORWARD_SPEED;
	double m_flRenderDistance = mapstyle::FAR_PLANE;
	int m_nTextureSize = 1024;                            // a texture's largest side, as read
	bool m_bHideSmallGrid = true;
	int m_nHighlightEvery = mapstyle::GRID_HIGHLIGHT_EVERY;
	bool m_bDrawVertices = true;
	bool m_bModels2D = true;
	bool m_bGroupColors = true;
	bool m_bSkybox3D = true;

	friend bool operator==( const MapPreferences_t &, const MapPreferences_t & ) = default;
};

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

	// Whether saving a file the game's manifest doesn't list offers to add it.
	[[nodiscard]] bool IsManifestReminder() const { return m_bManifestReminder; }
	void SetManifestReminder( bool bRemind );

	// Whether the files open at closing open again at the next start.
	[[nodiscard]] bool IsReopenSession() const { return m_bReopenSession; }
	void SetReopenSession( bool bReopen );

	// Whether open and save windows are the desktop's own file chooser (the XDG portal's, on
	// Linux) rather than Qt's.
	[[nodiscard]] bool IsDesktopFileChooser() const { return m_bDesktopFileChooser; }
	void SetDesktopFileChooser( bool bDesktop );

	// Whether the debug log prints what goes on in the background (Advanced).
	[[nodiscard]] bool IsDebugLog() const { return m_bDebugLog; }
	void SetDebugLog( bool bOn );

	// The map viewer's: open maps take a change at once (their own toggles start over).
	[[nodiscard]] const MapPreferences_t &GetMapPreferences() const { return m_Map; }
	void SetMapPreferences( const MapPreferences_t &map );

	// Whether materials are edited as a list of parameters (else as their text).
	[[nodiscard]] bool IsMaterialVisual() const { return m_bMaterialVisual; }
	void SetMaterialVisual( bool bVisual );
	// Whether the material preview redraws at each edit made in the list (the text's always does).
	[[nodiscard]] bool IsMaterialLive() const { return m_bMaterialLive; }
	void SetMaterialLive( bool bLive );

signals:
	void GridAnimationChanged( bool bAll );
	void MaterialEditingChanged();
	void MapPreferencesChanged();

private:
	CPreferences();

	bool m_bGridAnimatingAll = false;
	GraphicsApi_t m_nGraphicsApi = GraphicsApi_t::GRAPHICS_API_AUTO;
	bool m_bSingleWindow = true;
	bool m_bMaterialVisual = true;
	bool m_bMaterialLive = true;
	bool m_bManifestReminder = true;
	bool m_bReopenSession = true;
	bool m_bDesktopFileChooser = true;
	bool m_bDebugLog = false;
	MapPreferences_t m_Map;
};
