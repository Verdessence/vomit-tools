//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Editor for one soundscape or soundscript file: the list of entries on the left, the
//          selected entry as a card on the right, and the linter's findings underneath.
//
//===========================================================================//
#pragma once

#include <optional>
#include <vector>

#include <QWidget>

#include "sound/SoundLint.h"

class CKvCard;
class CListenCanvas;
class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class CSoundDocument;
class CSoundIndex;
class CSoundPlayer;

class CSoundEditor : public QWidget
{
	Q_OBJECT

public:
	// player may be null: no playback then.
	CSoundEditor( CSoundDocument *pDocument, const CSoundIndex &index, CSoundPlayer *pPlayer = nullptr, QWidget *pParent = nullptr );

	[[nodiscard]] CSoundDocument *GetDocument() const { return m_pDocument; }
	[[nodiscard]] const std::vector<xv::sound::Issue_t> &GetIssues() const { return m_Issues; }
	[[nodiscard]] bool GetListedInManifest() const { return m_bListedInManifest; }

	// Re-run the linter, e.g. after the game was remounted.
	void Relint();
	void SelectEntry( std::size_t nRoot );

signals:
	void AddToManifestRequested( CSoundDocument *pDocument );

private:
	void ScheduleRefresh();
	void Refresh();
	void RebuildEntryList();
	void ShowEntry();
	void Lint();
	void ShowIssueList();
	void JumpTo( const xv::sound::NodePath_t &path );
	void ShowUsedPositions();
	void AddEntry();
	void DuplicateEntry();
	void CopyEntry();
	void PasteEntries();
	void DeleteEntry();

	CSoundDocument *m_pDocument;
	const CSoundIndex &m_Index;
	CSoundPlayer *m_pPlayer;
	QLineEdit *m_pFilter = nullptr;
	QListWidget *m_pEntries = nullptr;
	QScrollArea *m_pScroll = nullptr;
	QListWidget *m_pIssueList = nullptr;
	QLabel *m_pSummary = nullptr;
	QAction *m_pDuplicate = nullptr;
	QAction *m_pDelete = nullptr;
	QAction *m_pAddToManifest = nullptr;
	CKvCard *m_pCard = nullptr;
	QAction *m_pListenAction = nullptr;
	CListenCanvas *m_pListen = nullptr;

	std::optional<std::size_t> m_Current;
	std::optional<std::size_t> m_PlayingSoundscape; // the entry this editor last played as a whole, while it plays
	std::vector<xv::sound::Issue_t> m_Issues;
	std::optional<xv::sound::NodePath_t> m_PendingEdit;
	bool m_bListedInManifest = true;
	bool m_bRefreshQueued = false;
};
