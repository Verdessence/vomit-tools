//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A tree's open folders kept in sight: as the tree scrolls, the folders holding the
//          rows at its top stay pinned over them, one row each (deepest last), until their own
//          rows come back into view. A click on one folds it and scrolls to it, so a long open
//          folder can be closed without scrolling back up to it.
//
//===========================================================================//
#pragma once

#include <vector>

#include <QPersistentModelIndex>
#include <QWidget>

class QTreeView;

class CStickyTreeParents : public QWidget
{
	Q_OBJECT

public:
	// Over view's viewport, following its scrolling, folding and model.
	explicit CStickyTreeParents( QTreeView *pView );

	// The folders pinned now, outermost first.
	[[nodiscard]] std::vector<QModelIndex> GetPinned() const;
	// The height each pinned row takes.
	[[nodiscard]] int GetRowHeight() const { return m_nRowHeight; }

	// At most this many are pinned (the deepest ones beyond it scroll as usual).
	static constexpr int MOST_PINNED = 5;

protected:
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;
	void paintEvent( QPaintEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void leaveEvent( QEvent *pEvent ) override;

private:
	void Update();

	QTreeView *m_pView = nullptr;
	std::vector<QPersistentModelIndex> m_Pinned;
	int m_nRowHeight = 0;
	int m_nHover = -1;
	bool m_bPending = false;
};
