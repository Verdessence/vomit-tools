//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Fog panel: the map's env_fog_controllers, one picked for the 3D views to show
//          (the game switches between them by inputs and trigger_fog; the views show one at a
//          time), and the picked one's entity a click away.
//
//===========================================================================//
#pragma once

#include <QWidget>

class CMapEditor;
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
	bool m_bFilling = false;
};
