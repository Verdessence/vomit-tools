//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A column of panels as Blender's properties editor lays them out: each panel has a
//          header that folds it and drags it to another place in the column, and a strip of
//          tabs down the side shows or hides each one (several at once, unlike Blender's
//          tabs). A panel can belong to a mode, as Blender's tool settings do: while its mode is
//          off, neither it nor its tab is there (SetAvailable), and its tab sits at the strip's
//          foot, under a line, apart from the tabs that are always there. The column scrolls when its panels are taller than it; a panel that stretches
//          takes the height left over when they aren't. Which panels show, which are folded and
//          their order are kept as one value (GetState, SetState).
//
//===========================================================================//
#pragma once

#include <vector>

#include <QIcon>
#include <QVariantMap>
#include <QWidget>

class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class CPanelColumn;

class CPanel : public QWidget
{
	Q_OBJECT

public:
	CPanel( CPanelColumn &column, const QString &szId, const QString &szTitle, QWidget *pContent, bool bStretch );

	[[nodiscard]] const QString &GetId() const { return m_szId; }
	[[nodiscard]] QWidget *GetContent() const { return m_pContent; }
	[[nodiscard]] bool IsFolded() const { return m_bFolded; }
	[[nodiscard]] bool IsStretching() const { return m_bStretch && !m_bFolded; }
	void SetFolded( bool bFolded );
	void SetTitle( const QString &szTitle );

signals:
	void FoldChanged();

protected:
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
	void UpdateArrow();

	CPanelColumn &m_Column;
	QString m_szId;
	QWidget *m_pHeader = nullptr;
	QLabel *m_pArrow = nullptr;
	QLabel *m_pTitle = nullptr;
	QWidget *m_pContent = nullptr;
	bool m_bStretch = false;
	bool m_bFolded = false;
	QPoint m_PressAt;
	bool m_bPressed = false;
};

class CPanelColumn : public QWidget
{
	Q_OBJECT

public:
	explicit CPanelColumn( QWidget *pParent = nullptr );

	// A panel at the column's end, and its tab. A stretching panel takes the column's spare
	// height; the others keep the height their content asks for.
	CPanel *AddPanel( const QString &szId, const QString &szTitle, const QIcon &icon, QWidget *pContent, bool bStretch = false );
	// Hidden until asked for (a command reveals it), also for a saved state from before it existed.
	void SetHiddenByDefault( const QString &szId );
	[[nodiscard]] CPanel *GetPanel( const QString &szId ) const;
	[[nodiscard]] QToolButton *GetTab( const QString &szId ) const;
	[[nodiscard]] std::vector<CPanel *> GetPanels() const; // in the column's order
	// Ticked in its tab (and so in the column, while it is available).
	[[nodiscard]] bool IsShown( const QString &szId ) const;
	void SetShown( const QString &szId, bool bShown );
	// A mode's panel: away, tab and all, while it isn't available, and back as it was left.
	[[nodiscard]] bool IsAvailable( const QString &szId ) const;
	void SetAvailable( const QString &szId, bool bAvailable );
	// Scrolled into view, if it shows.
	void ScrollTo( const QString &szId );
	// Shown, unfolded and scrolled to.
	void Reveal( const QString &szId );
	// Moves a panel to nIndex in the column's order.
	void MovePanel( const QString &szId, int nIndex );
	// Folds every panel but this one, or unfolds them all when the others are folded already
	// (Blender's Ctrl+click on a header).
	void FoldOthers( CPanel *pKeep );

	// "order" (ids), "hidden" and "folded" (ids): what SetState takes back. Ids it doesn't
	// know are passed over, and panels it doesn't name keep their place at the end.
	[[nodiscard]] QVariantMap GetState() const;
	void SetState( const QVariantMap &state );

	// For the panels' headers: a drag of pPanel has begun.
	void StartDrag( CPanel *pPanel );

	// As wide as the widest panel needs, beside the tabs and the scroll bar: the scroll area
	// alone would let a splitter squeeze the panels and cut them off.
	[[nodiscard]] QSize minimumSizeHint() const override;

signals:
	void StateChanged();

protected:
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
	struct Entry_t
	{
		CPanel *m_pPanel = nullptr;
		QToolButton *m_pTab = nullptr;
		bool m_bAvailable = true;
		bool m_bMode = false; // comes and goes with a mode (SetAvailable has been called)
		bool m_bHiddenByDefault = false;
	};

	// The layouts' order and stretch from m_Entries.
	void Relayout();
	// Where a drop at y (in the column's content) would put a panel: an index in m_Entries.
	[[nodiscard]] int DropIndexAt( int y ) const;

	std::vector<Entry_t> m_Entries; // in the column's order
	QWidget *m_pStrip = nullptr;
	QVBoxLayout *m_pTabs = nullptr;
	QWidget *m_pModeLine = nullptr; // between the tabs always there and the modes' tabs
	QScrollArea *m_pScroll = nullptr;
	QWidget *m_pContent = nullptr;
	QVBoxLayout *m_pPanels = nullptr;
	QWidget *m_pDropLine = nullptr; // where a dragged panel would land
};
