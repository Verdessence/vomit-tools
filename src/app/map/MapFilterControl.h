//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Filter Control: the map's own visgroups (User) and the automatic ones
//          (Auto), each ticked while its objects are shown. Unticking hides them; Mark selects
//          them. Hiding is the viewer's own state; the map isn't changed. A third tab lists
//          Hammer++'s cordons, each with its boxes, to switch, rename, add and delete; the one
//          picked is what Edit Cordon Bounds edits.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QWidget>

#include "map/Map.h"
#include "map/Visgroups.h"

class CMapDocument;
class QCheckBox;
class QPushButton;
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

	// The Cordons tab: the tree, and the cordon (and its box) picked in it. A new cordon or box
	// gets the box pfnNewBox gives (the selection's, say).
	[[nodiscard]] QTreeWidget *GetCordonTree() const { return m_pCordons; }
	[[nodiscard]] std::size_t GetPickedCordon() const { return m_nPickedCordon; }
	[[nodiscard]] std::size_t GetPickedBox() const { return m_nPickedBox; }
	void PickCordon( std::size_t nCordon, std::size_t nBox );
	void SetNewBoxSource( std::function<std::optional<xv::map::Box_t>()> pfnNewBox ) { m_pfnNewBox = std::move( pfnNewBox ); }
	// New, Add Box and Delete, as the buttons do them.
	void NewCordon();
	void AddCordonBox();
	void DeleteCordon();

signals:
	// The pick is about to change (or the cordons to be edited here): a box being dragged for the
	// picked cordon should go into the map first. Then another cordon or box was picked; a new
	// one wants its box dragged (Edit Cordon Bounds).
	void CordonEditing();
	void CordonPicked();
	void CordonEditWanted();

private:
	void UpdateStates();
	void ItemChanged( QTreeWidgetItem *pItem );
	void RebuildCordons();
	void CordonChanged( QTreeWidgetItem *pItem );
	// The map's cordons as they are to be after an edit made here, as one undo step.
	void EditCordons( std::vector<xv::map::Cordon_t> cordons, bool bActive, const QString &szName );

	CMapDocument &m_Document;
	QTabWidget *m_pTabs = nullptr;
	QTreeWidget *m_pUser = nullptr;
	QTreeWidget *m_pAuto = nullptr;
	QTreeWidget *m_pCordons = nullptr;
	QCheckBox *m_pCordonsOn = nullptr;
	QPushButton *m_pAddBox = nullptr;
	QPushButton *m_pDeleteCordon = nullptr;
	std::size_t m_nPickedCordon = 0;
	std::size_t m_nPickedBox = 0;
	std::function<std::optional<xv::map::Box_t>()> m_pfnNewBox;
	std::vector<const xv::map::AutoVisgroup_t *> m_AutoGroups; // what the Auto tree's items point at
	bool m_bUpdating = false;
};
