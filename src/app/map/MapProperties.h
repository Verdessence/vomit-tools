//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Object Properties, for the selected entities: Class Info (the keys, with
//          SmartEdit's names and typed editors, or the raw keys to add and change), Flags,
//          Outputs (to add, change and delete) and Inputs (what fires at the selection), with
//          Mark to go to the other end. Every change is one step to undo.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <QStyledItemDelegate>
#include <QWidget>

#include "map/Edit.h"
#include "map/MapDocument.h"
#include "map/Properties.h"

class CMapProperties;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Edits a key's value by its type (a choices key as a combo of its names), and an output's
// columns; what is typed goes to the panel, not to the item.
class CPropertyDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	CPropertyDelegate( CMapProperties &panel, QTreeWidget &list );

	QWidget *createEditor( QWidget *pParent, const QStyleOptionViewItem &option, const QModelIndex &index ) const override;
	void setEditorData( QWidget *pEditor, const QModelIndex &index ) const override;
	void setModelData( QWidget *pEditor, QAbstractItemModel *pModel, const QModelIndex &index ) const override;

private:
	CMapProperties &m_Panel;
	QTreeWidget &m_List;
};

class CMapProperties : public QWidget
{
	Q_OBJECT

public:
	explicit CMapProperties( CMapDocument &document, QWidget *pParent = nullptr );

	// Shows the document's selection as it is now.
	void Refresh();
	// Shows the world instead (Hammer's Map Properties): its keys and outputs, no inputs.
	void SetWorld( bool bWorld );
	[[nodiscard]] bool IsWorld() const { return m_bWorld; }
	[[nodiscard]] bool IsSmartEdit() const;
	void SetSmartEdit( bool bSmartEdit );
	// Puts the keyboard in the keys list (Alt+Enter).
	void FocusKeys();
	void SetFacePickKey( const QString &szKey );
	void BrowseInstanceFile();
	[[nodiscard]] const xv::map::PropertySheet_t &GetSheet() const { return m_Sheet; }
	[[nodiscard]] QTreeWidget *GetKeys() const { return m_pKeys; }
	[[nodiscard]] QTreeWidget *GetOutputs() const { return m_pOutputs; }

	// The edits the lists make (and tests): a key's value on every selected entity, a key
	// renamed (SmartEdit off), the selected entities' class, an output's column, the comments.
	void CommitKey( std::size_t nRow, const std::string &szValue );
	void RenameKey( std::size_t nRow, const std::string &szKey );
	void CommitClass( const std::string &szClass );
	void CommitOutput( std::size_t nRow, int nColumn, const std::string &szValue );
	void CommitComments();
	void AddKey();
	void RemoveKey();
	void AddOutput();
	void RemoveOutput();
	// What an editor for a key or an output's column may choose from (value and name), if it
	// is a choice; the first is empty for a free field.
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> GetKeyChoices( std::size_t nRow ) const;
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> GetOutputChoices( std::size_t nRow, int nColumn ) const;

signals:
	void FacePickRequested( const QString &szKey );

protected:
	bool eventFilter( QObject *pWatched, QEvent *pEvent ) override;

private:
	void ShowHelp();
	void Mark( const QTreeWidget *pList );
	void FlagChanged( QTreeWidgetItem *pItem );
	// An edit of the selected entities, made after the list that asked for it is done with
	// its editor; szName is its name in Edit > Undo.
	void Apply( const QString &szName, std::function<xv::map::MapEdit_t( const xv::map::CScene & )> pfnEdit );
	// The outputs of one selected entity as the list has them now.
	[[nodiscard]] std::vector<xv::map::Connection_t> OutputsOf( std::uint32_t nObject ) const;
	[[nodiscard]] xv::map::MapEdit_t EditShown( const xv::map::CScene &scene, const std::vector<std::uint32_t> &selection, const std::function<void( xv::map::Entity_t & )> &pfnEdit ) const;
	[[nodiscard]] bool TargetExists( const std::string &szTarget ) const;

	CMapDocument &m_Document;
	xv::map::PropertySheet_t m_Sheet;
	bool m_bFilling = false; // Refresh is putting rows in: their signals aren't edits
	bool m_bWorld = false;
	QLineEdit *m_pClass = nullptr;
	QToolButton *m_pSmartEdit = nullptr;
	QTabWidget *m_pTabs = nullptr;
	QTreeWidget *m_pKeys = nullptr;
	QPushButton *m_pBrowseInstance = nullptr;
	QPushButton *m_pPickFaces = nullptr;
	QString m_szFacePickKey;
	QPushButton *m_pAddKey = nullptr;
	QPushButton *m_pRemoveKey = nullptr;
	QLabel *m_pHelp = nullptr;
	QPlainTextEdit *m_pComments = nullptr;
	QTreeWidget *m_pFlags = nullptr;
	QTreeWidget *m_pOutputs = nullptr;
	QTreeWidget *m_pInputs = nullptr;
};
