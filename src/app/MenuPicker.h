//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Menus for long lists: a search box over a list no taller than a quarter of the screen,
//          where a plain menu of hundreds of entries would run off it.
//
//===========================================================================//
#pragma once

#include <functional>
#include <vector>

#include <QString>

class QMenu;

struct PickerItem_t
{
	QString m_szText;
	QString m_szTip;
	std::function<void()> m_pfnRun;
};

// Fills menu with the picker (szEmpty, greyed, when there are no items).
void FillPicker( QMenu *pMenu, std::vector<PickerItem_t> items, const QString &szEmpty );
// A submenu of parent holding the picker.
QMenu *AddPicker( QMenu *pParent, const QString &szTitle, std::vector<PickerItem_t> items, const QString &szEmpty );
