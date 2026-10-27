//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Filter Control: the map's own visgroups (User) and the automatic ones
//          (Auto), each ticked while its objects are shown. Unticking hides them; Mark selects
//          them. Hiding is the viewer's own state; the map isn't changed.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <vector>

#include <QWidget>

#include "map/Visgroups.h"

class CMapDocument;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

class CMapFilterControl : public QWidget
{
	Q_OBJECT

public:
	explicit CMapFilterControl( CMapDocument &document, QWidget *pParent = nullptr );

	// Fills both trees from the document's map, once it has loaded.
	void Rebuild();
	[[nodiscard]] QTreeWidget *GetUserTree() const { return m_pUser; }
	[[nodiscard]] QTreeWidget *GetAutoTree() const { return m_pAuto; }
	// The objects an item stands for (a visgroup's include those of the visgroups inside it).
	[[nodiscard]] std::vector<std::uint32_t> MembersOf( const QTreeWidgetItem *pItem ) const;
	// Selects the current item's objects.
	void Mark();

private:
	void UpdateStates();
	void ItemChanged( QTreeWidgetItem *pItem );

	CMapDocument &m_Document;
	QTabWidget *m_pTabs = nullptr;
	QTreeWidget *m_pUser = nullptr;
	QTreeWidget *m_pAuto = nullptr;
	std::vector<const xv::map::AutoVisgroup_t *> m_AutoGroups; // what the Auto tree's items point at
	bool m_bUpdating = false;
};
