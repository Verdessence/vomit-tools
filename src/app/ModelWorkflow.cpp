//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: See ModelWorkflow.h. Source compilation is performed by user tools.
//===========================================================================//
#include "ModelWorkflow.h"
#include "ModelImport.h"
#include "ModelVmfDialog.h"
#include "QtPaths.h"
#include "bsp/Bsp.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <stdexcept>
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Produces binary-safe text artifacts for a source project.
//-----------------------------------------------------------------------------
xv::model::Bytes_t Bytes( const QByteArray &bytes )
{
	return { reinterpret_cast<const std::byte *>( bytes.data() ), reinterpret_cast<const std::byte *>( bytes.data() + bytes.size() ) };
}
//-----------------------------------------------------------------------------
// Purpose: Creates a labeled file picker whose path remains directly editable.
//-----------------------------------------------------------------------------
QLineEdit *FileRow( QFormLayout *pForm, const QString &szLabel, const QString &szFilter, QWidget *pParent )
{
	auto *pRow = new QWidget;
	auto *pLayout = new QHBoxLayout( pRow );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	auto *pEdit = new QLineEdit;
	auto *pBrowse = new QPushButton( QObject::tr( "Browse…" ) );
	pLayout->addWidget( pEdit, 1 );
	pLayout->addWidget( pBrowse );
	pForm->addRow( szLabel, pRow );
	QObject::connect( pBrowse, &QPushButton::clicked, pParent, [ pParent, pEdit, szFilter ] {const auto path=QFileDialog::getOpenFileName(pParent,QObject::tr("Select File"),pEdit->text(),szFilter);if(!path.isEmpty()){pEdit->setText(path);emit pEdit->editingFinished();} } );
	return pEdit;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Presents source and export operations separately from edits to compiled input bytes.
//-----------------------------------------------------------------------------
CModelWorkflow::CModelWorkflow( QString szPath, xv::model::Reader_t pfnRead, xv::model::CModel model, xv::model::CStudioFile source, xv::model::Clip_t clip, std::optional<xv::vfs::CGameInfo> game, QWidget *pParent )
	: QDialog( pParent ),
	  m_szPath( std::move( szPath ) ),
	  m_pfnRead( std::move( pfnRead ) ),
	  m_Model( std::move( model ) ),
	  m_Source( std::move( source ) ),
	  m_Clip( std::move( clip ) ),
	  m_Game( std::move( game ) ),
	  m_Runner( this )
{
	setObjectName( QStringLiteral( "modelWorkflow" ) );
	setWindowTitle( tr( "Model Sources, Build and Export" ) );
	resize( 820, 800 );
	auto *pRoot = new QVBoxLayout( this );
	auto *pTabs = new QTabWidget;
	pRoot->addWidget( pTabs );
	auto *pExchange = new QWidget;
	auto *pExchangeLayout = new QVBoxLayout( pExchange );
	auto *pHint = new QLabel( tr( "Exports use the current body, skin and LOD. Editable source projects reconstruct meshes and an optional clip; review the generated QC before compiling complex models." ) );
	pHint->setWordWrap( true );
	pExchangeLayout->addWidget( pHint );
	for ( const auto &[ szName, nFormat ] : std::vector<std::pair<QString, int>>{ { tr( "Export OBJ and textures…" ), 0 }, { tr( "Export weighted SMD…" ), 1 }, { tr( "Export glTF and textures…" ), 2 }, { tr( "Export trimmed / retargeted animation SMD…" ), 3 }, { tr( "Collect compiled model and dependencies…" ), 4 } } )
	{
		auto *p = new QPushButton( szName );
		pExchangeLayout->addWidget( p );
		connect( p, &QPushButton::clicked, this, [ this, nFormat ] { Export( nFormat ); } );
	}
	auto *pVmf = new QPushButton( tr( "VMF (brushes)…" ) );
	pVmf->setObjectName( QStringLiteral( "modelVmfButton" ) );
	pExchangeLayout->addWidget( pVmf );
	connect( pVmf, &QPushButton::clicked, this, [ this ] { CModelVmfDialog dialog( m_szPath, m_pfnRead, m_Model, m_Selection, this ); dialog.exec(); } );
	pExchangeLayout->addStretch();
	pTabs->addTab( pExchange, tr( "Export" ) );
	auto *pAnimation = new QWidget;
	auto *pAnimationForm = new QFormLayout( pAnimation );
	m_pAnimation = new QCheckBox( tr( "Include this clip in the source project / glTF" ) );
	pAnimationForm->addRow( m_pAnimation );
	m_pSequences = new QComboBox;
	for ( const auto &seq : m_Source.m_Sequences )
		m_pSequences->addItem( QString::fromStdString( seq.m_szName ) );
	m_pSequences->setCurrentIndex( m_Clip.m_nSequence >= 0 ? m_Clip.m_nSequence : 0 );
	pAnimationForm->addRow( tr( "Sequence" ), m_pSequences );
	m_pFirst = new QSpinBox;
	m_pLast = new QSpinBox;
	m_pFirst->setRange( 0, 1000000 );
	m_pLast->setRange( 0, 1000000 );
	m_pFps = new QDoubleSpinBox;
	m_pFps->setRange( 0.01, 1000 );
	m_pFps->setValue( 30 );
	pAnimationForm->addRow( tr( "First frame" ), m_pFirst );
	pAnimationForm->addRow( tr( "Last frame" ), m_pLast );
	pAnimationForm->addRow( tr( "Playback fps" ), m_pFps );
	m_pBaseBlend = new QCheckBox( tr( "Export the base blend only when the sequence has layers" ) );
	pAnimationForm->addRow( m_pBaseBlend );
	const auto sequence = [ this ] {const int i=m_pSequences->currentIndex();m_pFirst->setValue(0);m_pLast->setValue(0);if(i>=0&&!m_Source.m_Sequences[i].m_Animations.empty()){const int a=m_Source.m_Sequences[i].m_Animations[0];if(a>=0&&a<static_cast<int>(m_Source.m_Animations.size())){m_pLast->setValue(std::max(0,m_Source.m_Animations[a].m_nFrames-1));m_pFps->setValue(std::max(0.01f,m_Source.m_Animations[a].m_flFps));}} };
	connect( m_pSequences, &QComboBox::currentIndexChanged, this, sequence );
	sequence();
	// Retargeting is performed from decoded source bones onto this model's bind pose by name.
	auto *pRetarget = FileRow( pAnimationForm, tr( "Animation model (optional)" ), tr( "Models (*.mdl)" ), this );
	connect( pRetarget, &QLineEdit::editingFinished, this, [ this, pRetarget ] {try{if(pRetarget->text().isEmpty())return;const auto path=QFileInfo(pRetarget->text()).absoluteFilePath();const auto dir=QFileInfo(path).absolutePath();const auto reader=[this,dir](std::string_view szFile)->std::optional<std::vector<std::byte>>{QFile f(QDir(dir).filePath(QFileInfo(QString::fromUtf8(szFile.data(),szFile.size())).fileName()));if(f.open(QIODevice::ReadOnly))return Bytes(f.readAll());return m_pfnRead(szFile);};std::string error;auto file=xv::model::ReadStudio(QFileInfo(path).fileName().toStdString(),reader,&error);if(!file)throw std::runtime_error(error);m_Source=std::move(*file);m_pSequences->clear();for(const auto &seq:m_Source.m_Sequences)m_pSequences->addItem(QString::fromStdString(seq.m_szName));}catch(const std::exception &e){Log(QString::fromUtf8(e.what()));} } );
	pTabs->addTab( pAnimation, tr( "Animation" ) );
	auto *pSource = new QWidget;
	auto *pForm = new QFormLayout( pSource );
	m_pModelName = new QLineEdit( QStringLiteral( "edited/" ) + QFileInfo( m_szPath ).fileName() );
	m_pModelName->setObjectName( QStringLiteral( "sourceModelName" ) );
	pForm->addRow( tr( "Output model name" ), m_pModelName );
	m_pMesh = FileRow( pForm, tr( "New mesh (optional)" ), tr( "Meshes (*.smd *.obj *.dmx *.gltf *.glb)" ), this );
	m_pOperation = new QComboBox;
	m_pOperation->addItems( { tr( "Replace a bodygroup model" ), tr( "Append a bodygroup" ) } );
	pForm->addRow( tr( "Mesh operation" ), m_pOperation );
	m_pBodygroup = new QComboBox;
	m_pBodyModel = new QComboBox;
	std::string error;
	auto own = xv::model::ReadStudio( m_szPath.toStdString(), m_pfnRead, &error );
	if ( own )
		for ( const auto &group : own->m_Bodygroups )
			m_pBodygroup->addItem( QString::fromStdString( group.m_szName ) );
	const auto models = [ this, own ] {m_pBodyModel->clear();const int n=m_pBodygroup->currentIndex();if(own&&n>=0)for(const auto &name:own->m_Bodygroups[n].m_Models)m_pBodyModel->addItem(QString::fromStdString(name)); };
	connect( m_pBodygroup, &QComboBox::currentIndexChanged, this, models );
	models();
	pForm->addRow( tr( "Bodygroup" ), m_pBodygroup );
	pForm->addRow( tr( "Model" ), m_pBodyModel );
	m_pCollision = FileRow( pForm, tr( "Collision source (optional)" ), tr( "Meshes (*.smd *.obj *.dmx *.gltf *.glb)" ), this );
	auto *pCollisionHint = new QLabel( tr( "studiomdl generates collision hulls from this source. Use a simplified closed mesh for physics." ) );
	pCollisionHint->setWordWrap( true );
	pForm->addRow( pCollisionHint );
	auto *pExport = new QPushButton( tr( "Export Editable Source Project…" ) );
	pForm->addRow( pExport );
	connect( pExport, &QPushButton::clicked, this, &CModelWorkflow::ExportSources );
	pTabs->addTab( pSource, tr( "Source project" ) );
	auto *pBuildPage = new QWidget;
	auto *pBuildForm = new QFormLayout( pBuildPage );
	m_pGame = FileRow( pBuildForm, tr( "Game info" ), tr( "Game info (gameinfo.txt)" ), this );
	m_pGame->setObjectName( QStringLiteral( "workflowGame" ) );
	if ( m_Game )
		m_pGame->setText( ToQString( m_Game->m_File ) );
	m_pCompiler = FileRow( pBuildForm, tr( "studiomdl / StudioMDL++" ), tr( "All files (*)" ), this );
	m_pCompiler->setObjectName( QStringLiteral( "workflowCompiler" ) );
	m_pQc = FileRow( pBuildForm, tr( "Existing QC (optional)" ), tr( "QC (*.qc)" ), this );
	m_pQc->setObjectName( QStringLiteral( "workflowQc" ) );
	m_pQc->setPlaceholderText( tr( "Empty: build the Source project settings" ) );
	m_pBuild = new QPushButton( tr( "Compile in Temporary Game" ) );
	m_pBuild->setObjectName( QStringLiteral( "compileModel" ) );
	pBuildForm->addRow( m_pBuild );
	connect( m_pBuild, &QPushButton::clicked, this, &CModelWorkflow::Compile );
	m_pSaveBuild = new QPushButton( tr( "Export Compiled Files…" ) );
	m_pSaveBuild->setObjectName( QStringLiteral( "exportCompiledFiles" ) );
	m_pSaveBuild->setEnabled( false );
	pBuildForm->addRow( m_pSaveBuild );
	connect( m_pSaveBuild, &QPushButton::clicked, this, &CModelWorkflow::SaveBuild );
	m_pOpen = new QPushButton( tr( "Open Exported Model" ) );
	m_pOpen->setEnabled( false );
	pBuildForm->addRow( m_pOpen );
	connect( m_pOpen, &QPushButton::clicked, this, [ this ] {if(!m_szSaved.isEmpty())emit OpenModelRequested(m_szSaved); } );
	pTabs->addTab( pBuildPage, tr( "Compile" ) );
	auto *pPackPage = new QWidget;
	auto *pPackForm = new QFormLayout( pPackPage );
	m_pPacker = FileRow( pPackForm, tr( "bspzip / BSPZIP++" ), tr( "All files (*)" ), this );
	m_pPacker->setObjectName( QStringLiteral( "workflowPacker" ) );
	m_pMap = FileRow( pPackForm, tr( "Input map" ), tr( "Maps (*.bsp)" ), this );
	auto *pPackHint = new QLabel( tr( "Collect this model's compiled files, included models, materials and textures and pack them into a new BSP. The input map is preserved." ) );
	pPackHint->setWordWrap( true );
	pPackForm->addRow( pPackHint );
	m_pPack = new QPushButton( tr( "Pack Model into a Copy of the Map…" ) );
	pPackForm->addRow( m_pPack );
	connect( m_pPack, &QPushButton::clicked, this, &CModelWorkflow::Pack );
	pTabs->addTab( pPackPage, tr( "Pack" ) );
	connect( m_pGame, &QLineEdit::editingFinished, this, &CModelWorkflow::UpdateTools );
	connect( m_pCompiler, &QLineEdit::editingFinished, this, [ this ] { QSettings{}.setValue( SourceToolSetting( SourceTool_t::SOURCE_TOOL_STUDIOMDL ), m_pCompiler->text() ); } );
	connect( m_pPacker, &QLineEdit::editingFinished, this, [ this ] { QSettings{}.setValue( SourceToolSetting( SourceTool_t::SOURCE_TOOL_BSPZIP ), m_pPacker->text() ); } );
	UpdateTools();
	m_pLog = new QPlainTextEdit;
	m_pLog->setReadOnly( true );
	m_pLog->setObjectName( QStringLiteral( "sourceToolLog" ) );
	m_pLog->setMaximumBlockCount( 12000 );
	pRoot->addWidget( m_pLog, 1 );
	auto *pCancel = new QPushButton( tr( "Cancel Running Tool" ) );
	pRoot->addWidget( pCancel );
	connect( pCancel, &QPushButton::clicked, &m_Runner, &CSourceToolRunner::Cancel );
	connect( &m_Runner, &CSourceToolRunner::Output, this, &CModelWorkflow::Log );
	connect( &m_Runner, &CSourceToolRunner::Finished, this, &CModelWorkflow::Finished );
}
//-----------------------------------------------------------------------------
// Purpose: Stops a running compiler before destroying its temporary project files.
//-----------------------------------------------------------------------------
CModelWorkflow::~CModelWorkflow()
{
	disconnect( &m_Runner, nullptr, this, nullptr );
	m_Runner.Cancel();
}
//-----------------------------------------------------------------------------
// Purpose: Reads the currently chosen animation range and pose parameters.
//-----------------------------------------------------------------------------
xv::model::Clip_t CModelWorkflow::Clip() const
{
	auto clip = m_Clip;
	clip.m_nSequence = m_pSequences->currentIndex();
	clip.m_nFirst = m_pFirst->value();
	clip.m_nLast = m_pLast->value();
	clip.m_flFps = m_pFps->value();
	clip.m_bAllowBaseBlend = m_pBaseBlend->isChecked();
	return clip;
}
//-----------------------------------------------------------------------------
// Purpose: Resolves the game explicitly selected for this external tool job.
//-----------------------------------------------------------------------------
xv::vfs::CGameInfo CModelWorkflow::Game() const
{
	std::string error;
	const auto game = xv::vfs::CGameInfo::Load( ToPath( m_pGame->text() ), &error );
	if ( !game )
		throw std::runtime_error( error );
	return *game;
}
//-----------------------------------------------------------------------------
// Purpose: Refreshes discovered tools while honoring manual overrides and ++ replacements.
//-----------------------------------------------------------------------------
void CModelWorkflow::UpdateTools()
{
	try
	{
		const auto game = Game();
		m_pCompiler->setText( FindSourceTool( SourceTool_t::SOURCE_TOOL_STUDIOMDL, game, QSettings{}.value( SourceToolSetting( SourceTool_t::SOURCE_TOOL_STUDIOMDL ) ).toString() ) );
		m_pPacker->setText( FindSourceTool( SourceTool_t::SOURCE_TOOL_BSPZIP, game, QSettings{}.value( SourceToolSetting( SourceTool_t::SOURCE_TOOL_BSPZIP ) ).toString() ) );
	}
	catch ( const std::exception & )
	{}
}
//-----------------------------------------------------------------------------
// Purpose: Writes only bounded relative export paths using atomic per-file replacement.
//-----------------------------------------------------------------------------
bool CModelWorkflow::Write( const xv::model::Export_t &files, const QString &szFolder )
{
	for ( const auto &[ path, bytes ] : files.m_Files )
	{
		if ( !xv::model::IsExportPath( path ) )
			throw std::runtime_error( "Unsafe export path" );
		const auto target = QDir( szFolder ).filePath( QString::fromStdString( path ) );
		if ( !QDir().mkpath( QFileInfo( target ).absolutePath() ) )
			throw std::runtime_error( "Could not create export directory" );
		QSaveFile file( target );
		if ( !file.open( QIODevice::WriteOnly ) || file.write( reinterpret_cast<const char *>( bytes.data() ), bytes.size() ) != static_cast<qint64>( bytes.size() ) || !file.commit() )
			throw std::runtime_error( file.errorString().toStdString() );
	}
	for ( const auto &note : files.m_Notes )
		Log( QString::fromStdString( note ) );
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Exports exchange meshes, a decoded animation, or the complete compiled dependency set.
//-----------------------------------------------------------------------------
void CModelWorkflow::Export( int nFormat )
{
	const auto folder = QFileDialog::getExistingDirectory( this, tr( "Export Model" ) );
	if ( folder.isEmpty() )
		return;
	try
	{
		xv::model::Export_t files;
		if ( nFormat == 0 )
			files = xv::model::ExportObj( m_Model, m_pfnRead );
		else if ( nFormat == 1 )
			files.m_Files[ "model.smd" ] = Bytes( QByteArray::fromStdString( xv::model::ExportSmd( m_Model ) ) );
		else if ( nFormat == 2 )
			files = xv::model::ExportGltf( m_Model, m_pfnRead, m_pAnimation->isChecked() ? &m_Source : nullptr, Clip() );
		else if ( nFormat == 3 )
			files.m_Files[ "animation.smd" ] = Bytes( QByteArray::fromStdString( xv::model::ExportAnimationSmd( m_Source, m_Model, Clip() ) ) );
		else
			files = xv::model::CollectModelFiles( m_szPath.toStdString(), m_pfnRead );
		Write( files, folder );
		Log( tr( "Exported to %1" ).arg( folder ) );
	}
	catch ( const std::exception &e )
	{
		Log( QString::fromUtf8( e.what() ) );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Converts glTF input when needed; supported native source formats are left to studiomdl.
//-----------------------------------------------------------------------------
xv::model::Export_t CModelWorkflow::Sources()
{
	xv::model::SourceOptions_t options;
	options.m_szModelName = m_pModelName->text().trimmed().toStdString();
	options.m_nBodygroup = m_pBodygroup->currentIndex();
	options.m_nBodyModel = m_pBodyModel->currentIndex();
	options.m_bAppendBodygroup = m_pOperation->currentIndex() == 1;
	std::optional<xv::model::CModel> mesh, collision;
	const auto convert = [ & ]( QLineEdit *p, std::optional<xv::model::CModel> &model ) -> std::string {if(p->text().isEmpty())return {};const auto path=QFileInfo(p->text()).absoluteFilePath();if(!QFileInfo(path).isFile())throw std::runtime_error("Source mesh does not exist");const auto suffix=QFileInfo(path).suffix().toLower();if(suffix==u"gltf"||suffix==u"glb"){model=ImportGltfMesh(path,m_Model);return {};}if(suffix!=u"smd"&&suffix!=u"obj"&&suffix!=u"dmx")throw std::runtime_error("Choose SMD, OBJ, DMX or glTF source meshes");return QDir::fromNativeSeparators(path).toStdString(); };
	options.m_szMeshSource = convert( m_pMesh, mesh );
	options.m_szCollisionSource = convert( m_pCollision, collision );
	if ( mesh )
		options.m_pReplacement = &*mesh;
	if ( collision )
		options.m_pCollision = &*collision;
	if ( m_pAnimation->isChecked() )
	{
		options.m_pAnimationSource = &m_Source;
		options.m_Clip = Clip();
	}
	return xv::model::ExportSourceProject( m_szPath.toStdString(), m_pfnRead, options );
}
//-----------------------------------------------------------------------------
// Purpose: Produces an inspectable source project before any external compiler is run.
//-----------------------------------------------------------------------------
void CModelWorkflow::ExportSources()
{
	const auto folder = QFileDialog::getExistingDirectory( this, tr( "Export Source Project" ) );
	if ( folder.isEmpty() )
		return;
	try
	{
		Write( Sources(), folder );
		m_pQc->setText( QDir( folder ).filePath( QStringLiteral( "model.qc" ) ) );
		Log( tr( "Source project exported to %1" ).arg( folder ) );
	}
	catch ( const std::exception &e )
	{
		Log( QString::fromUtf8( e.what() ) );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Invokes the chosen compiler against a private VProject, preserving installed models.
//-----------------------------------------------------------------------------
void CModelWorkflow::Compile()
{
	if ( m_Runner.IsRunning() )
		return;
	try
	{
		const auto game = Game();
		m_pWork = std::make_unique<QTemporaryDir>();
		if ( !m_pWork->isValid() )
			throw std::runtime_error( "Could not create build workspace" );
		const auto szGame = QDir( m_pWork->path() ).filePath( QStringLiteral( "game" ) );
		QString error;
		if ( !WriteBuildGame( game, szGame, &error ) )
			throw std::runtime_error( error.toStdString() );
		QString qc = m_pQc->text();
		if ( qc.isEmpty() )
		{
			const auto project = QDir( m_pWork->path() ).filePath( QStringLiteral( "source" ) );
			Write( Sources(), project );
			qc = QDir( project ).filePath( QStringLiteral( "model.qc" ) );
		}
		if ( !QFileInfo( qc ).isFile() )
			throw std::runtime_error( "Choose an existing QC source" );
		m_BuildFiles = {};
		m_szSaved.clear();
		m_pSaveBuild->setEnabled( false );
		m_pOpen->setEnabled( false );
		m_bPacking = false;
		m_pBuild->setEnabled( false );
		m_pPack->setEnabled( false );
		m_Runner.Start( m_pCompiler->text(), { QStringLiteral( "-nop4" ), QStringLiteral( "-game" ), szGame, QFileInfo( qc ).absoluteFilePath() }, QFileInfo( qc ).absolutePath(), szGame );
	}
	catch ( const std::exception &e )
	{
		Log( QString::fromUtf8( e.what() ) );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Packs a complete compiled model dependency set into a distinct output map.
//-----------------------------------------------------------------------------
void CModelWorkflow::Pack()
{
	if ( m_Runner.IsRunning() )
		return;
	const auto output = QFileDialog::getSaveFileName( this, tr( "Save Packed Map" ), {}, tr( "BSP map (*.bsp)" ) );
	if ( output.isEmpty() )
		return;
	try
	{
		const auto game = Game();
		if ( QFileInfo( output ).absoluteFilePath() == QFileInfo( m_pMap->text() ).absoluteFilePath() )
			throw std::runtime_error( "Choose a different output map to preserve the input" );
		std::string error;
		if ( !xv::bsp::CBsp::Load( ToPath( m_pMap->text() ), &error ) )
			throw std::runtime_error( error );
		m_pWork = std::make_unique<QTemporaryDir>();
		if ( !m_pWork->isValid() )
			throw std::runtime_error( "Could not create pack workspace" );
		const auto files = xv::model::CollectModelFiles( m_szPath.toStdString(), m_pfnRead );
		if ( !files.m_Notes.empty() )
			throw std::runtime_error( "Resolve missing model dependencies before packing" );
		const auto folder = QDir( m_pWork->path() ).filePath( QStringLiteral( "files" ) );
		Write( files, folder );
		QFile list( QDir( m_pWork->path() ).filePath( QStringLiteral( "files.txt" ) ) );
		if ( !list.open( QIODevice::WriteOnly ) )
			throw std::runtime_error( "Cannot write pack list" );
		for ( const auto &[ path, data ] : files.m_Files )
		{
			Q_UNUSED( data );
			list.write( QByteArray::fromStdString( path ) + "\n" + QDir( folder ).filePath( QString::fromStdString( path ) ).toUtf8() + "\n" );
		}
		list.close();
		m_szResultBsp = output;
		m_bPacking = true;
		m_pBuild->setEnabled( false );
		m_pPack->setEnabled( false );
		m_Runner.Start( m_pPacker->text(), { QStringLiteral( "-game" ), ToQString( game.m_GameDir ), QStringLiteral( "-addlist" ), QFileInfo( m_pMap->text() ).absoluteFilePath(), list.fileName(), QDir( m_pWork->path() ).filePath( QStringLiteral( "packed.bsp" ) ) }, QFileInfo( m_pPacker->text() ).absolutePath(), ToQString( game.m_GameDir ) );
	}
	catch ( const std::exception &e )
	{
		Log( QString::fromUtf8( e.what() ) );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Requires parseable tool output, not merely a zero exit status, before exporting it.
//-----------------------------------------------------------------------------
void CModelWorkflow::Finished( bool bSuccess )
{
	m_pBuild->setEnabled( true );
	m_pPack->setEnabled( true );
	if ( !bSuccess )
		return;
	try
	{
		if ( m_bPacking )
		{
			const auto path = QDir( m_pWork->path() ).filePath( QStringLiteral( "packed.bsp" ) );
			std::string error;
			if ( !xv::bsp::CBsp::Load( ToPath( path ), &error ) )
				throw std::runtime_error( "bspzip did not produce a valid map: " + error );
			QFile input( path );
			if ( !input.open( QIODevice::ReadOnly ) )
				throw std::runtime_error( "Cannot read packed map" );
			QSaveFile output( m_szResultBsp );
			const auto bytes = input.readAll();
			if ( !output.open( QIODevice::WriteOnly ) || output.write( bytes ) != bytes.size() || !output.commit() )
				throw std::runtime_error( "Cannot save packed map" );
			Log( tr( "Packed map saved to %1" ).arg( m_szResultBsp ) );
			return;
		}
		const auto root = QDir( m_pWork->path() ).filePath( QStringLiteral( "game" ) );
		QDirIterator models( QDir( root ).filePath( QStringLiteral( "models" ) ), { QStringLiteral( "*.mdl" ) }, QDir::Files, QDirIterator::Subdirectories );
		int nModels = 0;
		while ( models.hasNext() )
		{
			const auto path = models.next();
			const auto relative = QDir( root ).relativeFilePath( path ).toStdString();
			const xv::model::Reader_t read = [ root ]( std::string_view path ) -> std::optional<std::vector<std::byte>> {if(!xv::model::IsExportPath(path))return std::nullopt;QFile f(QDir(root).filePath(QString::fromUtf8(path.data(),path.size())));return f.open(QIODevice::ReadOnly)?std::optional{Bytes(f.readAll())}:std::nullopt; };
			std::string error;
			const auto studio = xv::model::ReadStudio( relative, read, &error );
			if ( !studio || ( !studio->m_Bodygroups.empty() && !xv::model::LoadModel( relative, read, &error ) ) )
				throw std::runtime_error( "Compiler output failed validation: " + error );
			nModels++;
		}
		if ( !nModels )
			throw std::runtime_error( "Compiler exited successfully but wrote no models into the temporary game" );
		QDirIterator files( QDir( root ).filePath( QStringLiteral( "models" ) ), QDir::Files, QDirIterator::Subdirectories );
		while ( files.hasNext() )
		{
			const auto path = files.next();
			QFile f( path );
			if ( !f.open( QIODevice::ReadOnly ) )
				throw std::runtime_error( "Cannot read compiler output" );
			m_BuildFiles.m_Files[ QDir( root ).relativeFilePath( path ).toStdString() ] = Bytes( f.readAll() );
		}
		m_pSaveBuild->setEnabled( true );
		Log( tr( "Validated %1 compiled model(s). Export the files to keep them." ).arg( nModels ) );
	}
	catch ( const std::exception &e )
	{
		Log( QString::fromUtf8( e.what() ) );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Saves validated compiler output to a user-chosen folder before opening a model tab.
//-----------------------------------------------------------------------------
void CModelWorkflow::SaveBuild()
{
	const auto folder = QFileDialog::getExistingDirectory( this, tr( "Export Compiled Models" ) );
	if ( folder.isEmpty() )
		return;
	try
	{
		Write( m_BuildFiles, folder );
		for ( const auto &[ path, bytes ] : m_BuildFiles.m_Files )
		{
			Q_UNUSED( bytes );
			if ( path.ends_with( ".mdl" ) )
			{
				m_szSaved = QDir( folder ).filePath( QString::fromStdString( path ) );
				break;
			}
		}
		m_pOpen->setEnabled( !m_szSaved.isEmpty() );
		Log( tr( "Compiled models exported to %1" ).arg( folder ) );
	}
	catch ( const std::exception &e )
	{
		Log( QString::fromUtf8( e.what() ) );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Shows progress and actionable errors without hiding the previous tool output.
//-----------------------------------------------------------------------------
void CModelWorkflow::Log( const QString &szText )
{
	if ( m_pLog )
		m_pLog->appendPlainText( szText );
}
