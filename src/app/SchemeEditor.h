//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The scheme editor: a VGUI scheme file (resource/ClientScheme.res, SourceScheme.res)
//          in a tab. Its blocks each have a list: the colours and the settings (with a swatch
//          where the value is a colour, and a colour picker), the fonts (each in the face it
//          gets on the screen height picked, its entries and their keys under it), the
//          borders, the font files, and the whole file as a tree. Names and values are edited
//          in place, keys and blocks added, copied and removed. Edits go into the KeyValues
//          text losslessly, one undo step each.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <QWidget>

#include "EditorTab.h"
#include "hud/Hud.h"
#include "kv1/Kv1.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;

class CSchemeEditor : public QWidget, public IEditorTab
{
	Q_OBJECT
public:
	// Where a node is: the index of its root, then of each child on the way down.
	using Path_t = std::vector<std::size_t>;

	// szName: the game path or file name shown; disk: the file on disk, empty for a game file.
	CSchemeEditor( const QString &szName, const std::string &szText, const std::filesystem::path &disk, bool bFromGame, QWidget *pParent = nullptr );

	[[nodiscard]] const xv::kv1::CDocument &GetDocument() const { return m_Document; }
	[[nodiscard]] std::string GetText() const;
	[[nodiscard]] xv::hud::CScheme GetScheme() const { return xv::hud::CScheme::FromDocument( m_Document ); }
	// What it was opened as: the path on disk, or the game's.
	[[nodiscard]] const QString &GetName() const { return m_szName; }
	[[nodiscard]] bool IsModified() const;

	// The node at a path; null when there is none.
	[[nodiscard]] const xv::kv1::CNode *NodeAt( const Path_t &path ) const;
	// The path of a block of the Scheme block ("Colors", "Fonts"...), the first that applies;
	// empty when the file has none.
	[[nodiscard]] Path_t FindSection( std::string_view szSection ) const;
	// Each one undo step; false when nothing changed (no such node, or text the file can't
	// hold).
	bool SetNodeValue( const Path_t &path, const QString &szValue );
	bool SetNodeKey( const Path_t &path, const QString &szKey );
	bool RemoveNode( const Path_t &path );
	// A copy of the node after it, under a name no sibling has.
	bool DuplicateNode( const Path_t &path );
	// A key (or an empty block) at the end of the block at parent.
	bool AddPair( const Path_t &parent, const QString &szKey, const QString &szValue );
	bool AddBlock( const Path_t &parent, const QString &szKey );
	// A font with one entry, ready to be filled in.
	bool AddFont( const QString &szName );

	// The screen height the fonts are shown for.
	void SetScreenTall( int nTall );
	[[nodiscard]] int GetScreenTall() const { return m_nScreenTall; }

	[[nodiscard]] QString GetTabTitle() const override;
	[[nodiscard]] QString GetSessionEntry() const override;
	[[nodiscard]] bool IsFromGame() const override { return m_bFromGame; }
	bool ConfirmClose() override;
	bool Save() override;
	bool SaveAs() override;
	[[nodiscard]] QUndoStack *GetUndoStack() const override { return m_pUndo; }

	// The text after an undo or redo step.
	void ApplyText( const std::string &szText );
	// A .res whose first block is "Scheme".
	[[nodiscard]] static bool IsSchemeFile( const std::filesystem::path &file, std::string_view szText );

signals:
	void ModifiedChanged( bool bModified );
	// The scheme is another: whatever draws with it follows.
	void Changed();

private:
	// One of the lists: the blocks of the Scheme block it shows (empty: the whole file).
	struct Section_t
	{
		std::string m_szBlock;
		QTreeWidget *m_pTree = nullptr;
	};
	[[nodiscard]] xv::kv1::CNode *NodeAt( xv::kv1::CDocument &document, const Path_t &path ) const;
	[[nodiscard]] static Path_t PathOf( const QTreeWidgetItem *pItem );
	[[nodiscard]] QTreeWidget *GetCurrentTree() const;
	[[nodiscard]] const Section_t *GetCurrentSection() const;
	// The block a new key goes into, by what is selected: the selected block, else the
	// selected key's, else the section's.
	[[nodiscard]] Path_t GetAddTarget() const;
	void Fill();
	void FillTree( const Section_t &section );
	void AddItems( const Section_t &section, QTreeWidgetItem *pParent, const xv::kv1::CNode &block, const Path_t &path, int nDepth );
	void ItemChanged( QTreeWidgetItem *pItem, int nColumn );
	void UpdateButtons();
	void PickColor();
	void ShowMenu( QTreeWidget *pTree, const QPoint &at );
	void Edit( const xv::kv1::CDocument &document, const QString &szWhat );
	bool WriteTo( const std::filesystem::path &file );

	QString m_szName;
	std::filesystem::path m_Disk;
	bool m_bFromGame = false;
	xv::kv1::CDocument m_Document;
	xv::hud::CScheme m_Scheme;
	int m_nScreenTall = 1080;
	bool m_bFilling = false;
	std::vector<Section_t> m_Sections;

	QUndoStack *m_pUndo = nullptr;
	QTabWidget *m_pTabs = nullptr;
	QLineEdit *m_pFilter = nullptr;
	QComboBox *m_pScreen = nullptr;
	QPushButton *m_pAdd = nullptr;
	QPushButton *m_pAddBlock = nullptr;
	QPushButton *m_pDuplicate = nullptr;
	QPushButton *m_pRemove = nullptr;
	QPushButton *m_pColor = nullptr;
};
