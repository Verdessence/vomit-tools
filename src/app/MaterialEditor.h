//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Editor for one material (.vmt), as a list of parameters (CMaterialForm) or as its text,
//          coloured, with line numbers and the lines the parser stumbles on marked; Preferences
//          picks which, and the switch above it too. Beside it the material is drawn the way it
//          would look, and below it Messages lists what is wrong or worth knowing (mistakes,
//          misspelt parameters, missing textures). Every edit is one to the text, so the text's
//          undo applies to both. In a texture's editor the preview and Messages are placed by it.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <QWidget>

#include "ChoiceBoxes.h"
#include "material/Material.h"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class CMaterialForm;
class CMaterialView;

class CMaterialEditor : public QWidget
{
	Q_OBJECT

public:
	// pfnRead finds the textures the material names; it may be empty.
	CMaterialEditor( QString szGamePath, const QString &szText, xv::material::Reader_t pfnRead, QWidget *pParent = nullptr );

	[[nodiscard]] QString GetText() const;
	[[nodiscard]] const QString &GetGamePath() const { return m_szGamePath; }
	[[nodiscard]] QString GetFileName() const;
	[[nodiscard]] bool IsModified() const;
	// The loose file Save writes to; empty when the file came from a VPK or is new.
	[[nodiscard]] const std::filesystem::path &GetDiskPath() const { return m_DiskPath; }
	void SetDiskPath( std::filesystem::path path ) { m_DiskPath = std::move( path ); }
	void MarkSaved();
	void MarkUnsaved(); // a new material that has never been written
	[[nodiscard]] bool IsNew() const { return m_bNeverSaved; }
	// Where Save As starts for a new material (beside its texture).
	[[nodiscard]] const std::filesystem::path &GetSuggestedPath() const { return m_SuggestedPath; }
	void SetSuggestedPath( std::filesystem::path path ) { m_SuggestedPath = std::move( path ); }
	void SetReader( xv::material::Reader_t pfnRead );
	// The game's surface properties, for $surfaceprop and its check.
	void SetSurfaceProps( std::vector<Choice_t> surfaceProps );
	// The textures under materials/ to offer while typing one, asked for when first needed.
	void SetTextureNames( std::function<QStringList()> pfnNames );

	// The parts a host can place in its own layout: the preview (with its controls) and Messages.
	[[nodiscard]] QWidget *GetPreviewHost() const { return m_pPreviewHost; }
	[[nodiscard]] QWidget *GetMessages() const { return m_pMessagesPanel; }

	// For tests: the preview as it stands, the lines marked as problems (1-based), the list,
	// Messages, and the text box.
	[[nodiscard]] CMaterialView *GetPreview() const { return m_pPreview; }
	[[nodiscard]] QList<int> GetProblemLines() const { return m_ProblemLines; }
	[[nodiscard]] CMaterialForm *GetForm() const { return m_pForm; }
	[[nodiscard]] QTreeWidget *GetMessageList() const { return m_pMessages; }
	[[nodiscard]] QPlainTextEdit *GetTextBox() const { return m_pText; }
	[[nodiscard]] bool IsVisual() const;
	// Replaces the text as an edit (undoable, marks it changed).
	void SetText( const QString &szText );
	// Draws the preview from the text now rather than after the pause, and checks it again.
	void UpdatePreview();
	// Shows a line: picked in the text, or its row in the list.
	void GoToLine( int nLine );

signals:
	void OpenDetailTypeRequested( const QString &szName );
	void ModifiedChanged( bool bModified );
	// The preview was drawn again (after an edit, a pause or Refresh).
	void PreviewUpdated();

private:
	void CheckText();
	void CheckMaterial();
	void ShowMode();

	QString m_szGamePath;
	xv::material::Reader_t m_pfnRead;
	std::vector<Choice_t> m_SurfaceProps;
	std::filesystem::path m_DiskPath;
	std::filesystem::path m_SuggestedPath;
	QPlainTextEdit *m_pText = nullptr;
	CMaterialForm *m_pForm = nullptr;
	QStackedWidget *m_pStack = nullptr;
	QToolButton *m_pVisualButton = nullptr;
	QToolButton *m_pTextButton = nullptr;
	QCheckBox *m_pLive = nullptr;
	QPushButton *m_pRefresh = nullptr;
	QPushButton *m_pAdd = nullptr;
	QWidget *m_pMessagesPanel = nullptr;
	QLabel *m_pMessageCount = nullptr;
	QTreeWidget *m_pMessages = nullptr;
	QWidget *m_pPreviewHost = nullptr;
	CMaterialView *m_pPreview = nullptr;
	QTimer *m_pPause = nullptr;
	QList<int> m_ProblemLines;
	bool m_bNeverSaved = false;
	bool m_bFormEdit = false; // the text is changing because of an edit in the list
};
