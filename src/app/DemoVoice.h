//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer's Voice tab: every stretch of talking in the demo, a row each (when,
//          who, how long), to hear alone, go to, or save as WAV (each clip, or all of a
//          talker's in one file).
//
//===========================================================================//
#pragma once

#include <memory>
#include <vector>

#include <QWidget>

#include "demo/Contents.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;
class QToolButton;
class CDemoViewer;

class CDemoVoice : public QWidget
{
	Q_OBJECT
public:
	explicit CDemoVoice( CDemoViewer &viewer, QWidget *pParent = nullptr );
	~CDemoVoice() override;
	void UpdateButtons();

	// The list: a row for each of GetSpeech()'s stretches, in their order.
	[[nodiscard]] QTableWidget *GetList() const { return m_pList; }
	// Each stretch (GetSpeech()'s indices) as a WAV in the folder; how many were written.
	int SaveClips( const QString &szFolder, const std::vector<std::size_t> &speech ) const;
	// All of a talker's stretches in one WAV, in time order, the silences between them kept
	// (each two seconds at most).
	bool SaveTalker( int nEntity, const QString &szFile ) const;

protected:
	void showEvent( QShowEvent *pEvent ) override;

private:
	void Fill();
	void UpdateRow( std::size_t nSpeech );
	void ApplyFilter();
	void UpdateStatus();
	void ShowMenu( const QPoint &point );
	void PlaySelected();
	void ExportSelected();
	void ExportAll();
	void ExportTalker();
	[[nodiscard]] std::vector<std::size_t> SelectedSpeech() const;
	[[nodiscard]] std::size_t SpeechOfRow( int nRow ) const;
	[[nodiscard]] QString TalkerName( int nEntity, int nTick ) const;
	[[nodiscard]] QString ClipFileName( std::size_t nSpeech ) const;

	CDemoViewer &m_Viewer;
	std::shared_ptr<const xv::demo::CContents> m_pContents;
	QTableWidget *m_pList = nullptr;
	QComboBox *m_pTalker = nullptr;
	QPushButton *m_pPlay = nullptr;
	QToolButton *m_pSave = nullptr;
	QLabel *m_pStatus = nullptr;
	QTimer *m_pPlayCheck = nullptr;
	bool m_bDecodes = false; // the demo's codec is one that decodes
};
