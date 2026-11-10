//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Preferences > Keys: the map views' keys, from a preset (Hammer++, Hammer, Blender)
//          or the user's own, every action's keys changeable and clashes shown.
//
//===========================================================================//
#pragma once

#include <QWidget>

#include "map/MapKeys.h"

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class CMapKeysPage : public QWidget
{
	Q_OBJECT

public:
	explicit CMapKeysPage( QWidget *pParent = nullptr );

	// Gives the action its keys (an empty combination for none), making the keys the user's
	// own first if a preset was in use.
	void SetKeys( MapAction_t nAction, QKeyCombination key, QKeyCombination alternative );

private:
	void Fill();
	void ShowSelected();
	void KeyEdited();
	void ApplyFilter();
	[[nodiscard]] std::optional<MapAction_t> GetSelectedAction() const;

	QComboBox *m_pPreset = nullptr;
	QPushButton *m_pCopy = nullptr;
	QLineEdit *m_pFilter = nullptr;
	QTreeWidget *m_pTree = nullptr;
	QKeySequenceEdit *m_pKey = nullptr;
	QKeySequenceEdit *m_pAlternative = nullptr;
	QLabel *m_pClashes = nullptr;
	bool m_bFilling = false;
};
