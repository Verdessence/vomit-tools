//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Preferences > Keys: the map views' keys, from a preset (Hammer++, Hammer, Blender)
//          or the user's own, every action's keys changeable and clashes shown.
//
//===========================================================================//
#pragma once

#include <functional>

#include <QWidget>

#include "Preferences.h"

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
	// With a stage (Preferences), a change waits there to be applied and the page shows it
	// meanwhile; without, it applies at once.
	void SetStage( std::function<void( std::function<void()> )> pfnStage ) { m_pfnStage = std::move( pfnStage ); }
	// Shows those settings' keys as the ones being edited (a restore waiting for Apply).
	void Show( const MapPreferences_t &preferences );

private:
	void Fill();
	void ShowSelected();
	void KeyEdited();
	void ApplyFilter();
	// The keys being edited become these, waiting on the stage or applied.
	void Change( MapKeyPreset_t nPreset, QStringList customKeys );
	[[nodiscard]] std::optional<MapAction_t> GetSelectedAction() const;

	QComboBox *m_pPreset = nullptr;
	QPushButton *m_pCopy = nullptr;
	QLineEdit *m_pFilter = nullptr;
	QTreeWidget *m_pTree = nullptr;
	QKeySequenceEdit *m_pKey = nullptr;
	QKeySequenceEdit *m_pAlternative = nullptr;
	QLabel *m_pClashes = nullptr;
	bool m_bFilling = false;
	std::function<void( std::function<void()> )> m_pfnStage;
	bool m_bChanged = false; // a change waits on the stage, so the preferences' keys aren't shown
	MapKeyPreset_t m_nPreset = MapKeyPreset_t::MAP_KEY_PRESET_HAMMER_PLUS_PLUS;
	QStringList m_CustomKeys;
};
