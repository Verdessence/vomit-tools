//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer's Voice tab: every stretch of talking in the demo, a row each (when,
//          who, how long, what was said), to hear alone, go to, or save as WAV (each clip, or
//          all of a talker's in one file), and put in words with whisper.cpp (demo/Transcribe)
//          on a thread, from a model file the user picks. Transcripts are kept in the cache
//          folder, so a demo opened again has them.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include <QWidget>

#include "demo/Contents.h"
#include "demo/Transcribe.h"

class QComboBox;
class QLabel;
class QLineEdit;
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
	// What has been put in words, by GetSpeech()'s index.
	[[nodiscard]] const std::map<std::size_t, xv::demo::Transcript_t> &GetTranscripts() const { return m_Transcripts; }

	// Puts every stretch not yet in words into words with the model, on a thread; false when
	// the model doesn't load (the status says why). The rows fill as each is done.
	bool StartTranscribing( const QString &szModel, const QString &szLanguage );
	void StopTranscribing();
	[[nodiscard]] bool IsTranscribing() const { return m_Worker.joinable(); }

	// Each stretch (GetSpeech()'s indices) as a WAV in the folder; how many were written.
	int SaveClips( const QString &szFolder, const std::vector<std::size_t> &speech ) const;
	// All of a talker's stretches in one WAV, in time order, the silences between them kept
	// (each two seconds at most).
	bool SaveTalker( int nEntity, const QString &szFile ) const;
	// Who said what when, a line each.
	[[nodiscard]] QString TranscriptText() const;
	bool SaveTranscript( const QString &szFile ) const;

signals:
	// A stretch was put in words (or the transcripts were read back from the cache).
	void TranscriptsChanged();

protected:
	void showEvent( QShowEvent *pEvent ) override;

private:
	void Fill();
	void UpdateRow( std::size_t nSpeech );
	void ApplyFilter();
	void UpdateStatus();
	void Transcribed( std::size_t nSpeech, const xv::demo::Transcript_t &transcript, int nGeneration );
	void TranscribingEnded( int nGeneration, const QString &szError );
	void AskToTranscribe();
	void ChooseModel();
	void ChooseLanguage();
	void ShowMenu( const QPoint &point );
	void PlaySelected();
	void ExportSelected();
	void ExportAll();
	void ExportTalker();
	void ExportTranscript();
	[[nodiscard]] std::vector<std::size_t> SelectedSpeech() const;
	[[nodiscard]] std::size_t SpeechOfRow( int nRow ) const;
	[[nodiscard]] QString TalkerName( int nEntity, int nTick ) const;
	[[nodiscard]] QString ClipFileName( std::size_t nSpeech ) const;
	// Where this demo's transcripts by this model are kept: the cache folder, by what makes
	// them this demo's and this model's.
	[[nodiscard]] QString CachePath( const QString &szModel, const QString &szLanguage ) const;
	void ReadCache();
	void WriteCache() const;

	CDemoViewer &m_Viewer;
	std::shared_ptr<const xv::demo::CContents> m_pContents;
	QTableWidget *m_pList = nullptr;
	QComboBox *m_pTalker = nullptr;
	QLineEdit *m_pFilter = nullptr;
	QPushButton *m_pPlay = nullptr;
	QToolButton *m_pSave = nullptr;
	QPushButton *m_pTranscribe = nullptr;
	QToolButton *m_pModel = nullptr;
	QLabel *m_pStatus = nullptr;
	QTimer *m_pPlayCheck = nullptr;
	bool m_bDecodes = false; // the demo's codec is one that decodes

	std::map<std::size_t, xv::demo::Transcript_t> m_Transcripts;
	QString m_szCachePath; // the transcripts' file for the model they came from
	std::thread m_Worker;
	std::atomic<bool> m_bStop{ false };
	int m_nGeneration = 0; // a stopped run's late results are dropped
	int m_nToDo = 0;
	int m_nDone = 0;
	QString m_szError;
};
