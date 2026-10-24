//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Editor for one material (.vmt): its text, with line numbers and the lines the parser
//          stumbles on marked, beside the material drawn the way it would look, redrawn shortly
//          after each edit. The text's own undo applies while typing.
//
//===========================================================================//
#pragma once

#include <filesystem>

#include <QWidget>

#include "material/Material.h"

class QLabel;
class QPlainTextEdit;
class QTimer;
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

	// For tests: the preview as it stands, and the lines marked as problems (1-based).
	[[nodiscard]] CMaterialView *GetPreview() const { return m_pPreview; }
	[[nodiscard]] QList<int> GetProblemLines() const { return m_ProblemLines; }
	// Replaces the text as an edit (undoable, marks it changed).
	void SetText( const QString &szText );
	// Draws the preview from the text now rather than after the pause.
	void UpdatePreview();

signals:
	void ModifiedChanged( bool bModified );

private:
	void CheckText();

	QString m_szGamePath;
	xv::material::Reader_t m_pfnRead;
	std::filesystem::path m_DiskPath;
	std::filesystem::path m_SuggestedPath;
	QPlainTextEdit *m_pText = nullptr;
	QLabel *m_pProblems = nullptr;
	QWidget *m_pPreviewHost = nullptr;
	CMaterialView *m_pPreview = nullptr;
	QTimer *m_pPause = nullptr;
	QList<int> m_ProblemLines;
	bool m_bNeverSaved = false;
};
