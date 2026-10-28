//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapCompileDialog.h.
//
//===========================================================================//
#include "MapCompileDialog.h"

#include <algorithm>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

#include "map/GameConfig.h"
#include "map/MapEditor.h"

namespace
{

constexpr const char *NORMAL_SETTING = "compile/normal"; // the Normal tab's choices, as one string

//-----------------------------------------------------------------------------
// Purpose: Hammer's Run Map radio rows: No, Normal and a third choice.
//-----------------------------------------------------------------------------
QButtonGroup *MakeRow( QFormLayout *pForm, const QString &szLabel, const QString &szThird, const char *pszName )
{
	auto *pGroup = new QButtonGroup( pForm );
	auto *pRow = new QHBoxLayout;
	const QString choices[] = { QObject::tr( "No" ), QObject::tr( "Normal" ), szThird };
	for ( int i = 0; i < 3; i++ )
	{
		auto *pButton = new QRadioButton( choices[ i ] );
		pButton->setObjectName( QStringLiteral( "%1%2" ).arg( QLatin1String( pszName ) ).arg( i ) );
		pGroup->addButton( pButton, i );
		pRow->addWidget( pButton );
	}
	pRow->addStretch( 1 );
	pForm->addRow( szLabel, pRow );
	pGroup->button( 1 )->setChecked( true );
	return pGroup;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapCompileDialog::SequencesSetting()
{
	return QStringLiteral( "compile/sequences" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapCompileDialog::CMapCompileDialog( CMapEditor &editor )
	: QDialog( &editor ),
	  m_Editor( editor )
{
	setObjectName( QStringLiteral( "mapCompileDialog" ) );
	setWindowTitle( tr( "Run Map" ) );
	resize( 760, 640 );
	m_pRunner = new CCompileRunner( this );
	auto *pLayout = new QVBoxLayout( this );
	m_pTabs = new QTabWidget;
	m_pTabs->setObjectName( QStringLiteral( "compileTabs" ) );

	// Normal: Hammer's three rows, HDR, the game.
	auto *pNormal = new QWidget;
	auto *pForm = new QFormLayout( pNormal );
	m_pBsp = MakeRow( pForm, tr( "Run BSP" ), tr( "Only Entities" ), "compileBsp" );
	m_pVis = MakeRow( pForm, tr( "Run VIS" ), tr( "Fast" ), "compileVis" );
	m_pRad = MakeRow( pForm, tr( "Run RAD" ), tr( "Fast" ), "compileRad" );
	m_pHdr = new QCheckBox( tr( "HDR (lights both ways: LDR and HDR)" ) );
	m_pHdr->setObjectName( QStringLiteral( "compileHdr" ) );
	pForm->addRow( QString{}, m_pHdr );
	m_pNoGame = new QCheckBox( tr( "Don't run the game after compiling" ) );
	m_pNoGame->setObjectName( QStringLiteral( "compileNoGame" ) );
	pForm->addRow( QString{}, m_pNoGame );
	m_pGameParameters = new QLineEdit;
	m_pGameParameters->setObjectName( QStringLiteral( "compileGameParameters" ) );
	m_pGameParameters->setPlaceholderText( tr( "-novid -w 1280 -h 720…" ) );
	pForm->addRow( tr( "Additional game parameters" ), m_pGameParameters );
	m_pTabs->addTab( pNormal, tr( "Normal" ) );

	// Expert: the sequences and their commands.
	auto *pExpert = new QWidget;
	auto *pExpertLayout = new QVBoxLayout( pExpert );
	auto *pSequenceRow = new QHBoxLayout;
	pSequenceRow->addWidget( new QLabel( tr( "Configuration" ) ) );
	m_pSequence = new QComboBox;
	m_pSequence->setObjectName( QStringLiteral( "compileSequence" ) );
	pSequenceRow->addWidget( m_pSequence, 1 );
	auto *pNewSequence = new QPushButton( tr( "New" ) );
	pNewSequence->setObjectName( QStringLiteral( "compileNewSequence" ) );
	auto *pRemoveSequence = new QPushButton( tr( "Remove" ) );
	pRemoveSequence->setObjectName( QStringLiteral( "compileRemoveSequence" ) );
	pSequenceRow->addWidget( pNewSequence );
	pSequenceRow->addWidget( pRemoveSequence );
	pExpertLayout->addLayout( pSequenceRow );
	auto *pCommandsRow = new QHBoxLayout;
	m_pCommands = new QListWidget;
	m_pCommands->setObjectName( QStringLiteral( "compileCommands" ) );
	pCommandsRow->addWidget( m_pCommands, 1 );
	auto *pButtons = new QVBoxLayout;
	for ( const auto &[ szText, pszName, pfn ] : std::initializer_list<std::tuple<QString, const char *, void ( CMapCompileDialog::* )()>>{
			  { tr( "New" ), "compileNewCommand", &CMapCompileDialog::AddCommand },
			  { tr( "Remove" ), "compileRemoveCommand", &CMapCompileDialog::RemoveCommand } } )
	{
		auto *pButton = new QPushButton( szText );
		pButton->setObjectName( QLatin1String( pszName ) );
		connect( pButton, &QPushButton::clicked, this, pfn );
		pButtons->addWidget( pButton );
	}
	auto *pUp = new QPushButton( tr( "Move Up" ) );
	connect( pUp, &QPushButton::clicked, this, [ this ] { MoveCommand( -1 ); } );
	auto *pDown = new QPushButton( tr( "Move Down" ) );
	connect( pDown, &QPushButton::clicked, this, [ this ] { MoveCommand( 1 ); } );
	pButtons->addWidget( pUp );
	pButtons->addWidget( pDown );
	pButtons->addStretch( 1 );
	pCommandsRow->addLayout( pButtons );
	pExpertLayout->addLayout( pCommandsRow, 1 );
	auto *pCommandForm = new QFormLayout;
	m_pSpecial = new QComboBox;
	m_pSpecial->setObjectName( QStringLiteral( "compileSpecial" ) );
	m_pSpecial->addItem( tr( "Run a program" ), static_cast<int>( xv::map::COMPILE_SPECIAL_RUN ) );
	m_pSpecial->addItem( tr( "Change directory" ), static_cast<int>( xv::map::COMPILE_SPECIAL_CHANGE_DIR ) );
	m_pSpecial->addItem( tr( "Copy file" ), static_cast<int>( xv::map::COMPILE_SPECIAL_COPY ) );
	m_pSpecial->addItem( tr( "Delete file" ), static_cast<int>( xv::map::COMPILE_SPECIAL_DELETE ) );
	m_pSpecial->addItem( tr( "Rename file" ), static_cast<int>( xv::map::COMPILE_SPECIAL_RENAME ) );
	pCommandForm->addRow( tr( "Command" ), m_pSpecial );
	auto *pRunRow = new QHBoxLayout;
	m_pRun = new QLineEdit;
	m_pRun->setObjectName( QStringLiteral( "compileRun" ) );
	m_pRun->setPlaceholderText( tr( "$bsp_exe, $vis_exe, $light_exe, $game_exe or a path" ) );
	pRunRow->addWidget( m_pRun, 1 );
	auto *pCmds = new QPushButton( tr( "Cmds" ) );
	auto *pCmdsMenu = new QMenu( pCmds );
	for ( const char *pszVariable : { "$bsp_exe", "$vis_exe", "$light_exe", "$game_exe" } )
	{
		pCmdsMenu->addAction( QLatin1String( pszVariable ), this, [ this, pszVariable ] { m_pRun->setText( QLatin1String( pszVariable ) ); } );
	}
	pCmds->setMenu( pCmdsMenu );
	pRunRow->addWidget( pCmds );
	pCommandForm->addRow( tr( "Program" ), pRunRow );
	auto *pParmsRow = new QHBoxLayout;
	m_pParms = new QLineEdit;
	m_pParms->setObjectName( QStringLiteral( "compileParms" ) );
	pParmsRow->addWidget( m_pParms, 1 );
	auto *pParms = new QPushButton( tr( "Parms" ) );
	auto *pParmsMenu = new QMenu( pParms );
	for ( const char *pszVariable : { "$file", "$path", "$bspdir", "$gamedir", "$exedir" } )
	{
		pParmsMenu->addAction( QLatin1String( pszVariable ), this, [ this, pszVariable ] { m_pParms->insert( QLatin1String( pszVariable ) ); } );
	}
	pParms->setMenu( pParmsMenu );
	pParmsRow->addWidget( pParms );
	pCommandForm->addRow( tr( "Parameters" ), pParmsRow );
	auto *pEnsureRow = new QHBoxLayout;
	m_pEnsure = new QCheckBox( tr( "Ensure file exists afterwards" ) );
	m_pEnsureFile = new QLineEdit;
	m_pEnsureFile->setPlaceholderText( QStringLiteral( "$path\\$file.bsp" ) );
	pEnsureRow->addWidget( m_pEnsure );
	pEnsureRow->addWidget( m_pEnsureFile, 1 );
	pCommandForm->addRow( QString{}, pEnsureRow );
	m_pWait = new QCheckBox( tr( "Wait for it to finish (off for the game)" ) );
	pCommandForm->addRow( QString{}, m_pWait );
	pExpertLayout->addLayout( pCommandForm );
	m_pTabs->addTab( pExpert, tr( "Expert" ) );
	pLayout->addWidget( m_pTabs );

	// The process: the log, the problems, the buttons.
	m_pLog = new QPlainTextEdit;
	m_pLog->setObjectName( QStringLiteral( "compileLog" ) );
	m_pLog->setReadOnly( true );
	m_pLog->setMaximumBlockCount( 20000 );
	m_pLog->setLineWrapMode( QPlainTextEdit::NoWrap );
	pLayout->addWidget( m_pLog, 2 );
	m_pProblems = new QListWidget;
	m_pProblems->setObjectName( QStringLiteral( "compileProblems" ) );
	m_pProblems->setMaximumHeight( 110 );
	m_pProblems->setToolTip( tr( "What the compilers complained about; a double click goes to the brush or point" ) );
	pLayout->addWidget( m_pProblems );
	auto *pBottom = new QHBoxLayout;
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "compileStatus" ) );
	pBottom->addWidget( m_pStatus, 1 );
	m_pGo = new QPushButton( tr( "Go!" ) );
	m_pGo->setObjectName( QStringLiteral( "compileGo" ) );
	m_pGo->setDefault( true );
	m_pCancel = new QPushButton( tr( "Cancel" ) );
	m_pCancel->setObjectName( QStringLiteral( "compileCancel" ) );
	m_pCancel->setEnabled( false );
	auto *pClose = new QPushButton( tr( "Close" ) );
	pBottom->addWidget( m_pGo );
	pBottom->addWidget( m_pCancel );
	pBottom->addWidget( pClose );
	pLayout->addLayout( pBottom );

	connect( m_pGo, &QPushButton::clicked, this, [ this ] { Run(); } );
	connect( m_pCancel, &QPushButton::clicked, m_pRunner, &CCompileRunner::Cancel );
	connect( pClose, &QPushButton::clicked, this, &QDialog::close );
	connect( m_pRunner, &CCompileRunner::Output, this, [ this ]( const QString &szText ) {
		m_pLog->moveCursor( QTextCursor::End );
		m_pLog->insertPlainText( szText );
		m_pLog->moveCursor( QTextCursor::End );
	} );
	connect( m_pRunner, &CCompileRunner::CommandStarted, this, [ this ]( std::size_t, const QString &szShown ) { m_pStatus->setText( tr( "Running %1 (%2 s)" ).arg( szShown.section( QLatin1Char( ' ' ), 0, 0 ) ).arg( m_Clock.elapsed() / 1000 ) ); } );
	connect( m_pRunner, &CCompileRunner::Finished, this, &CMapCompileDialog::Finished );
	connect( m_pSequence, &QComboBox::currentIndexChanged, this, &CMapCompileDialog::ShowSequence );
	connect( m_pCommands, &QListWidget::currentRowChanged, this, &CMapCompileDialog::ShowCommand );
	connect( m_pCommands, &QListWidget::itemChanged, this, &CMapCompileDialog::CommandEdited );
	for ( QLineEdit *pEdit : { m_pRun, m_pParms, m_pEnsureFile } )
	{
		connect( pEdit, &QLineEdit::editingFinished, this, &CMapCompileDialog::CommandEdited );
	}
	connect( m_pSpecial, &QComboBox::currentIndexChanged, this, &CMapCompileDialog::CommandEdited );
	connect( m_pEnsure, &QCheckBox::toggled, this, &CMapCompileDialog::CommandEdited );
	connect( m_pWait, &QCheckBox::toggled, this, &CMapCompileDialog::CommandEdited );
	connect( pNewSequence, &QPushButton::clicked, this, &CMapCompileDialog::AddSequence );
	connect( pRemoveSequence, &QPushButton::clicked, this, &CMapCompileDialog::RemoveSequence );
	connect( m_pProblems, &QListWidget::itemDoubleClicked, this, [ this ] { ProblemPicked( m_pProblems->currentRow() ); } );

	// The Normal tab as it was left.
	const QStringList normal = QSettings{}.value( QLatin1String( NORMAL_SETTING ) ).toString().split( QLatin1Char( '|' ) );
	if ( normal.size() >= 6 )
	{
		m_pBsp->button( std::clamp( normal[ 0 ].toInt(), 0, 2 ) )->setChecked( true );
		m_pVis->button( std::clamp( normal[ 1 ].toInt(), 0, 2 ) )->setChecked( true );
		m_pRad->button( std::clamp( normal[ 2 ].toInt(), 0, 2 ) )->setChecked( true );
		m_pHdr->setChecked( normal[ 3 ] == QStringLiteral( "1" ) );
		m_pNoGame->setChecked( normal[ 4 ] == QStringLiteral( "1" ) );
		m_pGameParameters->setText( normal.mid( 5 ).join( QLatin1Char( '|' ) ) );
	}
	LoadSequences();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapCompileDialog::~CMapCompileDialog()
{
	QSettings{}.setValue( QLatin1String( NORMAL_SETTING ), QStringLiteral( "%1|%2|%3|%4|%5|%6" ).arg( m_pBsp->checkedId() ).arg( m_pVis->checkedId() ).arg( m_pRad->checkedId() ).arg( m_pHdr->isChecked() ? 1 : 0 ).arg( m_pNoGame->isChecked() ? 1 : 0 ).arg( m_pGameParameters->text() ) );
	SaveSequences();
	m_pRunner->Cancel();
}

//-----------------------------------------------------------------------------
// Purpose: The settings' sequences; the first time, Hammer++'s own file beside the game, else
//          Hammer's CmdSeq.wc, else the built-in list.
//-----------------------------------------------------------------------------
void CMapCompileDialog::LoadSequences()
{
	const QString szSaved = QSettings{}.value( SequencesSetting() ).toString();
	if ( !szSaved.isEmpty() )
	{
		m_Sequences = xv::map::ReadSequencesText( szSaved.toStdString() );
	}
	if ( m_Sequences.empty() )
	{
		if ( const std::optional<xv::vfs::CGameInfo> &game = m_Editor.GetDocument().GetGame() )
		{
			const QString szRoot = QString::fromStdString( game->m_BaseDir.string() );
			for ( const QString &szFile : { szRoot + QStringLiteral( "/bin/x64/hammerplusplus/hammerplusplus_sequences.cfg" ), szRoot + QStringLiteral( "/BIN/x64/hammerplusplus/hammerplusplus_sequences.cfg" ) } )
			{
				QFile file( szFile );
				if ( m_Sequences.empty() && file.open( QIODevice::ReadOnly ) )
				{
					m_Sequences = xv::map::ReadSequencesText( file.readAll().toStdString() );
				}
			}
			for ( const QString &szFile : { szRoot + QStringLiteral( "/bin/CmdSeq.wc" ), szRoot + QStringLiteral( "/BIN/CmdSeq.wc" ) } )
			{
				QFile file( szFile );
				if ( m_Sequences.empty() && file.open( QIODevice::ReadOnly ) )
				{
					const QByteArray bytes = file.readAll();
					m_Sequences = xv::map::ReadCmdSeq( std::span<const std::byte>( reinterpret_cast<const std::byte *>( bytes.constData() ), static_cast<std::size_t>( bytes.size() ) ) );
				}
			}
		}
	}
	if ( m_Sequences.empty() )
	{
		m_Sequences = xv::map::BuiltinSequences();
	}
	m_bFilling = true;
	m_pSequence->clear();
	for ( const xv::map::CompileSequence_t &sequence : m_Sequences )
	{
		m_pSequence->addItem( QString::fromStdString( sequence.m_szName ) );
	}
	m_bFilling = false;
	ShowSequence();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompileDialog::SaveSequences()
{
	QSettings{}.setValue( SequencesSetting(), QString::fromStdString( xv::map::WriteSequencesText( m_Sequences ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::CompileSequence_t *CMapCompileDialog::CurrentSequence()
{
	const int n = m_pSequence->currentIndex();
	return n >= 0 && static_cast<std::size_t>( n ) < m_Sequences.size() ? &m_Sequences[ static_cast<std::size_t>( n ) ] : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::CompileCommand_t *CMapCompileDialog::CurrentCommand()
{
	xv::map::CompileSequence_t *pSequence = CurrentSequence();
	const int n = m_pCommands->currentRow();
	return pSequence && n >= 0 && static_cast<std::size_t>( n ) < pSequence->m_Commands.size() ? &pSequence->m_Commands[ static_cast<std::size_t>( n ) ] : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The sequence's commands, each with its box ticked when enabled.
//-----------------------------------------------------------------------------
void CMapCompileDialog::ShowSequence()
{
	if ( m_bFilling )
	{
		return;
	}
	m_bFilling = true;
	m_pCommands->clear();
	if ( const xv::map::CompileSequence_t *pSequence = CurrentSequence() )
	{
		for ( const xv::map::CompileCommand_t &command : pSequence->m_Commands )
		{
			QString szText;
			switch ( command.m_nSpecial )
			{
				case xv::map::COMPILE_SPECIAL_CHANGE_DIR: szText = tr( "cd %1" ).arg( QString::fromStdString( command.m_szParms ) ); break;
				case xv::map::COMPILE_SPECIAL_COPY: szText = tr( "copy %1" ).arg( QString::fromStdString( command.m_szParms ) ); break;
				case xv::map::COMPILE_SPECIAL_DELETE: szText = tr( "delete %1" ).arg( QString::fromStdString( command.m_szParms ) ); break;
				case xv::map::COMPILE_SPECIAL_RENAME: szText = tr( "rename %1" ).arg( QString::fromStdString( command.m_szParms ) ); break;
				default: szText = QString::fromStdString( command.m_szRun + " " + command.m_szParms ); break;
			}
			auto *pItem = new QListWidgetItem( szText );
			pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable );
			pItem->setCheckState( command.m_bEnabled ? Qt::Checked : Qt::Unchecked );
			m_pCommands->addItem( pItem );
		}
	}
	m_bFilling = false;
	if ( m_pCommands->count() > 0 )
	{
		m_pCommands->setCurrentRow( 0 );
	}
	ShowCommand();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompileDialog::ShowCommand()
{
	if ( m_bFilling )
	{
		return;
	}
	m_bFilling = true;
	const xv::map::CompileCommand_t *pCommand = CurrentCommand();
	m_pSpecial->setCurrentIndex( pCommand ? std::max( 0, m_pSpecial->findData( pCommand->m_nSpecial ) ) : 0 );
	m_pRun->setText( pCommand ? QString::fromStdString( pCommand->m_szRun ) : QString{} );
	m_pParms->setText( pCommand ? QString::fromStdString( pCommand->m_szParms ) : QString{} );
	m_pEnsure->setChecked( pCommand && pCommand->m_bEnsureFile );
	m_pEnsureFile->setText( pCommand ? QString::fromStdString( pCommand->m_szEnsure ) : QString{} );
	m_pWait->setChecked( !pCommand || pCommand->m_bWait );
	for ( QWidget *pWidget : std::initializer_list<QWidget *>{ m_pSpecial, m_pRun, m_pParms, m_pEnsure, m_pEnsureFile, m_pWait } )
	{
		pWidget->setEnabled( pCommand != nullptr );
	}
	m_pRun->setEnabled( pCommand && pCommand->m_nSpecial == xv::map::COMPILE_SPECIAL_RUN );
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose: A field or the tick changed: the command takes it, and its line is redrawn.
//-----------------------------------------------------------------------------
void CMapCompileDialog::CommandEdited()
{
	xv::map::CompileCommand_t *pCommand = CurrentCommand();
	if ( m_bFilling || !pCommand )
	{
		return;
	}
	const int nRow = m_pCommands->currentRow();
	pCommand->m_nSpecial = m_pSpecial->currentData().toInt();
	pCommand->m_szRun = m_pRun->text().toStdString();
	pCommand->m_szParms = m_pParms->text().toStdString();
	pCommand->m_bEnsureFile = m_pEnsure->isChecked();
	pCommand->m_szEnsure = m_pEnsureFile->text().toStdString();
	pCommand->m_bWait = m_pWait->isChecked();
	for ( int i = 0; i < m_pCommands->count(); i++ )
	{
		CurrentSequence()->m_Commands[ static_cast<std::size_t>( i ) ].m_bEnabled = m_pCommands->item( i )->checkState() == Qt::Checked;
	}
	ShowSequence();
	m_pCommands->setCurrentRow( nRow );
	SaveSequences();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompileDialog::AddCommand()
{
	xv::map::CompileSequence_t *pSequence = CurrentSequence();
	if ( !pSequence )
	{
		return;
	}
	xv::map::CompileCommand_t command;
	command.m_szRun = "$bsp_exe";
	command.m_szParms = "-game $gamedir $path\\$file";
	const int nAt = std::clamp( m_pCommands->currentRow() + 1, 0, static_cast<int>( pSequence->m_Commands.size() ) );
	pSequence->m_Commands.insert( pSequence->m_Commands.begin() + nAt, command );
	ShowSequence();
	m_pCommands->setCurrentRow( nAt );
	SaveSequences();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompileDialog::RemoveCommand()
{
	xv::map::CompileSequence_t *pSequence = CurrentSequence();
	const int nRow = m_pCommands->currentRow();
	if ( !pSequence || nRow < 0 || static_cast<std::size_t>( nRow ) >= pSequence->m_Commands.size() )
	{
		return;
	}
	pSequence->m_Commands.erase( pSequence->m_Commands.begin() + nRow );
	ShowSequence();
	m_pCommands->setCurrentRow( std::min( nRow, m_pCommands->count() - 1 ) );
	SaveSequences();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompileDialog::MoveCommand( int nBy )
{
	xv::map::CompileSequence_t *pSequence = CurrentSequence();
	const int nRow = m_pCommands->currentRow();
	const int nTo = nRow + nBy;
	if ( !pSequence || nRow < 0 || nTo < 0 || static_cast<std::size_t>( nTo ) >= pSequence->m_Commands.size() )
	{
		return;
	}
	std::swap( pSequence->m_Commands[ static_cast<std::size_t>( nRow ) ], pSequence->m_Commands[ static_cast<std::size_t>( nTo ) ] );
	ShowSequence();
	m_pCommands->setCurrentRow( nTo );
	SaveSequences();
}

//-----------------------------------------------------------------------------
// Purpose: A copy of the current sequence under a new name.
//-----------------------------------------------------------------------------
void CMapCompileDialog::AddSequence()
{
	bool bOk = false;
	const QString szName = QInputDialog::getText( this, tr( "New Configuration" ), tr( "Name:" ), QLineEdit::Normal, tr( "My Compile" ), &bOk ).trimmed();
	if ( !bOk || szName.isEmpty() )
	{
		return;
	}
	xv::map::CompileSequence_t sequence = CurrentSequence() ? *CurrentSequence() : xv::map::BuiltinSequences().front();
	sequence.m_szName = szName.toStdString();
	m_Sequences.push_back( std::move( sequence ) );
	m_bFilling = true;
	m_pSequence->addItem( szName );
	m_bFilling = false;
	m_pSequence->setCurrentIndex( m_pSequence->count() - 1 );
	SaveSequences();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompileDialog::RemoveSequence()
{
	const int n = m_pSequence->currentIndex();
	if ( n < 0 || m_Sequences.size() <= 1 )
	{
		return;
	}
	m_Sequences.erase( m_Sequences.begin() + n );
	m_bFilling = true;
	m_pSequence->removeItem( n );
	m_bFilling = false;
	ShowSequence();
	SaveSequences();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::NormalOptions_t CMapCompileDialog::GetNormalOptions() const
{
	xv::map::NormalOptions_t options;
	options.m_nBsp = static_cast<xv::map::NormalOptions_t::Bsp_t>( std::max( 0, m_pBsp->checkedId() ) );
	options.m_nVis = static_cast<xv::map::NormalOptions_t::Vis_t>( std::max( 0, m_pVis->checkedId() ) );
	options.m_nRad = static_cast<xv::map::NormalOptions_t::Rad_t>( std::max( 0, m_pRad->checkedId() ) );
	options.m_bHdr = m_pHdr->isChecked();
	options.m_bRun = !m_pNoGame->isChecked();
	options.m_szGameParameters = m_pGameParameters->text().trimmed().toStdString();
	return options;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapCompileDialog::IsExpert() const
{
	return m_pTabs->currentIndex() == 1;
}

//-----------------------------------------------------------------------------
// Purpose: The map's own names, the game's folders and the tools as Preferences has them.
//-----------------------------------------------------------------------------
xv::map::CompileVariables_t CMapCompileDialog::GetVariables() const
{
	xv::map::CompileVariables_t variables;
	const QFileInfo map( m_Editor.GetPath() );
	variables.m_szFile = map.completeBaseName().toStdString();
	variables.m_szPath = map.absolutePath().toStdString();
	if ( const std::optional<xv::vfs::CGameInfo> &game = m_Editor.GetDocument().GetGame() )
	{
		variables.m_szGameDir = game->m_GameDir.string();
		variables.m_szExeDir = game->m_BaseDir.string();
		variables.m_szBspDir = FindBspDir( *game, QSettings{}.value( BspDirSetting() ).toString() ).toStdString();
		variables.m_szBspExe = FindSourceTool( SourceTool_t::SOURCE_TOOL_VBSP, *game, QSettings{}.value( SourceToolSetting( SourceTool_t::SOURCE_TOOL_VBSP ) ).toString() ).toStdString();
		variables.m_szVisExe = FindSourceTool( SourceTool_t::SOURCE_TOOL_VVIS, *game, QSettings{}.value( SourceToolSetting( SourceTool_t::SOURCE_TOOL_VVIS ) ).toString() ).toStdString();
		variables.m_szLightExe = FindSourceTool( SourceTool_t::SOURCE_TOOL_VRAD, *game, QSettings{}.value( SourceToolSetting( SourceTool_t::SOURCE_TOOL_VRAD ) ).toString() ).toStdString();
		variables.m_szGameExe = FindSourceTool( SourceTool_t::SOURCE_TOOL_GAME, *game, QSettings{}.value( SourceToolSetting( SourceTool_t::SOURCE_TOOL_GAME ) ).toString() ).toStdString();
	}
	return variables;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<xv::map::ResolvedCommand_t> CMapCompileDialog::GetCommands() const
{
	const xv::map::CompileVariables_t variables = GetVariables();
	if ( IsExpert() )
	{
		const int n = m_pSequence->currentIndex();
		return n >= 0 && static_cast<std::size_t>( n ) < m_Sequences.size() ? xv::map::ResolveSequence( m_Sequences[ static_cast<std::size_t>( n ) ], variables ) : std::vector<xv::map::ResolvedCommand_t>{};
	}
	return xv::map::ResolveSequence( xv::map::SequenceFromNormal( GetNormalOptions() ), variables );
}

//-----------------------------------------------------------------------------
// Purpose: Hammer saves the map first; a map without a file yet asks where. The old leak line
//          is forgotten before the new compile.
//-----------------------------------------------------------------------------
bool CMapCompileDialog::Run()
{
	if ( m_pRunner->IsRunning() )
	{
		return false;
	}
	m_pLog->clear();
	m_pProblems->clear();
	if ( !m_Editor.GetDocument().GetGame() )
	{
		m_pLog->appendPlainText( tr( "Open the game the map is for first (File > Open > Game): the compilers need its folders." ) );
		return false;
	}
	if ( !m_Editor.Save() )
	{
		m_pLog->appendPlainText( tr( "The map wasn't saved, so nothing was compiled." ) );
		return false;
	}
	const std::vector<xv::map::ResolvedCommand_t> commands = GetCommands();
	if ( commands.empty() )
	{
		m_pLog->appendPlainText( tr( "Nothing to run." ) );
		return false;
	}
	const xv::map::CompileVariables_t variables = GetVariables();
	m_Editor.GetDocument().SetPointfile( std::nullopt );
	m_Clock.start();
	m_pGo->setEnabled( false );
	m_pCancel->setEnabled( true );
	m_pStatus->setText( tr( "Starting…" ) );
	return m_pRunner->Start( commands, QString::fromStdString( variables.m_szPath ), QString::fromStdString( variables.m_szGameDir ) );
}

//-----------------------------------------------------------------------------
// Purpose: The problems listed, and the leak's line read from beside the map and shown.
//-----------------------------------------------------------------------------
void CMapCompileDialog::Finished( bool bSuccess, const xv::map::CompileReport_t &report )
{
	m_Report = report;
	m_pGo->setEnabled( true );
	m_pCancel->setEnabled( false );
	const double flSeconds = m_Clock.elapsed() / 1000.0;
	for ( const xv::map::CompileProblem_t &problem : report.m_Problems )
	{
		auto *pItem = new QListWidgetItem( QStringLiteral( "%1%2" ).arg( problem.m_szTool.empty() ? QString{} : QString::fromStdString( problem.m_szTool ) + QStringLiteral( ": " ), QString::fromStdString( problem.m_szMessage ) ) );
		pItem->setForeground( problem.m_bError ? QBrush{ QColor{ 220, 0, 0 } } : QBrush{ QColor{ 200, 140, 0 } } );
		m_pProblems->addItem( pItem );
	}
	if ( report.m_bLeaked )
	{
		const xv::map::CompileVariables_t variables = GetVariables();
		QFile file( QDir( QString::fromStdString( variables.m_szPath ) ).filePath( QString::fromStdString( variables.m_szFile ) + QStringLiteral( ".lin" ) ) );
		if ( file.open( QIODevice::ReadOnly ) )
		{
			const std::vector<xv::map::Point_t> points = xv::map::ReadPointfile( file.readAll().toStdString() );
			if ( !points.empty() )
			{
				m_Editor.GetDocument().SetPointfile( points );
			}
		}
		m_pStatus->setText( tr( "The map leaked near %1 (%2 s): the red line in the views goes through the hole" ).arg( QString::fromStdString( report.m_szLeakEntity ) ).arg( flSeconds, 0, 'f', 1 ) );
		return;
	}
	m_pStatus->setText( bSuccess ? tr( "Done in %1 s" ).arg( flSeconds, 0, 'f', 1 ) : tr( "Stopped after %1 s" ).arg( flSeconds, 0, 'f', 1 ) );
}

//-----------------------------------------------------------------------------
// Purpose: A problem that names a brush selects it; one that names a point centres the views
//          there.
//-----------------------------------------------------------------------------
void CMapCompileDialog::ProblemPicked( int nRow )
{
	if ( nRow < 0 || static_cast<std::size_t>( nRow ) >= m_Report.m_Problems.size() )
	{
		return;
	}
	const xv::map::CompileProblem_t &problem = m_Report.m_Problems[ static_cast<std::size_t>( nRow ) ];
	if ( problem.m_nBrushId )
	{
		m_Editor.GoToObjectId( *problem.m_nBrushId );
	}
	else if ( problem.m_Point )
	{
		m_Editor.CenterViewsOn( *problem.m_Point );
	}
}
