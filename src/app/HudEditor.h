//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The HUD editor (a first look): a VGUI layout file (HudLayout.res, resource/ui/*.res)
//          in a tab, its elements listed, drawn where the game puts them on a screen of the
//          size picked, moved by dragging or the arrow keys, and their keys edited in a sheet.
//          Edits go into the KeyValues text losslessly, one undo step each.
//          The elements are drawn as plain boxes with their names, or (Render elements) as
//          the game draws them, as far as the files say: backgrounds, labels in the scheme's
//          fonts, images, and samples of what the well-known panels show. Elements can be
//          kept from being picked on the screen, and the scheme (ClientScheme.res) is shown
//          in a panel of its own: colours, settings, fonts and borders.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QFont>
#include <QImage>
#include <QPoint>
#include <QWidget>

#include "EditorTab.h"
#include "hud/Hud.h"
#include "kv1/Kv1.h"

class QCheckBox;
class QPainter;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;
class CHudEditor;

// The screen: the elements drawn at their places, scaled to fit the widget.
class CHudCanvas : public QWidget
{
	Q_OBJECT
public:
	explicit CHudCanvas( CHudEditor &editor, QWidget *pParent = nullptr );
	// Widget pixels to screen pixels and back.
	[[nodiscard]] QPointF ToScreen( QPointF at ) const;
	[[nodiscard]] QRectF ToWidget( const xv::hud::Rect_t &rect ) const;
	void SetBackground( const QImage &image );
	[[nodiscard]] bool HasBackground() const { return !m_Background.isNull(); }

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void mouseReleaseEvent( QMouseEvent *pEvent ) override;
	void keyPressEvent( QKeyEvent *pEvent ) override;
	void leaveEvent( QEvent *pEvent ) override;
	void contextMenuEvent( QContextMenuEvent *pEvent ) override;

private:
	[[nodiscard]] QRectF GetScreenRect() const;
	[[nodiscard]] double GetZoom() const;
	// One element as the game draws it; false when nothing of it shows.
	bool PaintRendered( QPainter &painter, const xv::hud::Element_t &element, const QRectF &box ) const;

	CHudEditor &m_Editor;
	QImage m_Background;
	std::optional<QPointF> m_DragFrom; // screen pixels, where the drag began
	QPoint m_DragOffset;               // screen pixels moved so far
	int m_nHover = -1;                 // the element under the pointer
	std::vector<int> m_GuidesX;        // while dragging: the lines the element has caught on, in screen pixels
	std::vector<int> m_GuidesY;
};

class CHudEditor : public QWidget, public IEditorTab
{
	Q_OBJECT
public:
	// szName: the game path or file name shown; disk: the file on disk, empty for a game file.
	CHudEditor( const QString &szName, const std::string &szText, const std::filesystem::path &disk, bool bFromGame, QWidget *pParent = nullptr );

	// szSource: the file the scheme was read from (a path on disk, or the game's), which
	// Edit Scheme asks to have opened; empty when it has none to open.
	void SetScheme( xv::hud::CScheme scheme, const QString &szSource = {} );
	[[nodiscard]] const xv::hud::CScheme &GetScheme() const { return m_Scheme; }
	[[nodiscard]] const QString &GetSchemeSource() const { return m_szSchemeSource; }
	// The game's files, for what drawing the elements as the game does needs beside the
	// scheme: its font files, the HUD's icons, the language file and the images.
	using ReadFile_t = std::function<std::optional<std::vector<std::byte>>( const std::string & )>;
	void SetFiles( ReadFile_t pfnRead );
	// The file is the main menu's (resource/GameMenu.res): its items are drawn as that menu,
	// over the game's menu background; with a map loaded (the items marked OnlyInGame too)
	// or without.
	[[nodiscard]] bool IsGameMenu() const { return m_bGameMenu; }
	[[nodiscard]] bool IsInGame() const;
	void SetInGame( bool bInGame );
	// What is behind the elements when no picture was picked: the menu's background for a
	// game menu, else nothing (a plain backdrop is drawn).
	[[nodiscard]] QImage GetBackdrop() const;
	// Whether the layout is Counter-Strike's (it has that game's armour panel): Half-Life 2's
	// panels left in it are then never shown by the game.
	[[nodiscard]] bool IsCounterStrike() const { return m_bCounterStrike; }
	// Drawn as the game draws them (else as boxes with their names).
	[[nodiscard]] bool IsRendering() const;
	void SetRendering( bool bRender );
	// Whether a click on the screen can pick the element (it can always be picked in the
	// list). Kept by the file's name and the element's, between sessions.
	[[nodiscard]] bool IsPickable( int nElement ) const;
	void SetPickable( int nElement, bool bPickable );
	// Dragging an element catches on what is around it: the screen's edges and middle, its
	// panel's, and the other elements' edges and middles (the box on the bar; Alt while
	// dragging goes without).
	[[nodiscard]] bool IsSnapping() const;
	void SetSnapping( bool bSnap );
	// A drag's offset made to catch: an element moved by offset has each of its edges and
	// its middle tried against those lines, and the nearest within nReach screen pixels on
	// each axis pulls it there. pGuidesX / pGuidesY get the lines it then lies on (screen
	// pixels across and down), to be drawn.
	[[nodiscard]] QPoint SnapOffset( int nElement, QPoint offset, int nReach, std::vector<int> *pGuidesX = nullptr, std::vector<int> *pGuidesY = nullptr ) const;
	// Whether the element is drawn on the screen at all (the eye in the list): one left out
	// is neither seen nor picked there. Kept like the pick choice; the file isn't touched.
	[[nodiscard]] bool IsShown( int nElement ) const;
	void SetShown( int nElement, bool bShown );
	// What the game's hudanimations.txt leaves a panel's property at ("FgColor"), as a colour.
	[[nodiscard]] std::optional<xv::hud::Color_t> AnimatedColor( std::string_view szElement, std::string_view szProperty ) const;
	// Values to try in place of a text's variables ("%mapname%", "%s1"), for this sitting
	// only; an empty value takes one back out.
	void SetVariable( const QString &szName, const QString &szValue );
	[[nodiscard]] QString GetVariable( const QString &szName ) const;
	// A label's text as drawn: its token in the game's language, the variables put in.
	[[nodiscard]] QString TextFor( const std::string &szText ) const;
	// The scheme's names an element uses, lower-case: what its keys name (a setting that
	// names a colour brings the colour too) and what it is drawn with without saying so (the
	// scheme's FgColor and BgColor, the fonts of its sample, its colours in hudanimations).
	[[nodiscard]] std::set<std::string> SchemeNamesUsedBy( int nElement ) const;
	// A scheme font at a zoom (widget pixels a screen pixel); the editor's own when the
	// scheme hasn't it.
	[[nodiscard]] QFont FontFor( std::string_view szName, double flZoom, bool *pAdditive = nullptr ) const;
	// An image of materials/vgui, through its material; null when it can't be read.
	[[nodiscard]] QImage ImageFor( const std::string &szImage ) const;
	[[nodiscard]] const xv::hud::Icon_t *IconFor( std::string_view szName ) const;
	// "#Token" in the game's language; anything else as it is.
	[[nodiscard]] QString Localize( const std::string &szText ) const;
	void SetScreen( const xv::hud::Screen_t &screen );
	[[nodiscard]] const xv::hud::Screen_t &GetScreen() const { return m_Screen; }
	[[nodiscard]] const std::vector<xv::hud::Element_t> &GetElements() const { return m_Elements; }
	[[nodiscard]] const xv::kv1::CDocument &GetDocument() const { return m_Document; }
	[[nodiscard]] std::string GetText() const;
	[[nodiscard]] CHudCanvas *GetCanvas() const { return m_pCanvas; }
	[[nodiscard]] bool IsShowingHidden() const;

	// Which element (an index into GetElements()) is selected; -1 for none.
	[[nodiscard]] int GetSelected() const { return m_nSelected; }
	void Select( int nElement );
	// The topmost element under a screen pixel, or -1.
	[[nodiscard]] int ElementAt( QPointF at ) const;
	// Sets key on the element (adding it when missing), as one undo step.
	void SetValue( int nElement, const QString &szKey, const QString &szValue );
	// Puts the element's top left corner at screen pixel at, keeping its anchors.
	void MoveElement( int nElement, QPoint at );
	[[nodiscard]] bool IsModified() const;

	[[nodiscard]] QString GetTabTitle() const override;
	[[nodiscard]] QString GetSessionEntry() const override;
	[[nodiscard]] bool IsFromGame() const override { return m_bFromGame; }
	bool ConfirmClose() override;
	bool Save() override;
	bool SaveAs() override;
	[[nodiscard]] QUndoStack *GetUndoStack() const override { return m_pUndo; }

	// The text after an undo or redo step.
	void ApplyText( const std::string &szText );
	// A HUD layout is a .res whose root holds positioned blocks.
	[[nodiscard]] static bool IsHudLayout( const std::filesystem::path &file, std::string_view szText );

signals:
	void ModifiedChanged( bool bModified );
	void Changed();
	// Edit Scheme: the scheme's file is to be opened in the scheme editor.
	void EditSchemeRequested( const QString &szSource );

private:
	void Rebuild();
	void FillList();
	void FillSheet();
	void FillScheme();
	void FillVariables();
	[[nodiscard]] QString GetPickKey() const;
	void StorePickable();
	void ApplySchemeName( QTreeWidgetItem *pItem );
	void Edit( const std::string &szText, const QString &szWhat );
	bool WriteTo( const std::filesystem::path &file );

	QString m_szName;
	std::filesystem::path m_Disk;
	bool m_bFromGame = false;
	xv::kv1::CDocument m_Document;
	xv::hud::CScheme m_Scheme;
	xv::hud::Screen_t m_Screen{ 1920, 1080, true };
	std::vector<xv::hud::Element_t> m_Elements;
	int m_nSelected = -1;
	bool m_bFilling = false;
	bool m_bGameMenu = false;
	bool m_bCounterStrike = false;
	QString m_szSchemeSource;
	ReadFile_t m_pfnRead;
	std::map<std::string, xv::hud::Icon_t> m_Icons;
	std::set<std::string> m_Unpickable; // lower-case element names
	std::set<std::string> m_Unshown;    // and those left out of the screen
	xv::hud::Animations_t m_Animations;
	std::map<std::string, std::string> m_Variables; // lower-case name -> the value tried
	// Read when first needed.
	mutable std::optional<std::map<std::string, std::string>> m_Tokens;
	mutable std::map<std::string, QImage> m_Images;

	QUndoStack *m_pUndo = nullptr;
	CHudCanvas *m_pCanvas = nullptr;
	QTreeWidget *m_pList = nullptr;
	QLineEdit *m_pFilter = nullptr;
	QTableWidget *m_pSheet = nullptr;
	QComboBox *m_pResolution = nullptr;
	QCheckBox *m_pHidden = nullptr;
	QCheckBox *m_pRender = nullptr;
	QCheckBox *m_pInGame = nullptr;
	QCheckBox *m_pSnap = nullptr;
	QLineEdit *m_pSchemeFilter = nullptr;
	QPushButton *m_pEditScheme = nullptr;
	QCheckBox *m_pUsedOnly = nullptr;
	QTableWidget *m_pVariables = nullptr;
	QTabWidget *m_pSchemeTabs = nullptr;
	QTreeWidget *m_pSchemeColors = nullptr;
	QTreeWidget *m_pSchemeSettings = nullptr;
	QTreeWidget *m_pSchemeFonts = nullptr;
	QTreeWidget *m_pSchemeBorders = nullptr;
	QLabel *m_pStatus = nullptr;
};
