//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoVoice.h.
//
//===========================================================================//
#include "DemoVoice.h"

#include <algorithm>

#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "DemoViewer.h"
#include "DemoWorld.h"
#include "FileDialogs.h"
#include "Preferences.h"
#include "audio/Audio.h"
#include "demo/Voice.h"

namespace
{

constexpr int COLUMN_TIME = 0;
constexpr int COLUMN_TALKER = 1;
constexpr int COLUMN_LENGTH = 2;
constexpr int COLUMN_SAID = 3;
constexpr int ROLE_SPEECH = Qt::UserRole + 1;
// A talker's stretches saved as one file: the silence between two is kept up to this long.
constexpr double MOST_GAP_SECONDS = 2.0;
// The cache file's first line; a change of what it holds changes it.
constexpr const char *CACHE_HEADER = "vomit-tools voice transcripts 1";
// Results between writes of the cache while transcribing.
constexpr int CACHE_EVERY = 20;
// Where whisper's models are, for the user to pick one from.
constexpr const char *MODELS_URL = "https://huggingface.co/ggerganov/whisper.cpp/tree/main";

//-----------------------------------------------------------------------------
// Purpose: A name made safe as part of a file name.
//-----------------------------------------------------------------------------
QString FileSafe( QString szText )
{
	static const QString s_szBad = QStringLiteral( "<>:\"/\\|?*" );
	for ( QChar &ch : szText )
	{
		if ( ch.unicode() < 32 || s_szBad.contains( ch ) )
			ch = u'_';
	}
	szText = szText.trimmed();
	return szText.isEmpty() ? QStringLiteral( "player" ) : szText.left( 48 );
}

//-----------------------------------------------------------------------------
// Purpose: Bytes into a file, whole or not at all.
//-----------------------------------------------------------------------------
bool WriteFile( const QString &szFile, const QByteArray &bytes )
{
	QSaveFile file( szFile );
	if ( !file.open( QIODevice::WriteOnly ) )
		return false;
	file.write( bytes );
	return file.commit();
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The talker filter and the text filter over the list, the buttons under it.
//-----------------------------------------------------------------------------
CDemoVoice::CDemoVoice( CDemoViewer &viewer, QWidget *pParent )
	: QWidget( pParent ),
	  m_Viewer( viewer ),
	  m_pContents( viewer.GetContentsShared() )
{
	setObjectName( QStringLiteral( "demoVoice" ) );
	m_bDecodes = m_pContents && xv::demo::CVoiceDecoder( m_pContents->GetVoiceCodec() ).IsSupported();

	m_pTalker = new QComboBox;
	m_pTalker->setObjectName( QStringLiteral( "demoVoiceTalker" ) );
	m_pTalker->setToolTip( tr( "Only one talker's stretches" ) );
	connect( m_pTalker, &QComboBox::currentIndexChanged, this, &CDemoVoice::ApplyFilter );
	m_pFilter = new QLineEdit;
	m_pFilter->setObjectName( QStringLiteral( "demoVoiceFilter" ) );
	m_pFilter->setPlaceholderText( tr( "Find in what was said" ) );
	m_pFilter->setClearButtonEnabled( true );
	connect( m_pFilter, &QLineEdit::textChanged, this, &CDemoVoice::ApplyFilter );
	auto *pTop = new QHBoxLayout;
	pTop->addWidget( m_pTalker );
	pTop->addWidget( m_pFilter, 1 );

	m_pList = new QTableWidget( 0, 4 );
	m_pList->setObjectName( QStringLiteral( "demoVoiceList" ) );
	m_pList->setHorizontalHeaderLabels( { tr( "Time" ), tr( "Talker" ), tr( "Length" ), tr( "Said" ) } );
	m_pList->setEditTriggers( QAbstractItemView::NoEditTriggers );
	m_pList->setSelectionBehavior( QAbstractItemView::SelectRows );
	m_pList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_pList->setWordWrap( true );
	m_pList->verticalHeader()->hide();
	m_pList->horizontalHeader()->setStretchLastSection( true );
	m_pList->setContextMenuPolicy( Qt::CustomContextMenu );
	m_pList->setToolTip( tr( "Double click: go there; the menu plays it or saves it" ) );
	connect( m_pList, &QTableWidget::cellDoubleClicked, this, [ this ]( int nRow ) {
		const std::size_t nSpeech = SpeechOfRow( nRow );
		if ( m_pContents && nSpeech < m_pContents->GetSpeech().size() )
			m_Viewer.SetTick( m_pContents->GetSpeech()[ nSpeech ].m_nFrom );
	} );
	connect( m_pList, &QWidget::customContextMenuRequested, this, &CDemoVoice::ShowMenu );
	connect( m_pList, &QTableWidget::itemSelectionChanged, this, &CDemoVoice::UpdateButtons );

	m_pPlay = new QPushButton( tr( "Play" ) );
	m_pPlay->setObjectName( QStringLiteral( "demoVoicePlay" ) );
	connect( m_pPlay, &QPushButton::clicked, this, [ this ] {
		if ( m_Viewer.GetSound().IsPlayingSpeech() )
			m_Viewer.GetSound().StopSpeech();
		else
			PlaySelected();
		UpdateButtons();
	} );
	m_pPlayCheck = new QTimer( this );
	m_pPlayCheck->setInterval( 200 );
	connect( m_pPlayCheck, &QTimer::timeout, this, [ this ] {
		if ( !m_Viewer.GetSound().IsPlayingSpeech() )
		{
			m_pPlayCheck->stop();
			UpdateButtons();
		}
	} );

	m_pSave = new QToolButton;
	m_pSave->setObjectName( QStringLiteral( "demoVoiceSave" ) );
	m_pSave->setText( tr( "Save" ) );
	m_pSave->setPopupMode( QToolButton::InstantPopup );
	auto *pSaveMenu = new QMenu( m_pSave );
	pSaveMenu->addAction( tr( "Picked Stretches as WAV…" ), this, &CDemoVoice::ExportSelected );
	pSaveMenu->addAction( tr( "Every Stretch Shown as WAV…" ), this, &CDemoVoice::ExportAll );
	pSaveMenu->addAction( tr( "All of the Picked Talker's as One WAV…" ), this, &CDemoVoice::ExportTalker );
	pSaveMenu->addSeparator();
	pSaveMenu->addAction( tr( "Transcript…" ), this, &CDemoVoice::ExportTranscript );
	m_pSave->setMenu( pSaveMenu );

	m_pTranscribe = new QPushButton( tr( "Transcribe" ) );
	m_pTranscribe->setObjectName( QStringLiteral( "demoVoiceTranscribe" ) );
	m_pTranscribe->setToolTip( tr( "Put what was said in words, on this computer, with a whisper.cpp model" ) );
	connect( m_pTranscribe, &QPushButton::clicked, this, [ this ] {
		if ( IsTranscribing() )
			StopTranscribing();
		else
			AskToTranscribe();
	} );
	m_pModel = new QToolButton;
	m_pModel->setObjectName( QStringLiteral( "demoVoiceModel" ) );
	m_pModel->setText( tr( "Model" ) );
	m_pModel->setPopupMode( QToolButton::InstantPopup );
	auto *pModelMenu = new QMenu( m_pModel );
	pModelMenu->addAction( tr( "Choose Model…" ), this, &CDemoVoice::ChooseModel );
	pModelMenu->addAction( tr( "Language…" ), this, &CDemoVoice::ChooseLanguage );
	m_pModel->setMenu( pModelMenu );
	if ( !xv::demo::CTranscriber::IsAvailable() )
	{
		m_pTranscribe->hide();
		m_pModel->hide();
	}

	auto *pBottom = new QHBoxLayout;
	pBottom->addWidget( m_pPlay );
	pBottom->addWidget( m_pSave );
	pBottom->addStretch( 1 );
	pBottom->addWidget( m_pTranscribe );
	pBottom->addWidget( m_pModel );

	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "demoVoiceStatus" ) );
	m_pStatus->setWordWrap( true );
	m_pStatus->setTextInteractionFlags( Qt::TextBrowserInteraction );
	m_pStatus->setOpenExternalLinks( true );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 4, 4, 4, 4 );
	pLayout->addLayout( pTop );
	pLayout->addWidget( m_pList, 1 );
	pLayout->addLayout( pBottom );
	pLayout->addWidget( m_pStatus );

	Fill();
	ReadCache();
	UpdateStatus();
	UpdateButtons();
}

//-----------------------------------------------------------------------------
// Purpose: A transcription under way stops (whisper is asked to, between its steps).
//-----------------------------------------------------------------------------
CDemoVoice::~CDemoVoice()
{
	// The viewer's members (its sound) may be gone already: nothing on screen is touched.
	if ( m_Worker.joinable() )
	{
		m_bStop = true;
		m_Worker.join();
		WriteCache();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The rows, and the talkers in the filter, each by its name at its first stretch.
//-----------------------------------------------------------------------------
void CDemoVoice::Fill()
{
	m_pList->setRowCount( 0 );
	m_pTalker->clear();
	m_pTalker->addItem( tr( "Everyone" ), 0 );
	if ( !m_pContents )
		return;
	const std::vector<xv::demo::Speech_t> &speech = m_pContents->GetSpeech();
	m_pList->setRowCount( static_cast<int>( speech.size() ) );
	std::map<int, QString> talkers;
	for ( std::size_t i = 0; i < speech.size(); i++ )
	{
		const int nRow = static_cast<int>( i );
		for ( int nColumn = 0; nColumn < 4; nColumn++ )
			m_pList->setItem( nRow, nColumn, new QTableWidgetItem );
		m_pList->item( nRow, COLUMN_TIME )->setData( ROLE_SPEECH, static_cast<qulonglong>( i ) );
		m_pList->item( nRow, COLUMN_LENGTH )->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
		talkers.try_emplace( speech[ i ].m_nEntity, TalkerName( speech[ i ].m_nEntity, speech[ i ].m_nFrom ) );
		UpdateRow( i );
	}
	for ( const auto &[ nEntity, szName ] : talkers )
		m_pTalker->addItem( szName, nEntity );
	m_pList->resizeColumnToContents( COLUMN_TIME );
	m_pList->resizeColumnToContents( COLUMN_TALKER );
	m_pList->resizeColumnToContents( COLUMN_LENGTH );
}

//-----------------------------------------------------------------------------
// Purpose: A row's cells: when, who (in its team's colour), how long, what was said.
//-----------------------------------------------------------------------------
void CDemoVoice::UpdateRow( std::size_t nSpeech )
{
	const xv::demo::Speech_t &talk = m_pContents->GetSpeech()[ nSpeech ];
	const int nRow = static_cast<int>( nSpeech );
	const double flInterval = m_pContents->GetTickInterval() > 0.0 ? m_pContents->GetTickInterval() : 0.015;
	m_pList->item( nRow, COLUMN_TIME )->setText( m_Viewer.FormatTime( talk.m_nFrom ) );
	QTableWidgetItem *pTalker = m_pList->item( nRow, COLUMN_TALKER );
	pTalker->setText( TalkerName( talk.m_nEntity, talk.m_nFrom ) );
	if ( const int nTeam = m_Viewer.PlayerTeam( talk.m_nEntity, talk.m_nFrom ); nTeam >= 2 )
		pTalker->setForeground( DemoTeamColor( nTeam ) );
	m_pList->item( nRow, COLUMN_LENGTH )->setText( tr( "%1 s" ).arg( std::max( 1, talk.m_nTo - talk.m_nFrom ) * flInterval, 0, 'f', 1 ) );
	QTableWidgetItem *pSaid = m_pList->item( nRow, COLUMN_SAID );
	if ( const auto found = m_Transcripts.find( nSpeech ); found != m_Transcripts.end() )
	{
		const xv::demo::Transcript_t &transcript = found->second;
		pSaid->setText( transcript.m_szText.empty() ? tr( "(no words heard)" ) : QString::fromStdString( transcript.m_szText ) );
		pSaid->setForeground( transcript.m_szText.empty() ? palette().color( QPalette::PlaceholderText ) : palette().color( QPalette::Text ) );
		pSaid->setToolTip( transcript.m_szLanguage.empty() ? QString{} : tr( "Heard as %1" ).arg( QString::fromStdString( transcript.m_szLanguage ) ) );
	}
	else
	{
		pSaid->setText( {} );
		pSaid->setToolTip( {} );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Rows of other talkers, and those whose words and talker don't hold the text, hide.
//-----------------------------------------------------------------------------
void CDemoVoice::ApplyFilter()
{
	if ( !m_pContents )
		return;
	const int nTalker = m_pTalker->currentData().toInt();
	const QString szFind = m_pFilter->text().trimmed();
	const std::vector<xv::demo::Speech_t> &speech = m_pContents->GetSpeech();
	for ( int nRow = 0; nRow < m_pList->rowCount(); nRow++ )
	{
		const std::size_t nSpeech = SpeechOfRow( nRow );
		bool bShown = nTalker == 0 || speech[ nSpeech ].m_nEntity == nTalker;
		if ( bShown && !szFind.isEmpty() )
			bShown = m_pList->item( nRow, COLUMN_SAID )->text().contains( szFind, Qt::CaseInsensitive ) || m_pList->item( nRow, COLUMN_TALKER )->text().contains( szFind, Qt::CaseInsensitive );
		m_pList->setRowHidden( nRow, !bShown );
	}
}

//-----------------------------------------------------------------------------
// Purpose: How much talking there is, and how far a transcription has come.
//-----------------------------------------------------------------------------
void CDemoVoice::UpdateStatus()
{
	if ( !m_pContents || m_pContents->GetSpeech().empty() )
	{
		m_pStatus->setText( tr( "Nobody talked in this demo (or the demo has no voice)." ) );
		return;
	}
	if ( !m_bDecodes )
	{
		m_pStatus->setText( tr( "This demo's voice is %1, which isn't decoded: the stretches are listed, but can't be heard or put in words." ).arg( QString::fromStdString( m_pContents->GetVoiceCodec().m_szCodec ) ) );
		return;
	}
	const double flInterval = m_pContents->GetTickInterval() > 0.0 ? m_pContents->GetTickInterval() : 0.015;
	double flSeconds = 0.0;
	for ( const xv::demo::Speech_t &talk : m_pContents->GetSpeech() )
		flSeconds += std::max( 1, talk.m_nTo - talk.m_nFrom ) * flInterval;
	QString szText = tr( "%L1 stretches of talking, %2 in all." ).arg( m_pContents->GetSpeech().size() ).arg( m_Viewer.FormatTime( static_cast<int>( flSeconds / flInterval ) ) );
	if ( IsTranscribing() )
		szText += QLatin1Char( ' ' ) + tr( "Transcribing: %L1 of %L2…" ).arg( m_nDone ).arg( m_nToDo );
	else if ( !m_szError.isEmpty() )
		szText += QLatin1Char( ' ' ) + m_szError;
	else if ( !m_Transcripts.empty() )
		szText += QLatin1Char( ' ' ) + tr( "%L1 in words." ).arg( m_Transcripts.size() );
	m_pStatus->setText( szText );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::UpdateButtons()
{
	const bool bPlaying = m_Viewer.GetSound().IsPlayingSpeech();
	m_pPlay->setText( bPlaying ? tr( "Stop" ) : tr( "Play" ) );
	const auto picked = SelectedSpeech();
	const bool bMuted = !picked.empty() && !m_Viewer.GetSound().CanPlaySpeech( picked.front() );
	m_pPlay->setEnabled( m_bDecodes && ( bPlaying || ( !picked.empty() && !bMuted ) ) );
	m_pPlay->setToolTip( bMuted && !bPlaying ? tr( "This player’s voice is muted" ) : tr( "Hear the picked stretch alone, from its start" ) );
	m_pSave->setEnabled( m_bDecodes && m_pList->rowCount() > 0 );
	m_pTranscribe->setText( IsTranscribing() ? tr( "Stop Transcribing" ) : tr( "Transcribe" ) );
	m_pTranscribe->setEnabled( m_bDecodes && m_pList->rowCount() > 0 );
}

//-----------------------------------------------------------------------------
// Purpose: The stretches not yet in words, in time order, handed to a thread that puts each
//          in words and posts it back.
//-----------------------------------------------------------------------------
bool CDemoVoice::StartTranscribing( const QString &szModel, const QString &szLanguage )
{
	StopTranscribing();
	m_szError.clear();
	if ( !m_pContents || !m_bDecodes )
		return false;
	auto pTranscriber = std::make_shared<xv::demo::CTranscriber>( std::filesystem::path( szModel.toStdU16String() ), szLanguage.toStdString() );
	if ( !pTranscriber->IsOk() )
	{
		m_szError = QString::fromStdString( pTranscriber->GetError() );
		UpdateStatus();
		return false;
	}
	// Transcripts by another model or language aren't these.
	const QString szCache = CachePath( szModel, szLanguage );
	if ( szCache != m_szCachePath )
	{
		m_Transcripts.clear();
		m_szCachePath = szCache;
		ReadCache();
		for ( std::size_t i = 0; i < m_pContents->GetSpeech().size(); i++ )
			UpdateRow( i );
	}
	std::vector<std::size_t> order;
	for ( std::size_t i = 0; i < m_pContents->GetSpeech().size(); i++ )
	{
		if ( !m_Transcripts.contains( i ) )
			order.push_back( i );
	}
	m_nToDo = static_cast<int>( order.size() );
	m_nDone = 0;
	if ( order.empty() )
	{
		UpdateStatus();
		return true;
	}
	m_bStop = false;
	const int nGeneration = ++m_nGeneration;
	m_Worker = std::thread( [ this, pTranscriber, order = std::move( order ), pContents = m_pContents, nGeneration ] {
		xv::demo::TranscribeSpeech( *pContents, *pTranscriber, order, [ & ]( std::size_t nSpeech, const xv::demo::Transcript_t &transcript ) { QMetaObject::invokeMethod( this, [ this, nSpeech, transcript, nGeneration ] { Transcribed( nSpeech, transcript, nGeneration ); }, Qt::QueuedConnection ); }, &m_bStop );
		QMetaObject::invokeMethod( this, [ this, nGeneration ] { TranscribingEnded( nGeneration, {} ); }, Qt::QueuedConnection );
	} );
	UpdateStatus();
	UpdateButtons();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The thread asked to stop and waited for; what it did is kept.
//-----------------------------------------------------------------------------
void CDemoVoice::StopTranscribing()
{
	if ( !m_Worker.joinable() )
		return;
	m_bStop = true;
	m_Worker.join();
	m_nGeneration++;
	WriteCache();
	UpdateStatus();
	UpdateButtons();
}

//-----------------------------------------------------------------------------
// Purpose: One stretch in words, from the thread.
//-----------------------------------------------------------------------------
void CDemoVoice::Transcribed( std::size_t nSpeech, const xv::demo::Transcript_t &transcript, int nGeneration )
{
	if ( nGeneration != m_nGeneration || !m_pContents || nSpeech >= m_pContents->GetSpeech().size() )
		return;
	m_Transcripts[ nSpeech ] = transcript;
	m_nDone++;
	UpdateRow( nSpeech );
	if ( !m_pFilter->text().isEmpty() )
		ApplyFilter();
	if ( m_nDone % CACHE_EVERY == 0 )
		WriteCache();
	UpdateStatus();
	emit TranscriptsChanged();
}

//-----------------------------------------------------------------------------
// Purpose: The thread is done: joined, and what it did kept.
//-----------------------------------------------------------------------------
void CDemoVoice::TranscribingEnded( int nGeneration, const QString &szError )
{
	if ( nGeneration != m_nGeneration )
		return;
	if ( m_Worker.joinable() )
		m_Worker.join();
	m_szError = szError;
	WriteCache();
	UpdateStatus();
	UpdateButtons();
}

//-----------------------------------------------------------------------------
// Purpose: With a model picked, transcribing starts; without one, where to get one is said
//          and one is asked for.
//-----------------------------------------------------------------------------
void CDemoVoice::AskToTranscribe()
{
	CPreferences &preferences = CPreferences::Instance();
	if ( preferences.GetWhisperModel().isEmpty() || !QFileInfo::exists( preferences.GetWhisperModel() ) )
	{
		QMessageBox box( QMessageBox::Information, tr( "Transcribe" ),
			tr( "Transcripts are made on this computer by whisper.cpp, from a model file you pick. Models are at <a href=\"%1\">%1</a>: ggml-base.bin (148 MB) hears every language whisper knows; ggml-base.en.bin English only, a little better at it; the small and medium ones are better and slower." ).arg( QLatin1String( MODELS_URL ) ),
			QMessageBox::Cancel, this );
		box.setTextFormat( Qt::RichText );
		box.setTextInteractionFlags( Qt::TextBrowserInteraction );
		QPushButton *pChoose = box.addButton( tr( "Choose Model…" ), QMessageBox::AcceptRole );
		box.exec();
		if ( box.clickedButton() != pChoose )
			return;
		ChooseModel();
		if ( preferences.GetWhisperModel().isEmpty() )
			return;
	}
	StartTranscribing( preferences.GetWhisperModel(), preferences.GetWhisperLanguage() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::ChooseModel()
{
	CPreferences &preferences = CPreferences::Instance();
	const QString szModel = AskOpenFile( this, tr( "Choose a whisper.cpp Model" ), preferences.GetWhisperModel(), tr( "whisper.cpp models (*.bin);;All files (*)" ) );
	if ( szModel.isEmpty() )
		return;
	preferences.SetWhisperModel( szModel );
	// What that model made of this demo before, if anything.
	m_Transcripts.clear();
	m_szCachePath.clear();
	ReadCache();
	for ( std::size_t i = 0; m_pContents && i < m_pContents->GetSpeech().size(); i++ )
		UpdateRow( i );
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::ChooseLanguage()
{
	CPreferences &preferences = CPreferences::Instance();
	bool bOk = false;
	const QString szLanguage = QInputDialog::getText( this, tr( "Language" ), tr( "The language listened for: auto (told for each talker), or a code such as en, ru, de, pt:" ), QLineEdit::Normal, preferences.GetWhisperLanguage(), &bOk );
	if ( bOk )
		preferences.SetWhisperLanguage( szLanguage );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::ShowMenu( const QPoint &point )
{
	const int nRow = m_pList->rowAt( point.y() );
	if ( nRow < 0 )
		return;
	if ( !m_pList->item( nRow, 0 )->isSelected() )
		m_pList->selectRow( nRow );
	const std::size_t nSpeech = SpeechOfRow( nRow );
	QMenu menu( this );
	menu.addAction( tr( "Play" ), this, [ this, nSpeech ] {
			if ( m_Viewer.GetSound().PlaySpeech( nSpeech ) )
				m_pPlayCheck->start();
			UpdateButtons();
		} )
		->setEnabled( m_bDecodes && m_Viewer.GetSound().CanPlaySpeech( nSpeech ) );
	menu.addAction( tr( "Go There" ), this, [ this, nSpeech ] { m_Viewer.SetTick( m_pContents->GetSpeech()[ nSpeech ].m_nFrom ); } );
	menu.addSeparator();
	menu.addAction( tr( "Save as WAV…" ), this, &CDemoVoice::ExportSelected )->setEnabled( m_bDecodes );
	menu.addAction( tr( "Save All of This Talker's as One WAV…" ), this, &CDemoVoice::ExportTalker )->setEnabled( m_bDecodes );
	const QString szSaid = m_pList->item( nRow, COLUMN_SAID )->text();
	if ( m_Transcripts.contains( nSpeech ) )
		menu.addAction( tr( "Copy What Was Said" ), this, [ szSaid ] { QGuiApplication::clipboard()->setText( szSaid ); } );
	menu.exec( m_pList->viewport()->mapToGlobal( point ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::PlaySelected()
{
	const std::vector<std::size_t> picked = SelectedSpeech();
	if ( picked.empty() )
		return;
	if ( m_Viewer.GetSound().PlaySpeech( picked.front() ) )
		m_pPlayCheck->start();
	UpdateButtons();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::ExportSelected()
{
	const std::vector<std::size_t> picked = SelectedSpeech();
	if ( picked.empty() )
		return;
	const QString szFolder = AskFolder( this, tr( "Save the Stretches in" ) );
	if ( szFolder.isEmpty() )
		return;
	SaveClips( szFolder, picked );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::ExportAll()
{
	std::vector<std::size_t> shown;
	for ( int nRow = 0; nRow < m_pList->rowCount(); nRow++ )
	{
		if ( !m_pList->isRowHidden( nRow ) )
			shown.push_back( SpeechOfRow( nRow ) );
	}
	if ( shown.empty() )
		return;
	const QString szFolder = AskFolder( this, tr( "Save %n Stretches in", nullptr, static_cast<int>( shown.size() ) ) );
	if ( szFolder.isEmpty() )
		return;
	const int nWritten = SaveClips( szFolder, shown );
	if ( nWritten < static_cast<int>( shown.size() ) )
		QMessageBox::warning( this, tr( "Save" ), tr( "%1 of %2 stretches were saved; the rest didn't decode or couldn't be written." ).arg( nWritten ).arg( shown.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: The picked row's talker (else the filter's).
//-----------------------------------------------------------------------------
void CDemoVoice::ExportTalker()
{
	const std::vector<std::size_t> picked = SelectedSpeech();
	const int nEntity = !picked.empty() ? m_pContents->GetSpeech()[ picked.front() ].m_nEntity : m_pTalker->currentData().toInt();
	if ( nEntity == 0 )
		return;
	const xv::demo::Speech_t *pFirst = nullptr;
	for ( const xv::demo::Speech_t &talk : m_pContents->GetSpeech() )
	{
		if ( talk.m_nEntity == nEntity )
		{
			pFirst = &talk;
			break;
		}
	}
	const QString szName = pFirst ? TalkerName( nEntity, pFirst->m_nFrom ) : QString{};
	const QString szFile = AskSaveFile( this, tr( "Save All of %1's Talking" ).arg( szName ), FileSafe( szName ) + QStringLiteral( ".wav" ), tr( "WAV (*.wav)" ) );
	if ( !szFile.isEmpty() && !SaveTalker( nEntity, szFile ) )
		QMessageBox::warning( this, tr( "Save" ), tr( "%1 couldn't be written." ).arg( szFile ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::ExportTranscript()
{
	const QString szFile = AskSaveFile( this, tr( "Save the Transcript" ), QFileInfo( m_Viewer.GetTabTitle() ).completeBaseName() + QStringLiteral( " voice.txt" ), tr( "Text (*.txt)" ) );
	if ( !szFile.isEmpty() && !SaveTranscript( szFile ) )
		QMessageBox::warning( this, tr( "Save" ), tr( "%1 couldn't be written." ).arg( szFile ) );
}

//-----------------------------------------------------------------------------
// Purpose: Each decoded and written; a file name says when, who and which.
//-----------------------------------------------------------------------------
int CDemoVoice::SaveClips( const QString &szFolder, const std::vector<std::size_t> &speech ) const
{
	if ( !m_pContents || !QDir().mkpath( szFolder ) )
		return 0;
	int nWritten = 0;
	for ( const std::size_t nSpeech : speech )
	{
		if ( nSpeech >= m_pContents->GetSpeech().size() )
			continue;
		int nRate = 0;
		const std::vector<float> samples = xv::demo::DecodeSpeech( *m_pContents, m_pContents->GetSpeech()[ nSpeech ], &nRate );
		if ( samples.empty() || nRate <= 0 )
			continue;
		const std::vector<std::byte> wav = xv::audio::EncodeWav( samples, static_cast<std::uint32_t>( nRate ) );
		if ( WriteFile( QDir( szFolder ).filePath( ClipFileName( nSpeech ) ), QByteArray( reinterpret_cast<const char *>( wav.data() ), static_cast<qsizetype>( wav.size() ) ) ) )
			nWritten++;
	}
	return nWritten;
}

//-----------------------------------------------------------------------------
// Purpose: The talker's stretches one after another at the decoder's rate, silence between
//          them as long as the demo had (up to MOST_GAP_SECONDS).
//-----------------------------------------------------------------------------
bool CDemoVoice::SaveTalker( int nEntity, const QString &szFile ) const
{
	if ( !m_pContents )
		return false;
	const double flInterval = m_pContents->GetTickInterval() > 0.0 ? m_pContents->GetTickInterval() : 0.015;
	std::vector<float> all;
	int nRate = 0;
	int nLastTo = -1;
	for ( const xv::demo::Speech_t &talk : m_pContents->GetSpeech() )
	{
		if ( talk.m_nEntity != nEntity )
			continue;
		int nClipRate = 0;
		const std::vector<float> samples = xv::demo::DecodeSpeech( *m_pContents, talk, &nClipRate );
		if ( samples.empty() || ( nRate > 0 && nClipRate != nRate ) )
			continue;
		nRate = nClipRate;
		if ( nLastTo >= 0 )
		{
			const double flGap = std::clamp( ( talk.m_nFrom - nLastTo ) * flInterval, 0.0, MOST_GAP_SECONDS );
			all.resize( all.size() + static_cast<std::size_t>( flGap * nRate ), 0.0f );
		}
		all.insert( all.end(), samples.begin(), samples.end() );
		nLastTo = talk.m_nTo;
	}
	if ( all.empty() )
		return false;
	const std::vector<std::byte> wav = xv::audio::EncodeWav( all, static_cast<std::uint32_t>( nRate ) );
	return WriteFile( szFile, QByteArray( reinterpret_cast<const char *>( wav.data() ), static_cast<qsizetype>( wav.size() ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: "[12:34.5] name: words", in time order; stretches not in words yet say so.
//-----------------------------------------------------------------------------
QString CDemoVoice::TranscriptText() const
{
	QString szText;
	if ( !m_pContents )
		return szText;
	const std::vector<xv::demo::Speech_t> &speech = m_pContents->GetSpeech();
	for ( std::size_t i = 0; i < speech.size(); i++ )
	{
		const auto found = m_Transcripts.find( i );
		const QString szSaid = found == m_Transcripts.end() ? tr( "(not transcribed)" ) : found->second.m_szText.empty() ? tr( "(no words heard)" )
																														 : QString::fromStdString( found->second.m_szText );
		szText += QStringLiteral( "[%1] %2: %3\n" ).arg( m_Viewer.FormatTime( speech[ i ].m_nFrom ), TalkerName( speech[ i ].m_nEntity, speech[ i ].m_nFrom ), szSaid );
	}
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CDemoVoice::SaveTranscript( const QString &szFile ) const
{
	return WriteFile( szFile, TranscriptText().toUtf8() );
}

//-----------------------------------------------------------------------------
// Purpose: The selected rows' stretches, in list order.
//-----------------------------------------------------------------------------
std::vector<std::size_t> CDemoVoice::SelectedSpeech() const
{
	std::vector<std::size_t> picked;
	for ( int nRow = 0; nRow < m_pList->rowCount(); nRow++ )
	{
		if ( m_pList->item( nRow, 0 ) && m_pList->item( nRow, 0 )->isSelected() && !m_pList->isRowHidden( nRow ) )
			picked.push_back( SpeechOfRow( nRow ) );
	}
	return picked;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CDemoVoice::SpeechOfRow( int nRow ) const
{
	const QTableWidgetItem *pItem = m_pList->item( nRow, COLUMN_TIME );
	return pItem ? static_cast<std::size_t>( pItem->data( ROLE_SPEECH ).toULongLong() ) : 0;
}

//-----------------------------------------------------------------------------
// Purpose: The talker's name then, else its slot.
//-----------------------------------------------------------------------------
QString CDemoVoice::TalkerName( int nEntity, int nTick ) const
{
	const QString szName = m_Viewer.PlayerName( nEntity, nTick );
	return szName.isEmpty() ? tr( "player %1" ).arg( nEntity ) : szName;
}

//-----------------------------------------------------------------------------
// Purpose: "0042 12m34s name.wav": sorted as the list is.
//-----------------------------------------------------------------------------
QString CDemoVoice::ClipFileName( std::size_t nSpeech ) const
{
	const xv::demo::Speech_t &talk = m_pContents->GetSpeech()[ nSpeech ];
	const double flInterval = m_pContents->GetTickInterval() > 0.0 ? m_pContents->GetTickInterval() : 0.015;
	const int nSeconds = static_cast<int>( talk.m_nFrom * flInterval );
	return QStringLiteral( "%1 %2m%3s %4.wav" ).arg( nSpeech, 4, 10, QLatin1Char( '0' ) ).arg( nSeconds / 60, 2, 10, QLatin1Char( '0' ) ).arg( nSeconds % 60, 2, 10, QLatin1Char( '0' ) ).arg( FileSafe( TalkerName( talk.m_nEntity, talk.m_nFrom ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: The demo by what it is (its size, server, map, length and talking), the model by
//          its file's name and size, and the language asked for.
//-----------------------------------------------------------------------------
QString CDemoVoice::CachePath( const QString &szModel, const QString &szLanguage ) const
{
	if ( !m_pContents || szModel.isEmpty() )
		return {};
	const xv::demo::Header_t &header = m_Viewer.GetDemo().GetHeader();
	QCryptographicHash hash( QCryptographicHash::Sha1 );
	hash.addData( QByteArray::number( static_cast<qulonglong>( m_Viewer.GetDemo().GetFileSize() ) ) );
	hash.addData( QByteArray::fromStdString( header.m_szServer + '\n' + header.m_szMap + '\n' + header.m_szClient ) );
	hash.addData( QByteArray::number( m_Viewer.GetDemo().GetLastTick() ) );
	hash.addData( QByteArray::number( static_cast<qulonglong>( m_pContents->GetSpeech().size() ) ) );
	const QFileInfo model( szModel );
	hash.addData( model.fileName().toUtf8() + '\n' + QByteArray::number( model.size() ) );
	hash.addData( szLanguage.toUtf8() );
	return QStandardPaths::writableLocation( QStandardPaths::CacheLocation ) + QStringLiteral( "/voice-transcripts/" ) + QString::fromLatin1( hash.result().toHex() ) + QStringLiteral( ".txt" );
}

//-----------------------------------------------------------------------------
// Purpose: What the model picked made of this demo before: "index\tlanguage\ttext" lines.
//-----------------------------------------------------------------------------
void CDemoVoice::ReadCache()
{
	if ( m_szCachePath.isEmpty() )
		m_szCachePath = CachePath( CPreferences::Instance().GetWhisperModel(), CPreferences::Instance().GetWhisperLanguage() );
	QFile file( m_szCachePath );
	if ( m_szCachePath.isEmpty() || !m_pContents || !file.open( QIODevice::ReadOnly ) )
		return;
	if ( file.readLine().trimmed() != CACHE_HEADER )
		return;
	while ( !file.atEnd() )
	{
		const QList<QByteArray> fields = file.readLine().chopped( 1 ).split( '\t' );
		if ( fields.size() < 3 )
			continue;
		bool bOk = false;
		const qulonglong nSpeech = fields[ 0 ].toULongLong( &bOk );
		if ( !bOk || nSpeech >= m_pContents->GetSpeech().size() )
			continue;
		xv::demo::Transcript_t transcript;
		transcript.m_szLanguage = fields[ 1 ].toStdString();
		transcript.m_szText = fields.mid( 2 ).join( ' ' ).toStdString();
		m_Transcripts[ static_cast<std::size_t>( nSpeech ) ] = std::move( transcript );
	}
	for ( const auto &[ nSpeech, transcript ] : m_Transcripts )
		UpdateRow( nSpeech );
	if ( !m_Transcripts.empty() )
		emit TranscriptsChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::WriteCache() const
{
	if ( m_szCachePath.isEmpty() || m_Transcripts.empty() )
		return;
	QDir().mkpath( QFileInfo( m_szCachePath ).absolutePath() );
	QByteArray bytes = QByteArray( CACHE_HEADER ) + '\n';
	for ( const auto &[ nSpeech, transcript ] : m_Transcripts )
	{
		QByteArray szText = QByteArray::fromStdString( transcript.m_szText );
		szText.replace( '\n', ' ' ).replace( '\t', ' ' ).replace( '\r', ' ' );
		bytes += QByteArray::number( static_cast<qulonglong>( nSpeech ) ) + '\t' + QByteArray::fromStdString( transcript.m_szLanguage ) + '\t' + szText + '\n';
	}
	WriteFile( m_szCachePath, bytes );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoVoice::showEvent( QShowEvent *pEvent )
{
	QWidget::showEvent( pEvent );
	UpdateButtons();
}
