//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Object Properties for the selection, read only: the class and its keys
//          (with SmartEdit, as the FGD names them), the flags, the outputs and the inputs, and
//          the help the FGD gives.
//
//===========================================================================//
#pragma once

#include <QWidget>

#include "map/Properties.h"

class CMapDocument;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QToolButton;
class QTreeWidget;

class CMapProperties : public QWidget
{
	Q_OBJECT

public:
	explicit CMapProperties( CMapDocument &document, QWidget *pParent = nullptr );

	// Shows the document's selection as it is now.
	void Refresh();
	[[nodiscard]] bool IsSmartEdit() const;
	void SetSmartEdit( bool bSmartEdit );
	// Puts the keyboard in the keys list (Alt+Enter).
	void FocusKeys();
	[[nodiscard]] const xv::map::PropertySheet_t &GetSheet() const { return m_Sheet; }
	[[nodiscard]] QTreeWidget *GetKeys() const { return m_pKeys; }

private:
	void ShowHelp();
	void Mark( const QTreeWidget *pList );

	CMapDocument &m_Document;
	xv::map::PropertySheet_t m_Sheet;
	QLineEdit *m_pClass = nullptr;
	QToolButton *m_pSmartEdit = nullptr;
	QTabWidget *m_pTabs = nullptr;
	QTreeWidget *m_pKeys = nullptr;
	QLabel *m_pHelp = nullptr;
	QPlainTextEdit *m_pComments = nullptr;
	QTreeWidget *m_pFlags = nullptr;
	QTreeWidget *m_pOutputs = nullptr;
	QTreeWidget *m_pInputs = nullptr;
};
