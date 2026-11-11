//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Fog panel: the map's env_fog_controllers, one picked for the 3D views to show
//          (the game switches between them by inputs and trigger_fog; the views show one at a
//          time), the picked one's keys as fields under the list (each change one step to
//          undo, the views following at once), and the 3D skybox's fog (sky_camera's keys)
//          when the map has a sky_camera.
//
//===========================================================================//
#pragma once

#include <QWidget>

class CMapEditor;
class CMapEntityFields;
class QLabel;
class QListWidget;
class QPushButton;

class CMapFogPanel : public QWidget
{
	Q_OBJECT

public:
	explicit CMapFogPanel( CMapEditor &editor, QWidget *pParent = nullptr );

	// The controllers listed again from the map (after an edit or another map).
	void Refresh();

private:
	CMapEditor &m_Editor;
	QListWidget *m_pList = nullptr;
	QPushButton *m_pSelect = nullptr;
	QLabel *m_pNote = nullptr;
	CMapEntityFields *m_pFields = nullptr;
	CMapEntityFields *m_pSkyFields = nullptr;
	QWidget *m_pSkySection = nullptr;
	bool m_bFilling = false;
};
