//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The lighting preview's window (Map > Lighting): how it bakes and shows (the
//          preferences, and Hammer++'s Bake Lighting); the entities that set the map's look,
//          each in a folding section of its keys (the sun and sky, shadows, the tone map,
//          colour correction); and the map's lights in a short list, folded, whose picked light
//          is selected for Object Properties.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <vector>

#include <QWidget>

class CMapEditor;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QListWidget;
class QToolButton;

class CMapLightingPanel : public QWidget
{
	Q_OBJECT

public:
	explicit CMapLightingPanel( CMapEditor &editor, QWidget *pParent = nullptr );

	// The lights listed again from the map (after an edit or another map).
	void Refresh();
	void ResetToDefault();
	// The bake's progress, or what the last one made.
	void UpdateStatus();

private:
	void LoadPreferences();
	void StorePreferences();

	CMapEditor &m_Editor;
	QDoubleSpinBox *m_pBrightness = nullptr;
	QDoubleSpinBox *m_pMonitorGamma = nullptr;
	QSpinBox *m_pBounces = nullptr;
	QSpinBox *m_pLuxels = nullptr;
	QCheckBox *m_pHdr = nullptr;
	QCheckBox *m_pLightOnly = nullptr;
	QCheckBox *m_pPropShadows = nullptr;
	QCheckBox *m_pSmoothShadows = nullptr;
	QCheckBox *m_pAutoBake = nullptr;
	QCheckBox *m_pInRadius = nullptr;
	QDoubleSpinBox *m_pRadius = nullptr;
	QSpinBox *m_pDelay = nullptr;
	QSpinBox *m_pSkySamples = nullptr;
	QLabel *m_pStatus = nullptr;
	QListWidget *m_pLights = nullptr;
	QToolButton *m_pLightsHeader = nullptr;
	std::vector<std::uint32_t> m_LightEntities; // by row of m_pLights
	bool m_bFilling = false;
};
