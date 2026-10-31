//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The lighting preview's window (Map > Lighting): how it bakes and shows (the
//          preferences, and Hammer++'s Bake Lighting), and the map's lights in a table whose
//          colour, brightness, falloff distances and cones are edited in place, one undo step
//          each.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QWidget>

class CMapEditor;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

class CMapLightingPanel : public QWidget
{
	Q_OBJECT

public:
	explicit CMapLightingPanel( CMapEditor &editor, QWidget *pParent = nullptr );

	// The lights listed again from the map (after an edit or another map).
	void Refresh();
	// The bake's progress, or what the last one made.
	void UpdateStatus();

private:
	// A row of the table: a light's own value, or a light_environment's ambient.
	struct Row_t
	{
		std::uint32_t m_nEntity = 0;
		std::string m_szKey; // "_light" or "_ambient"
	};
	void Edited( QTableWidgetItem *pItem );
	void PickColour( int nRow );
	// One key of one entity set, as one step to undo.
	void SetKey( std::uint32_t nEntity, const std::string &szKey, const std::string &szValue, const QString &szName );
	void LoadPreferences();
	void StorePreferences();

	CMapEditor &m_Editor;
	QDoubleSpinBox *m_pBrightness = nullptr;
	QSpinBox *m_pBounces = nullptr;
	QSpinBox *m_pLuxels = nullptr;
	QCheckBox *m_pHdr = nullptr;
	QCheckBox *m_pPropShadows = nullptr;
	QCheckBox *m_pAutoBake = nullptr;
	QLabel *m_pStatus = nullptr;
	QTableWidget *m_pTable = nullptr;
	std::vector<Row_t> m_Rows;
	bool m_bFilling = false;
};
