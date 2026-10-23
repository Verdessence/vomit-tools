//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The skybox creator: six face images, laid out as they join, a view from inside, and
//          checks for what would go wrong in game (missing or mismatched faces, faces turned the
//          wrong way, sizes the game can't use).
//
//===========================================================================//
#pragma once

#include <array>
#include <optional>
#include <vector>

#include <QWidget>

#include "sky/Sky.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class CSkyDocument;
class CSkyView;

class CSkyEditor : public QWidget
{
	Q_OBJECT

public:
	struct Issue_t
	{
		enum class Severity_t
		{
			SEVERITY_INFO,
			SEVERITY_WARNING,
			SEVERITY_ERROR
		};
		Severity_t m_nSeverity = Severity_t::SEVERITY_INFO;
		QString m_szMessage;
		std::optional<xv::sky::OrientationFix_t> m_Fix; // applied by double-clicking the issue
	};

	// Takes ownership of document.
	explicit CSkyEditor( CSkyDocument *pDocument, QWidget *pParent = nullptr );

	[[nodiscard]] CSkyDocument *GetDocument() const { return m_pDocument; }
	[[nodiscard]] CSkyView *GetView() const { return m_pView; }

	// Puts each file on the face its name ends with (…rt.png, …UP.tga), as one undo step. A single
	// file dropped into an empty sky brings the other faces beside it along. Returns the files that
	// couldn't be placed or read, with the reason.
	QStringList LoadFiles( const QStringList &paths );
	bool LoadFace( xv::sky::Face_t nFace, const QString &szPath );
	// Fills every face with a labelled pattern for checking orientation in game.
	void UseTestPattern( int nSize = 512 );
	void ApplyFix( const xv::sky::OrientationFix_t &fix );

	[[nodiscard]] const std::vector<Issue_t> &GetIssues() const { return m_Issues; }
	// No errors: every face present, a valid name, sizes the game can use.
	[[nodiscard]] bool CanExport() const;
	// After an export: where the files went and how to see the sky in game.
	void ShowExported( const QString &szFolder );

signals:
	void ExportRequested();   // to the game's write folder
	void ExportToRequested(); // to a folder the user picks

protected:
	void dragEnterEvent( QDragEnterEvent *pEvent ) override;
	void dropEvent( QDropEvent *pEvent ) override;

private:
	void BrowseFace( xv::sky::Face_t nFace );
	void BrowseFiles();
	void FacesChanged();
	void Relint();
	void UpdateSizeEstimate();

	CSkyDocument *m_pDocument = nullptr;
	CSkyView *m_pView = nullptr;
	std::array<QWidget *, 6> m_Slots{};
	QLineEdit *m_pName = nullptr;
	QComboBox *m_pFormat = nullptr;
	QComboBox *m_pSize = nullptr;
	QCheckBox *m_pMipmaps = nullptr;
	QLabel *m_pEstimate = nullptr;
	QLabel *m_pExported = nullptr;
	QListWidget *m_pIssueList = nullptr;
	std::vector<Issue_t> m_Issues;
};
