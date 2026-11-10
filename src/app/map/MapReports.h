//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's map-wide dialogs: Replace Textures (find, replace or mark faces by material),
//          the Entity Report (the map's entities filtered, selected from a list) and Check for
//          Problems (what the core's CheckMap found, with Go to and Fix). The report and the
//          check stay open beside the views and follow the map's edits.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <vector>

#include <QDialog>

#include "map/Check.h"
#include "map/Faces.h"

class CMapEditor;
class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class CMapReplaceTexturesDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CMapReplaceTexturesDialog( CMapEditor &editor );

	[[nodiscard]] xv::map::ReplaceTextures_t GetValues() const;
	[[nodiscard]] bool IsMarkOnly() const;     // Hammer's "do not replace textures (mark solids found)"
	[[nodiscard]] bool IsSelectedOnly() const; // "Marked objects" rather than "Everything"
	[[nodiscard]] bool IsHiddenToo() const;
	void SetValues( const xv::map::ReplaceTextures_t &how, bool bMarkOnly, bool bSelectedOnly, bool bHiddenToo );

private:
	void Browse( QLineEdit *pField );

	CMapEditor &m_Editor;
	QLineEdit *m_pFind = nullptr;
	QLineEdit *m_pReplace = nullptr;
	QButtonGroup *m_pIn = nullptr;
	QCheckBox *m_pHidden = nullptr;
	QButtonGroup *m_pAction = nullptr;
	QCheckBox *m_pRescale = nullptr;
};

class CMapEntityReport : public QDialog
{
	Q_OBJECT

public:
	explicit CMapEntityReport( CMapEditor &editor );

	// The list made again from the map and the filter.
	void Refresh();
	[[nodiscard]] std::vector<std::uint32_t> GetListed() const { return m_Listed; }
	[[nodiscard]] QListWidget *GetList() const { return m_pList; }

private:
	void FollowList();
	void GoTo();

	CMapEditor &m_Editor;
	QListWidget *m_pList = nullptr;
	QButtonGroup *m_pKind = nullptr;
	QCheckBox *m_pHidden = nullptr;
	QLineEdit *m_pClass = nullptr;
	QLineEdit *m_pKey = nullptr;
	QLineEdit *m_pValue = nullptr;
	QCheckBox *m_pExact = nullptr;
	std::vector<std::uint32_t> m_Listed; // by row
	bool m_bFollowing = false;           // the list is being set from the selection
};

// Check for Problems, a panel of the side: it checks the map again on each edit while it shows.
class CMapCheckPanel : public QWidget
{
	Q_OBJECT

public:
	explicit CMapCheckPanel( CMapEditor &editor );

	// The map checked again.
	void Run();
	[[nodiscard]] const std::vector<xv::map::Problem_t> &GetProblems() const { return m_Problems; }
	[[nodiscard]] QListWidget *GetList() const { return m_pList; }
	// The problem a row of the list shows (errors or warnings may be hidden); null for none.
	[[nodiscard]] const xv::map::Problem_t *ProblemAt( int nRow ) const;
	void Fix();
	void FixAll();

signals:
	// After each check: how many errors and warnings it found (for the side tab's badge).
	void CountsChanged( int nErrors, int nWarnings );

private:
	void Fill();
	void Pick();
	void GoTo();

protected:
	void showEvent( QShowEvent *pEvent ) override;

private:
	CMapEditor &m_Editor;
	QListWidget *m_pList = nullptr;
	QCheckBox *m_pVisibleOnly = nullptr;
	QCheckBox *m_pShowErrors = nullptr;
	QCheckBox *m_pShowWarnings = nullptr;
	QLabel *m_pSummary = nullptr;
	std::vector<std::size_t> m_Rows; // each row's problem, by index into m_Problems
	QPushButton *m_pGoTo = nullptr;
	QPushButton *m_pFix = nullptr;
	QPushButton *m_pFixAll = nullptr;
	std::vector<xv::map::Problem_t> m_Problems;
};
