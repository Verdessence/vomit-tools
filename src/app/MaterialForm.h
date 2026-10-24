//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A material (VMT) as a list to edit rather than text: its shader, then each parameter
//          with a box that suits its value (a tick for a switch, a number box, a colour picker, a
//          search box for surface properties and named choices, textures with their picture),
//          and its blocks (proxies, fallbacks) folded under their names. Right-click adds a
//          parameter or a proxy, or removes one. Every edit is made to the text, keeping its
//          comments and layout, so the text's undo takes it back.
//
//===========================================================================//
#pragma once

#include <functional>
#include <vector>

#include <QDialog>
#include <QIcon>
#include <QModelIndex>
#include <QSet>
#include <QWidget>

#include "ChoiceBoxes.h"
#include "kv1/Kv1.h"

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Picks a parameter to add: a search box over the ones the shader uses (then the others), each with
// what it does. A $ or % name that isn't listed can be typed in.
class CAddParameterDialog : public QDialog
{
	Q_OBJECT

public:
	// existing: the keys the block already has, left out of the list.
	CAddParameterDialog( const QString &szShader, const QStringList &existing, QWidget *pParent = nullptr );

	// The key picked (or typed).
	[[nodiscard]] QString GetKey() const;
	// For tests.
	[[nodiscard]] QLineEdit *GetSearch() const { return m_pSearch; }
	[[nodiscard]] QTreeWidget *GetList() const { return m_pList; }

protected:
	bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
	void Filter();

	QLineEdit *m_pSearch = nullptr;
	QTreeWidget *m_pList = nullptr;
};

class CMaterialForm : public QWidget
{
	Q_OBJECT

public:
	explicit CMaterialForm( QWidget *pParent = nullptr );

	// Shows a material's text. While the parser stumbles on it, the list can't be edited.
	void SetText( const QString &szText );
	[[nodiscard]] const QString &GetText() const { return m_szText; }
	// The text without the parameters and blocks unticked on the left: what the preview draws.
	[[nodiscard]] QString GetPreviewText() const;

	void SetDetailTypes( std::function<std::vector<Choice_t>()> pfnTypes ) { m_pfnDetailTypes = std::move( pfnTypes ); }
	[[nodiscard]] std::vector<Choice_t> GetDetailTypes() const { return m_pfnDetailTypes ? m_pfnDetailTypes() : std::vector<Choice_t>{}; }
	// The game's surface properties, for $surfaceprop.
	void SetSurfaceProps( std::vector<Choice_t> surfaceProps ) { m_SurfaceProps = std::move( surfaceProps ); }
	// The textures under materials/ to offer (without materials/ and .vtf), asked for the first
	// time a texture is edited.
	void SetTextureNames( std::function<QStringList()> pfnNames ) { m_pfnTextureNames = std::move( pfnNames ); }
	// A picture for the texture a parameter names, and what it is, for its row.
	void SetTextureIcons( std::function<QIcon( const QString &szKey, QString *pDescription )> pfnIcon );

	// Edits, for the menu and tests. A parameter goes in the material's own block unless
	// blockPath names another (indices from the file's first block down). Each is one change to
	// the text.
	bool SetParameter( const QString &szKey, const QString &szValue );
	bool RemoveParameter( const QString &szKey );
	bool SetShader( const QString &szShader );
	bool AddParameter( const QString &szKey, const std::vector<int> &blockPath = {} );
	bool AddProxy( const QString &szProxy );
	// Asks which parameter to add to the block picked in the list (or the material's own).
	void ChooseParameterToAdd();
	// Picks the row written on a line of the text (1-based); false when none is.
	bool SelectLine( int nLine );

	// For tests.
	[[nodiscard]] QTreeWidget *GetTree() const { return m_pTree; }
	[[nodiscard]] QTreeWidgetItem *FindItem( const QString &szKey ) const;

	// Called back by the value editors: the key (column 0) or value (column 1) of a row.
	void CommitEdit( const QModelIndex &index, const QString &szText );
	[[nodiscard]] const std::vector<Choice_t> &GetSurfaceProps() const { return m_SurfaceProps; }
	[[nodiscard]] QStringList GetTextureNames();

signals:
	void OpenDetailTypeRequested( const QString &szName );
	// The whole text after an edit made here.
	void TextEdited( const QString &szText );
	// Undo or redo was pressed in the list: the text's own undo takes it.
	void UndoRequested();
	void RedoRequested();
	// A tick on the left changed what the preview draws.
	void PreviewTogglesChanged();

private:
	void Fill();
	void AddItems( QTreeWidgetItem *pParent, const xv::kv1::CNode &node, std::vector<int> path );
	void ShowMenu( const QPoint &at );
	// Writes the edited document as the text and says so; refills the list after.
	void Apply( const xv::kv1::CDocument &document, const std::vector<int> &select = {} );
	[[nodiscard]] int RootIndex() const;
	[[nodiscard]] std::vector<int> PathOf( const QTreeWidgetItem *pItem ) const;
	[[nodiscard]] QTreeWidgetItem *ItemAt( const std::vector<int> &path ) const;
	[[nodiscard]] QString Shader() const;
	void PickColour( QTreeWidgetItem *pItem );

	QString m_szText;
	xv::kv1::CParseResult m_Parsed;
	QTreeWidget *m_pTree = nullptr;
	QLabel *m_pBlocked = nullptr;
	std::function<std::vector<Choice_t>()> m_pfnDetailTypes;
	std::vector<Choice_t> m_SurfaceProps;
	std::function<QStringList()> m_pfnTextureNames;
	QStringList m_TextureNames;
	bool m_bHaveTextureNames = false;
	std::function<QIcon( const QString &, QString * )> m_pfnIcon;
	QSet<QString> m_Collapsed; // blocks folded, by path
	QSet<QString> m_Hidden;    // unticked, left out of the preview, by key chain
	bool m_bFilling = false;
};
