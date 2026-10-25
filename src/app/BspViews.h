//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What a map holds besides its packed files: its entities, each with its keyvalues, and
//          the table of lumps in its header.
//
//===========================================================================//
#pragma once

#include <QWidget>

namespace xv::bsp
{
class CBsp;
}

class QLineEdit;
class QTableWidget;
class QTreeWidget;

// Every entity by class and name, a search over their keyvalues, and the picked one's keyvalues.
class CEntityView : public QWidget
{
	Q_OBJECT

public:
	// bsp must outlive the view.
	explicit CEntityView( const xv::bsp::CBsp &bsp, QWidget *pParent = nullptr );

	// Shows only entities with a key or value holding the text.
	void SetSearch( const QString &szSearch );
	[[nodiscard]] int GetShownCount() const;
	// Picks the entity at its index in the map's entity lump.
	void SelectEntity( int nIndex );

private:
	void ShowKeyValues( int nIndex );

	const xv::bsp::CBsp &m_Bsp;
	QLineEdit *m_pSearch = nullptr;
	QTreeWidget *m_pList = nullptr;
	QTableWidget *m_pKeyValues = nullptr;
};

// The header's 64 lumps: offset, size, version and whether each is compressed.
class CLumpView : public QWidget
{
	Q_OBJECT

public:
	explicit CLumpView( const xv::bsp::CBsp &bsp, QWidget *pParent = nullptr );
};
