//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The HUD editor (a first look): a VGUI layout file (HudLayout.res, resource/ui/*.res)
//          in a tab, its elements listed, drawn where the game puts them on a screen of the
//          size picked, moved by dragging or the arrow keys, and their keys edited in a sheet.
//          Edits go into the KeyValues text losslessly, one undo step each.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <QImage>
#include <QPoint>
#include <QWidget>

#include "EditorTab.h"
#include "hud/Hud.h"
#include "kv1/Kv1.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTreeWidget;
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

private:
	[[nodiscard]] QRectF GetScreenRect() const;
	[[nodiscard]] double GetZoom() const;

	CHudEditor &m_Editor;
	QImage m_Background;
	std::optional<QPointF> m_DragFrom; // screen pixels, where the drag began
	QPoint m_DragOffset;               // screen pixels moved so far
};

class CHudEditor : public QWidget, public IEditorTab
{
	Q_OBJECT
public:
	// szName: the game path or file name shown; disk: the file on disk, empty for a game file.
	CHudEditor( const QString &szName, const std::string &szText, const std::filesystem::path &disk, bool bFromGame, QWidget *pParent = nullptr );

	void SetScheme( xv::hud::CScheme scheme );
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

private:
	void Rebuild();
	void FillList();
	void FillSheet();
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

	QUndoStack *m_pUndo = nullptr;
	CHudCanvas *m_pCanvas = nullptr;
	QTreeWidget *m_pList = nullptr;
	QLineEdit *m_pFilter = nullptr;
	QTableWidget *m_pSheet = nullptr;
	QComboBox *m_pResolution = nullptr;
	QCheckBox *m_pHidden = nullptr;
	QLabel *m_pStatus = nullptr;
};
