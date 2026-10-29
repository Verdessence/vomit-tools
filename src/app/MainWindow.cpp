//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MainWindow.h.
//
//===========================================================================//
#include "MainWindow.h"

#include <set>

#include <QApplication>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QCheckBox>
#include <QMessageBox>
#include <QMenu>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QPointer>
#include <QSysInfo>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeView>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include "DebugLog.h"
#include "EditorTab.h"
#include "AboutDialog.h"
#include "BuildInfo.h"
#include "FileActions.h"
#include "FileDialogs.h"
#include "FileViews.h"
#include "FolderBrowser.h"
#include "ImageFiles.h"
#include "InterfaceMode.h"
#include "KvCard.h"
#include "MaterialView.h"
#include "MenuPicker.h"
#include "ParticleDocument.h"
#include "ParticleSearch.h"
#include "Preferences.h"
#include "map/MapEditor.h"
#include "map/Write.h"
#include "PreferencesDialog.h"
#include "PremiumDialog.h"
#include "QtPaths.h"
#include "SkyDocument.h"
#include "SkyEditor.h"
#include "SoundDocument.h"
#include "SoundEditor.h"
#include "DetailEditor.h"
#include "DetailPreview.h"
#include "ModelBrowser.h"
#include "ModelDocument.h"
#include "ModelEditor.h"
#include "ModelWorkflow.h"
#include "MaterialForm.h"
#include "SoundPlayer.h"
#include "BatchConvertDialog.h"
#include "MakeMaterialDialog.h"
#include "MaterialEditor.h"
#include "TextureDocument.h"
#include "TextureEditor.h"
#include "VfsModel.h"
#include "VpkEditor.h"
#include "bsp/Bsp.h"
#include "dmx/Dmx.h"
#include "material/Parameters.h"
#include "particles/Particles.h"
#include "sky/Sky.h"
#include "sound/SoundLint.h"
#include "model/Model.h"
#include "vfs/Package.h"

namespace fs = std::filesystem;
using xv::sound::FileKind_t;

namespace
{

constexpr auto LAST_GAME_KEY = "lastGameInfo";
constexpr auto LAST_FOLDER_KEY = "lastFolder";
constexpr auto SESSION_KEY = "lastSession";
constexpr auto SESSION_CURRENT_KEY = "lastSessionTab";
constexpr auto SESSION_SIDE_KEY = "lastSessionSide";
constexpr auto RECENT_KEY = "recentPaths";
constexpr auto VOLUME_KEY = "volume";
constexpr auto SKY_EXPORT_KEY = "skyExportFolder";
constexpr int RECENT_LIMIT = 12;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString WriteFolderKey( const fs::path &gameInfo )
{
	const QByteArray hash = QCryptographicHash::hash( ToQString( gameInfo ).toUtf8(), QCryptographicHash::Md5 ).toHex();
	return QStringLiteral( "writeFolders/" ) + QString::fromLatin1( hash );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *Placeholder( const QString &szText )
{
	auto *pLabel = new QLabel( szText );
	pLabel->setAlignment( Qt::AlignCenter );
	pLabel->setWordWrap( true );
	return pLabel;
}

//-----------------------------------------------------------------------------
// Purpose: Where Steam may be installed on this platform.
//-----------------------------------------------------------------------------
std::vector<fs::path> SteamRoots()
{
	std::vector<fs::path> roots;
#if defined( Q_OS_WIN )
	const QSettings registry( QStringLiteral( "HKEY_CURRENT_USER\\Software\\Valve\\Steam" ), QSettings::NativeFormat );
	if ( const QString szPath = registry.value( QStringLiteral( "SteamPath" ) ).toString(); !szPath.isEmpty() )
	{
		roots.push_back( ToPath( szPath ) );
	}
	roots.push_back( ToPath( QStringLiteral( "C:/Program Files (x86)/Steam" ) ) );
#elif defined( Q_OS_MACOS )
	roots.push_back( ToPath( QDir::homePath() + QStringLiteral( "/Library/Application Support/Steam" ) ) );
#else
	roots.push_back( ToPath( QDir::homePath() + QStringLiteral( "/.local/share/Steam" ) ) );
	roots.push_back( ToPath( QDir::homePath() + QStringLiteral( "/.steam/steam" ) ) );
	roots.push_back( ToPath( QDir::homePath() + QStringLiteral( "/.var/app/com.valvesoftware.Steam/.local/share/Steam" ) ) );
#endif
	std::vector<fs::path> existing;
	for ( const fs::path &root : roots )
	{
		std::error_code ec;
		if ( fs::is_directory( root / "steamapps", ec ) )
		{
			existing.push_back( root );
		}
	}
	return existing;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString KindName( FileKind_t nKind )
{
	return nKind == FileKind_t::FILE_KIND_SOUNDSCAPE ? QObject::tr( "soundscape" ) : QObject::tr( "soundscript" );
}

//-----------------------------------------------------------------------------
// Purpose: The gameinfo.txt of the game a file or folder belongs to: in it, above it, or (for a
//          game's
//          install folder) one level below it.
//-----------------------------------------------------------------------------
QString FindGameInfo( const QString &szPath )
{
	const QFileInfo info{ szPath };
	QDir dir = info.isDir() ? QDir{ info.absoluteFilePath() } : info.absoluteDir();
	if ( info.isDir() )
	{
		QStringList found;
		for ( const QFileInfo &child : dir.entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot ) )
		{
			if ( QFileInfo::exists( child.absoluteFilePath() + QStringLiteral( "/gameinfo.txt" ) ) )
			{
				found << child.absoluteFilePath() + QStringLiteral( "/gameinfo.txt" );
			}
		}
		if ( found.size() == 1 && !QFileInfo::exists( dir.filePath( QStringLiteral( "gameinfo.txt" ) ) ) )
		{
			return found.front();
		}
	}
	for ( int nDepth = 0; nDepth < 8; nDepth++ )
	{
		if ( QFileInfo::exists( dir.filePath( QStringLiteral( "gameinfo.txt" ) ) ) )
		{
			return dir.filePath( QStringLiteral( "gameinfo.txt" ) );
		}
		if ( !dir.cdUp() )
		{
			break;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<QByteArray> ReadDiskFile( const QString &szPath )
{
	QFile file{ szPath };
	if ( !file.open( QIODevice::ReadOnly ) )
	{
		return std::nullopt;
	}
	return file.readAll();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const std::byte> AsBytes( const QByteArray &data )
{
	return { reinterpret_cast<const std::byte *>( data.constData() ), static_cast<std::size_t>( data.size() ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsParticlePath( const QString &szPath )
{
	return szPath.endsWith( u".pcf", Qt::CaseInsensitive );
}

//-----------------------------------------------------------------------------
// Purpose: A particle file read from bytes; nullopt for other DMX files and unreadable data.
//-----------------------------------------------------------------------------
std::optional<xv::dmx::Document_t> ReadParticleFile( std::span<const std::byte> bytes )
{
	auto read = xv::dmx::Read( bytes );
	if ( !read.m_Document || !xv::particles::IsParticleFile( *read.m_Document ) )
	{
		return std::nullopt;
	}
	return std::move( read.m_Document );
}

//-----------------------------------------------------------------------------
// Purpose: The sky a face file on disk belongs to, when it clearly is one: it sits in a skybox
//          folder, or
//          at least half of its faces are beside it (a lone "brick_up.png" is just an image).
//-----------------------------------------------------------------------------
std::optional<xv::sky::CSkyFiles> SkyFilesFor( const QString &szPath )
{
	auto files = xv::sky::FindSkyFiles( ToPath( szPath ) );
	if ( files && ( files->m_bInSkyboxFolder || files->Count() >= 3 ) )
	{
		return files;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString SkyOrigin( const xv::sky::CSkyFiles &files )
{
	for ( const auto &face : files.m_Faces )
	{
		if ( !face.empty() )
		{
			return QDir::cleanPath( ToQString( face.parent_path() ) ) + u'/' + QString::fromStdString( files.m_szName );
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The sky a skybox material or texture belongs to: materials/skybox/sky_dustrt.vmt is
//          sky_dust.
//-----------------------------------------------------------------------------
QString SkyNameFor( const QString &szGamePath )
{
	const QString szPath = szGamePath.toLower();
	if ( !szPath.startsWith( u"materials/skybox/" ) || !( szPath.endsWith( u".vmt" ) || szPath.endsWith( u".vtf" ) ) )
	{
		return {};
	}
	const QString szBase = QFileInfo{ szPath }.completeBaseName();
	if ( szBase.size() <= 2 || !xv::sky::FaceFromFileName( szBase.toStdString() ) )
	{
		return {};
	}
	return szBase.left( szBase.size() - 2 );
}

//-----------------------------------------------------------------------------
// Purpose: The .mdl a model's other file (.vvd, .vtx, .phy, .ani) belongs to: the first guess
//          pfnFind finds, as it gives it back. Empty for other files or when none is there.
//-----------------------------------------------------------------------------
QString ModelForCompanion( const QString &szPath, const std::function<QString( const QString & )> &pfnFind )
{
	for ( const std::string &szMdl : xv::model::ModelPathsFor( szPath.toStdString() ) )
	{
		if ( QString szFound = pfnFind( QString::fromStdString( szMdl ) ); !szFound.isEmpty() )
		{
			return szFound;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The same, beside a file on disk; the .mdl's case may differ from the file's.
//-----------------------------------------------------------------------------
QString DiskModelForCompanion( const QString &szPath )
{
	return ModelForCompanion( szPath, []( const QString &szMdl ) {
		const QString szFound = ToQString( xv::vfs::FindCaseInsensitive( ToPath( szMdl ) ) );
		return QFileInfo( szFound ).isFile() ? szFound : QString{};
	} );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Tests pass Output::Offline so nothing is played aloud.
//-----------------------------------------------------------------------------
CMainWindow::CMainWindow( QWidget *pParent, xv::audio::CEngine::Output_t nAudio )
	: QMainWindow( pParent ),
	  m_pFileSystem( std::make_unique<xv::vfs::CFileSystem>() ),
	  m_pModel( new CVfsModel( this ) ),
	  m_pFilter( new QSortFilterProxyModel( this ) ),
	  m_pUndoGroup( new QUndoGroup( this ) )
{
	setWindowTitle( QStringLiteral( "vomit-tools" ) );
	resize( 1400, 900 );
	setAcceptDrops( true );
	m_pPlayer = new CSoundPlayer( *m_pFileSystem, this, nAudio );

	m_pFilter->setSourceModel( m_pModel );
	m_pFilter->setRecursiveFilteringEnabled( true );
	m_pFilter->setFilterCaseSensitivity( Qt::CaseInsensitive );
	m_pFilter->setFilterKeyColumn( CVfsModel::COLUMN_NAME_COLUMN );

	auto *pSearch = new QLineEdit;
	pSearch->setObjectName( QStringLiteral( "gameFileSearch" ) );
	pSearch->setPlaceholderText( tr( "Filter files…" ) );
	pSearch->setClearButtonEnabled( true );
	connect( pSearch, &QLineEdit::textChanged, m_pFilter, &QSortFilterProxyModel::setFilterFixedString );

	m_pTree = new QTreeView;
	m_pTree->setModel( m_pFilter );
	m_pTree->setUniformRowHeights( true );
	// Every column can be sized; the widths are kept between runs.
	m_pTree->header()->setSectionResizeMode( QHeaderView::Interactive );
	m_pTree->header()->setStretchLastSection( false );
	m_pTree->setColumnWidth( CVfsModel::COLUMN_NAME_COLUMN, 220 );
	m_pTree->header()->restoreState( QSettings{}.value( QStringLiteral( "gameBrowserColumns" ) ).toByteArray() );
	connect( m_pTree->header(), &QHeaderView::sectionResized, this, [ this ] { QSettings{}.setValue( QStringLiteral( "gameBrowserColumns" ), m_pTree->header()->saveState() ); } );
	m_pTree->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pTree, &QTreeView::customContextMenuRequested, this, [ this ]( const QPoint &at ) {
		const QModelIndex proxyIndex = m_pTree->indexAt( at );
		if ( !proxyIndex.isValid() )
		{
			return;
		}
		const QModelIndex index = m_pFilter->mapToSource( proxyIndex );
		const QString szPath = index.siblingAtColumn( 0 ).data( CVfsModel::ROLE_PATH_ROLE ).toString();
		const bool bFolder = index.siblingAtColumn( 0 ).data( CVfsModel::ROLE_IS_DIRECTORY_ROLE ).toBool();
		const std::string szGamePath = xv::vfs::NormalizePath( szPath.toStdString() );
		fs::path disk;
		if ( const xv::vfs::CMount *pMount = bFolder ? nullptr : m_pFileSystem->Resolve( szGamePath ) )
		{
			disk = pMount->DiskPath( szGamePath ).value_or( fs::path{} );
		}
		// A loose file gets the file's own menu; one in a VPK, what can be done with it here.
		if ( !disk.empty() )
		{
			ShowFileMenu( m_pTree, m_pTree->viewport()->mapToGlobal( at ), ToQString( disk ), [ this, proxyIndex ] { emit m_pTree->activated( proxyIndex ); } );
			return;
		}
		QMenu menu( this );
		if ( !bFolder )
		{
			menu.addAction( tr( "Open" ), this, [ this, proxyIndex ] { emit m_pTree->activated( proxyIndex ); } );
		}
		menu.addAction( tr( "Copy Game Path" ), this, [ szPath ] { QGuiApplication::clipboard()->setText( szPath ); } );
		if ( !bFolder )
		{
			menu.addAction( tr( "Save a Copy…" ), this, [ this, szPath, szGamePath ] {
				const QString szFile = AskSaveFile( this, tr( "Save a Copy" ), QFileInfo{ szPath }.fileName() );
				const auto bytes = szFile.isEmpty() ? std::nullopt : m_pFileSystem->Read( szGamePath );
				QSaveFile out( szFile );
				if ( bytes && ( !out.open( QIODevice::WriteOnly ) || out.write( reinterpret_cast<const char *>( bytes->data() ), static_cast<qint64>( bytes->size() ) ) != static_cast<qint64>( bytes->size() ) || !out.commit() ) )
				{
					QMessageBox::warning( this, tr( "Save a Copy" ), tr( "Couldn't write %1." ).arg( szFile ) );
				}
			} );
		}
		menu.exec( m_pTree->viewport()->mapToGlobal( at ) );
	} );
	m_pTree->setToolTip( tr( "The game's files as the engine sees them. Double-click a soundscape, soundscript, sky material or particle file to edit it" ) );
	connect( m_pTree->selectionModel(), &QItemSelectionModel::currentChanged, this, &CMainWindow::PreviewFile );
	connect( m_pTree, &QTreeView::activated, this, [ this ]( const QModelIndex &proxyIndex ) {
		const QString szPath = m_pFilter->mapToSource( proxyIndex ).data( CVfsModel::ROLE_PATH_ROLE ).toString();
		if ( xv::sound::FileKindForPath( szPath.toStdString() ) != FileKind_t::FILE_KIND_UNKNOWN )
		{
			OpenSoundFile( szPath );
		}
		else if ( const QString szSky = SkyNameFor( szPath ); !szSky.isEmpty() )
		{
			OpenSky( szSky );
		}
		else if ( szPath.endsWith( u".vbsp", Qt::CaseInsensitive ) )
		{
			OpenDetailFile( szPath );
		}
		else if ( szPath.endsWith( u".mdl", Qt::CaseInsensitive ) )
		{
			OpenModelFile( szPath );
		}
		else if ( szPath.endsWith( u".vmf", Qt::CaseInsensitive ) )
		{
			OpenMapFile( szPath );
		}
		else if ( const QString szMdl = GameModelForCompanion( szPath ); !szMdl.isEmpty() )
		{
			OpenModelFile( szMdl );
		}
		else if ( IsParticlePath( szPath ) )
		{
			OpenParticleFile( szPath );
		}
		else if ( const QString szDisk = DiskPathForGame( szPath ); !szDisk.isEmpty() && xv::vfs::CPackage::IsPackageFile( ToPath( szDisk ) ) )
		{
			OpenPackage( szDisk );
		}
	} );

	m_pGameBrowser = new QWidget;
	auto *pBrowserLayout = new QVBoxLayout( m_pGameBrowser );
	pBrowserLayout->setContentsMargins( 0, 0, 0, 0 );
	pBrowserLayout->addWidget( pSearch );
	pBrowserLayout->addWidget( m_pTree );

	m_pFolders = new CFolderBrowser;
	connect( m_pFolders, &CFolderBrowser::OpenFolderRequested, this, &CMainWindow::ChooseFolder );
	connect( m_pFolders, &CFolderBrowser::FileSelected, this, &CMainWindow::PreviewDiskFile );
	connect( m_pFolders, &CFolderBrowser::FileActivated, this, &CMainWindow::OpenDiskFile );

	m_pSides = new QTabWidget;
	m_pSides->setDocumentMode( true );
	m_pSides->addTab( m_pGameBrowser, tr( "Game" ) );
	m_pSides->addTab( m_pFolders, tr( "Folder" ) );

	m_pPreviewHost = new QWidget;
	auto *pPreviewLayout = new QVBoxLayout( m_pPreviewHost );
	pPreviewLayout->setContentsMargins( 0, 0, 0, 0 );
	pPreviewLayout->addWidget( Placeholder( tr( "Open a game, a folder or a file from the File menu, or drop files and folders here.\n\n"
												"Soundscapes (soundscapes_*.txt) and soundscripts (game_sounds_*.txt) open in the sound editor, skies "
												"in the skybox editor (File → New Skybox) and particle files (.pcf) in the particle editor; sounds, "
												"textures and other files show here." ) ) );

	m_pTabs = new QTabWidget;
	m_pTabs->setDocumentMode( true );
	m_pTabs->setTabsClosable( true );
	m_pTabs->addTab( m_pPreviewHost, tr( "Preview" ) );
	m_pTabs->tabBar()->setTabButton( 0, QTabBar::RightSide, nullptr );
	m_pTabs->tabBar()->setTabButton( 0, QTabBar::LeftSide, nullptr );
	connect( m_pTabs, &QTabWidget::tabCloseRequested, this, &CMainWindow::CloseTab );
	connect( m_pTabs, &QTabWidget::currentChanged, this, [ this ] {
		CDetailEditor *pDetails = GetCurrentDetails();
		CSoundEditor *pEditor = GetCurrentEditor();
		CSkyEditor *pSky = GetCurrentSky();
		CParticleEditor *pParticles = GetCurrentParticles();
		CTextureEditor *pTexture = GetCurrentTexture();
		m_pUndoGroup->setActiveStack( pDetails ? pDetails->GetDocument()->GetUndoStack() : pEditor ? pEditor->GetDocument()->GetUndoStack()
				: pSky                                                                             ? pSky->GetDocument()->GetUndoStack()
				: pParticles                                                                       ? pParticles->GetDocument()->GetUndoStack()
				: pTexture                                                                         ? pTexture->GetDocument()->GetUndoStack()
				: qobject_cast<CModelEditor *>( m_pTabs->currentWidget() )                         ? qobject_cast<CModelEditor *>( m_pTabs->currentWidget() )->GetDocument()->GetUndoStack()
				: AsEditorTab( m_pTabs->currentWidget() )                                          ? AsEditorTab( m_pTabs->currentWidget() )->GetUndoStack()
																								   : nullptr );
		UpdateTitle();
		UpdateMapMenu();
	} );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( m_pSides );
	pSplitter->addWidget( m_pTabs );
	pSplitter->setStretchFactor( 1, 3 );
	pSplitter->setSizes( { 320, 1080 } );
	setCentralWidget( pSplitter );

	m_pMounts = new QTableWidget( 0, 4 );
	m_pMounts->setHorizontalHeaderLabels( { tr( "Status" ), tr( "Search path" ), tr( "Path IDs" ), tr( "Mounted from" ) } );
	m_pMounts->horizontalHeader()->setStretchLastSection( true );
	m_pMounts->verticalHeader()->setVisible( false );
	m_pMounts->setEditTriggers( QAbstractItemView::NoEditTriggers );
	m_pMounts->setToolTip( tr( "Double-click a VPK to open it in its own tab" ) );
	connect( m_pMounts, &QTableWidget::cellDoubleClicked, this, [ this ]( int nRow ) {
		const auto &records = m_pFileSystem->GetRecords();
		if ( nRow >= 0 && nRow < static_cast<int>( records.size() ) && xv::vfs::CPackage::IsPackageFile( records[ static_cast<std::size_t>( nRow ) ].m_Location ) )
		{
			OpenPackage( ToQString( records[ static_cast<std::size_t>( nRow ) ].m_Location ) );
		}
	} );
	auto *pMountsDock = new QDockWidget( tr( "Search Paths" ), this );
	pMountsDock->setObjectName( QStringLiteral( "searchPathsDock" ) );
	pMountsDock->setWidget( m_pMounts );
	addDockWidget( Qt::BottomDockWidgetArea, pMountsDock );
	pMountsDock->hide();

	BuildPlaybackBar();
	m_pGameLabel = new QLabel;
	statusBar()->addPermanentWidget( m_pGameLabel );
	UpdateGameLabel();
	statusBar()->addPermanentWidget( BuildFooter() );

	BuildMenus( pMountsDock->toggleViewAction() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMainWindow::~CMainWindow() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::BuildMenus( QAction *pSearchPathsToggle )
{
	auto *pFile = menuBar()->addMenu( tr( "&File" ) );
	auto *pNew = pFile->addMenu( tr( "&New" ) );
	pNew->setObjectName( QStringLiteral( "fileNewMenu" ) );
	pNew->addAction( tr( "&Map (VMF)" ), this, [ this ] { NewMap(); } );
	pNew->addAction( tr( "&Particle File…" ), this, &CMainWindow::NewParticleFile );
	pNew->addAction( tr( "S&kybox" ), this, [ this ] { NewSky(); } );
	pNew->addAction( tr( "&Detail File…" ), this, [ this ] { NewDetailFile(); } );
	pNew->addAction( tr( "&Soundscape File…" ), this, [ this ] { NewSoundFile( FileKind_t::FILE_KIND_SOUNDSCAPE ); } );
	pNew->addAction( tr( "Sound&script File…" ), this, [ this ] { NewSoundFile( FileKind_t::FILE_KIND_SOUNDSCRIPT ); } );
	pNew->addSeparator();
	pNew->addAction( tr( "&VTF from Pictures…" ), this, &CMainWindow::ChooseNewTexture );
	pNew->addAction( tr( "Con&vert a Folder of Textures…" ), this, &CMainWindow::ChooseBatchConvert );
	// Archives and compiled maps open through File… too: its chooser lists them.
	auto *pOpen = pFile->addMenu( tr( "&Open" ) );
	pOpen->setObjectName( QStringLiteral( "fileOpenMenu" ) );
	pOpen->addAction( tr( "&File…" ), QKeySequence::Open, this, &CMainWindow::ChooseFiles );
	pOpen->addAction( tr( "F&older…" ), QKeySequence{ Qt::CTRL | Qt::SHIFT | Qt::Key_O }, this, &CMainWindow::ChooseFolder );
	auto *pGames = pOpen->addMenu( tr( "&Game" ) );
	connect( pGames, &QMenu::aboutToShow, this, [ this, pGames ] { FillGameMenu( pGames ); } );
	auto *pRecent = pOpen->addMenu( tr( "&Recent" ) );
	connect( pRecent, &QMenu::aboutToShow, this, [ this, pRecent ] { FillRecentMenu( pRecent ); } );
	pFile->addSeparator();
	// For a sky, saving is exporting its VTFs and VMTs.
	pFile->addAction( tr( "&Save" ), QKeySequence::Save, this, [ this ] {
		if ( CDetailEditor *pDetails = GetCurrentDetails() )
		{
			Save( pDetails );
		}
		else if ( CSoundEditor *pEditor = GetCurrentEditor() )
		{
			Save( pEditor );
		}
		else if ( CSkyEditor *pSky = GetCurrentSky() )
		{
			ExportSky( pSky, false );
		}
		else if ( CParticleEditor *pParticles = GetCurrentParticles() )
		{
			Save( pParticles );
		}
		else if ( CTextureEditor *pTexture = GetCurrentTexture() )
		{
			Save( pTexture );
		}
		else if ( CMaterialEditor *pMaterial = GetCurrentMaterial() )
		{
			Save( pMaterial );
		}
		else if ( auto *pPackage = qobject_cast<CVpkEditor *>( m_pTabs->currentWidget() ) )
		{
			pPackage->Save(); // a map's Stripper config
		}
		else if ( auto *pModel = qobject_cast<CModelEditor *>( m_pTabs->currentWidget() ) )
		{
			Save( pModel );
		}
		else if ( IEditorTab *pTab = AsEditorTab( m_pTabs->currentWidget() ) )
		{
			pTab->Save();
		}
	} );
	pFile->addAction( tr( "Save &As…" ), QKeySequence::SaveAs, this, [ this ] {
		if ( CDetailEditor *pDetails = GetCurrentDetails() )
		{
			SaveAs( pDetails );
		}
		else if ( CSoundEditor *pEditor = GetCurrentEditor() )
		{
			SaveAs( pEditor );
		}
		else if ( CSkyEditor *pSky = GetCurrentSky() )
		{
			ExportSky( pSky, true );
		}
		else if ( CParticleEditor *pParticles = GetCurrentParticles() )
		{
			SaveAs( pParticles );
		}
		else if ( CTextureEditor *pTexture = GetCurrentTexture() )
		{
			SaveAs( pTexture );
		}
		else if ( CMaterialEditor *pMaterial = GetCurrentMaterial() )
		{
			SaveAs( pMaterial );
		}
		else if ( auto *pModel = qobject_cast<CModelEditor *>( m_pTabs->currentWidget() ) )
		{
			SaveAs( pModel );
		}
		else if ( IEditorTab *pTab = AsEditorTab( m_pTabs->currentWidget() ) )
		{
			pTab->SaveAs();
		}
	} );
	pFile->addAction( tr( "&Close Tab" ), QKeySequence::Close, this, [ this ] {
		if ( GetCurrentDetails() || GetCurrentEditor() || GetCurrentSky() || GetCurrentParticles() || GetCurrentTexture() || GetCurrentMaterial() || qobject_cast<CModelEditor *>( m_pTabs->currentWidget() ) || qobject_cast<CVpkEditor *>( m_pTabs->currentWidget() ) || AsEditorTab( m_pTabs->currentWidget() ) )
		{
			CloseTab( m_pTabs->currentIndex() );
		}
	} );
	pFile->addSeparator();
	pFile->addAction( tr( "&Quit" ), QKeySequence::Quit, this, &QWidget::close );

	auto *pEdit = menuBar()->addMenu( tr( "&Edit" ) );
	QAction *pUndo = m_pUndoGroup->createUndoAction( this, tr( "&Undo" ) );
	pUndo->setShortcut( QKeySequence::Undo );
	QAction *pRedo = m_pUndoGroup->createRedoAction( this, tr( "&Redo" ) );
	pRedo->setShortcut( QKeySequence::Redo );
	pEdit->addAction( pUndo );
	pEdit->addAction( pRedo );
	pEdit->addSeparator();
	pEdit->addAction( tr( "&Find Particle System…" ), QKeySequence{ Qt::CTRL | Qt::SHIFT | Qt::Key_F }, this, [ this ] { FindParticleSystem(); } );
	pEdit->addSeparator();
	// The platform's shortcut where it has one (macOS), Ctrl+, elsewhere.
	const QKeySequence preferencesKey = QKeySequence::keyBindings( QKeySequence::Preferences ).isEmpty() ? QKeySequence{ Qt::CTRL | Qt::Key_Comma } : QKeySequence{ QKeySequence::Preferences };
	QAction *pPreferences = pEdit->addAction( tr( "&Preferences…" ), preferencesKey, this, &CMainWindow::ShowPreferences );
	pPreferences->setObjectName( QStringLiteral( "preferencesAction" ) );
	pPreferences->setMenuRole( QAction::PreferencesRole );

	auto *pGame = menuBar()->addMenu( tr( "&Game" ) );
	pGame->addAction( tr( "&Reload Files" ), QKeySequence::Refresh, this, &CMainWindow::Remount );
	pGame->addAction( tr( "Set &Write Folder…" ), this, &CMainWindow::ChooseWriteFolder );
	pGame->addAction( tr( "&Open Write Folder" ), this, [ this ] {
		if ( m_Game )
		{
			std::error_code ec;
			fs::create_directories( GetWriteFolder(), ec );
			QDesktopServices::openUrl( QUrl::fromLocalFile( ToQString( GetWriteFolder() ) ) );
		}
	} );
	pGame->addSeparator();
	QAction *pModelBrowser = pGame->addAction( tr( "Browse M&odels" ), QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_M ), this, &CMainWindow::OpenModelBrowser );
	pModelBrowser->setObjectName( QStringLiteral( "modelBrowserAction" ) );
	auto *pSkies = pGame->addMenu( tr( "Open &Sky" ) );
	pSkies->setObjectName( QStringLiteral( "gameSkyMenu" ) );
	connect( pSkies, &QMenu::aboutToShow, this, [ this, pSkies ] { FillSkyMenu( pSkies ); } );
	auto *pManifests = pGame->addMenu( tr( "Generate &Manifest" ) );
	pManifests->addAction( tr( "&Soundscapes…" ), this, [ this ] { GenerateManifest( CManifestDialog::Format_t::FORMAT_SOUNDSCAPES ); } );
	pManifests->addAction( tr( "Sound&scripts…" ), this, [ this ] { GenerateManifest( CManifestDialog::Format_t::FORMAT_SOUNDSCRIPTS ); } );
	pManifests->addAction( tr( "&Particles…" ), this, [ this ] { GenerateManifest( CManifestDialog::Format_t::FORMAT_PARTICLES ); } );
	pManifests->addAction( tr( "A &Map's Particles…" ), this, &CMainWindow::ChooseMapParticles );
	pGame->addSeparator();
	pGame->addAction( pSearchPathsToggle );

	// Hammer's Map menu, for the map tabs only.
	m_pMapMenu = menuBar()->addMenu( tr( "&Map" ) );
	m_pMapMenu->setObjectName( QStringLiteral( "mapMenu" ) );
	m_pMapMenu->addAction( tr( "Map &Properties…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->ShowMapProperties();
		}
	} );
	m_pMapMenu->addAction( tr( "&Go to Brush…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_GO_TO_BRUSH, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "&Transform…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_TRANSFORM, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "Flip Hori&zontal" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_FLIP_HORIZONTAL, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "Flip &Vertical" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_FLIP_VERTICAL, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "Replace &Textures…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_REPLACE_TEXTURES, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "&Find Entities…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_FIND_ENTITIES, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "&Entity Report…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_ENTITY_REPORT, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "Check for &Problems…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_CHECK_PROBLEMS, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "Edit Cordon &Bounds" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_TOOL_CORDON, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "Toggle Cordon &State" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_TOGGLE_CORDON, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "Paste &Special…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_PASTE_SPECIAL, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addSeparator();
	m_pMapMenu->addAction( tr( "Make &Hollow…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_HOLLOW, pMap->GetActiveView() );
		}
	} );
	m_pMapMenu->addAction( tr( "&Carve" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunAction( MapAction_t::MAP_ACTION_CARVE, pMap->GetActiveView() );
		}
	} );
	auto *pInstancing = m_pMapMenu->addMenu( tr( "Instancing" ) );
	pInstancing->addAction( tr( "Edit Instance" ), this, [ this ] { if ( auto *pMap = GetCurrentMap() ) pMap->EditInstance(); } )->setObjectName( QStringLiteral( "mapEditInstance" ) );
	pInstancing->addAction( tr( "Collapse Selection" ), this, [ this ] { if ( auto *pMap = GetCurrentMap() ) pMap->CollapseInstances( false ); } )->setObjectName( QStringLiteral( "mapCollapseInstances" ) );
	pInstancing->addAction( tr( "Collapse All" ), this, [ this ] { if ( auto *pMap = GetCurrentMap() ) pMap->CollapseInstances( true ); } )->setObjectName( QStringLiteral( "mapCollapseAllInstances" ) );
	m_pMapMenu->addSeparator();
	QAction *pRunMap = m_pMapMenu->addAction( tr( "&Run Map…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->RunMap();
		}
	} );
	pRunMap->setObjectName( QStringLiteral( "mapRunMapAction" ) );
	pRunMap->setToolTip( tr( "Compile the map with vbsp, vvis and vrad and run the game on it (F9 in a view)" ) );
	m_pMapMenu->addAction( tr( "&Load Pointfile…" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->LoadPointfile();
		}
	} );
	m_pMapMenu->addAction( tr( "&Unload Pointfile" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->UnloadPointfile();
		}
	} );
	m_pMapMenu->addAction( tr( "Go to Lea&k" ), this, [ this ] {
		if ( CMapEditor *pMap = GetCurrentMap() )
		{
			pMap->GoToLeak();
		}
	} );
	m_pMapMenu->menuAction()->setVisible( false );

	// Simple or regular names, everywhere and remembered.
	auto *pTools = menuBar()->addMenu( tr( "&Tools" ) );
	auto *pCreatePrefab = pTools->addAction( tr( "Create Prefab..." ), this, [ this ] { if ( auto *pMap = GetCurrentMap() ) pMap->CreatePrefab(); } );
	pCreatePrefab->setObjectName( QStringLiteral( "mapCreatePrefabAction" ) );
	connect( pTools, &QMenu::aboutToShow, this, [ this, pCreatePrefab ] { pCreatePrefab->setEnabled( GetCurrentMap() && !GetCurrentMap()->GetDocument().GetSelection().empty() ); } );

	auto *pView = menuBar()->addMenu( tr( "&View" ) );
	QAction *pSimple = pView->addAction( tr( "&Simple Interface" ) );
	pSimple->setObjectName( QStringLiteral( "simpleInterface" ) );
	pSimple->setCheckable( true );
	pSimple->setChecked( CInterfaceMode::Instance().IsSimple() );
	pSimple->setToolTip( tr( "Plain names with what each setting does, and advanced settings tucked away. "
							 "Off: the files' own names and every setting. Either way the files are edited the same" ) );
	connect( pSimple, &QAction::toggled, this, []( bool bOn ) { CInterfaceMode::Instance().SetSimple( bOn ); } );
	connect( &CInterfaceMode::Instance(), &CInterfaceMode::Changed, pSimple, &QAction::setChecked );

	auto *pHelp = menuBar()->addMenu( tr( "&Help" ) );
	pHelp->addAction( tr( "&About vomit-tools" ), this, [ this ] { CAboutDialog{ this }.exec(); } );
	pHelp->addAction( tr( "About &Premium…" ), this, [ this ] { CPremiumDialog{ this }.exec(); } );
	pHelp->addAction( tr( "About &Qt" ), qApp, &QApplication::aboutQt );
}

//-----------------------------------------------------------------------------
// Purpose: "VOMIT TOOLS - FREE EDITION - <version> - <OS> - MADE IN THE PHILIPPINES", bottom
//          right.
//-----------------------------------------------------------------------------
QLabel *CMainWindow::BuildFooter()
{
	auto *pFooter = new QLabel( QStringLiteral( "VOMIT TOOLS - FREE EDITION - %1 - %2 - MADE IN THE PHILIPPINES" )
			.arg( QString::fromUtf8( buildinfo::version ), QSysInfo::prettyProductName().toUpper() ) );
	pFooter->setObjectName( QStringLiteral( "footer" ) );
	pFooter->setToolTip( CAboutDialog::BuildDetails() );
	QFont font = pFooter->font();
	font.setPointSizeF( font.pointSizeF() * 0.85 );
	font.setLetterSpacing( QFont::PercentageSpacing, 105 );
	pFooter->setFont( font );
	pFooter->setForegroundRole( QPalette::PlaceholderText );
	pFooter->setContentsMargins( 12, 0, 4, 0 );
	return pFooter;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::BuildPlaybackBar()
{
	auto *pStop = new QToolButton;
	pStop->setText( QStringLiteral( "■" ) );
	pStop->setToolTip( tr( "Stop all sounds" ) );
	pStop->setAutoRaise( true );
	pStop->setEnabled( false );
	connect( pStop, &QToolButton::clicked, m_pPlayer, &CSoundPlayer::Stop );

	auto *pVolume = new QSlider( Qt::Horizontal );
	pVolume->setRange( 0, 100 );
	pVolume->setFixedWidth( 90 );
	pVolume->setToolTip( tr( "Preview volume" ) );
	pVolume->setValue( QSettings{}.value( VOLUME_KEY, 80 ).toInt() );
	m_pPlayer->SetVolume( static_cast<float>( pVolume->value() ) / 100.0f );
	connect( pVolume, &QSlider::valueChanged, this, [ this ]( int nValue ) {
		m_pPlayer->SetVolume( static_cast<float>( nValue ) / 100.0f );
		QSettings{}.setValue( VOLUME_KEY, nValue );
	} );

	m_pNowPlaying = new QLabel;
	m_pNowPlaying->setFixedWidth( 360 );

	auto *pBar = new QWidget;
	auto *pLayout = new QHBoxLayout( pBar );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( pStop );
	pLayout->addWidget( pVolume );
	pLayout->addWidget( m_pNowPlaying, 1 );
	// Permanent, so status messages show beside it instead of on top of it.
	statusBar()->addPermanentWidget( pBar );

	connect( m_pPlayer, &CSoundPlayer::Started, this, [ this, pStop ]( const QString &szDescription ) {
		pStop->setEnabled( true );
		const QString szText = QStringLiteral( "♪ " ) + szDescription;
		m_pNowPlaying->setText( m_pNowPlaying->fontMetrics().elidedText( szText, Qt::ElideMiddle, m_pNowPlaying->width() ) );
		m_pNowPlaying->setToolTip( szDescription );
	} );
	connect( m_pPlayer, &CSoundPlayer::Stopped, this, [ this, pStop ] {
		pStop->setEnabled( false );
		m_pNowPlaying->clear();
	} );
	connect( m_pPlayer, &CSoundPlayer::Failed, this, [ this ]( const QString &szMessage ) {
		statusBar()->showMessage( szMessage, 6000 );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::FillGameMenu( QMenu *pMenu )
{
	ClearMenu( pMenu );
	if ( !m_SteamGames )
	{
		std::vector<fs::path> libraries;
		for ( const fs::path &root : SteamRoots() )
		{
			for ( const fs::path &library : xv::vfs::SteamLibraries( root ) )
			{
				if ( std::ranges::find( libraries, library ) == libraries.end() )
				{
					libraries.push_back( library );
				}
			}
		}
		m_SteamGames = xv::vfs::FindSourceGames( libraries );
	}
	if ( m_SteamGames->empty() )
	{
		pMenu->addAction( tr( "No Source games found in Steam libraries" ) )->setEnabled( false );
	}
	for ( const auto &found : *m_SteamGames )
	{
		const QString szPath = ToQString( found.m_GameInfo );
		QAction *pAction = pMenu->addAction( QStringLiteral( "%1  (%2)" ).arg( QString::fromStdString( found.m_szName ), ToQString( found.m_GameInfo.parent_path().filename() ) ),
			this, [ this, szPath ] { OpenGame( szPath ); } );
		pAction->setToolTip( szPath );
	}
	pMenu->setToolTipsVisible( true );
	pMenu->addSeparator();
	pMenu->addAction( tr( "&Browse for gameinfo.txt…" ), this, &CMainWindow::ChooseGameInfo );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::FillRecentMenu( QMenu *pMenu )
{
	ClearMenu( pMenu );
	const QStringList recent = QSettings{}.value( RECENT_KEY ).toStringList();
	if ( recent.isEmpty() )
	{
		pMenu->addAction( tr( "Nothing yet" ) )->setEnabled( false );
		return;
	}
	for ( const QString &szPath : recent )
	{
		const QFileInfo info{ szPath };
		const QString szLabel = info.fileName().compare( u"gameinfo.txt", Qt::CaseInsensitive ) == 0
			? tr( "%1 (game)" ).arg( QDir::toNativeSeparators( info.absolutePath() ) )
			: QDir::toNativeSeparators( szPath );
		QAction *pAction = pMenu->addAction( szLabel, this, [ this, szPath ] { OpenPath( szPath ); } );
		pAction->setEnabled( info.exists() );
	}
	pMenu->addSeparator();
	pMenu->addAction( tr( "Clear" ), this, [] { QSettings{}.remove( RECENT_KEY ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::FillSkyMenu( QMenu *pMenu )
{
	ClearMenu( pMenu );
	if ( !m_Game )
	{
		pMenu->addAction( tr( "Open a game first" ) )->setEnabled( false );
		return;
	}
	// A game can have hundreds of skies: a searchable list, not a menu that runs off the screen.
	std::vector<PickerItem_t> items;
	for ( const std::string &szName : xv::sky::SkyNames( *m_pFileSystem ) )
	{
		const QString szSky = QString::fromStdString( szName );
		items.push_back( { szSky, {}, [ this, szSky ] { OpenSky( szSky ); } } );
	}
	FillPicker( pMenu, std::move( items ), tr( "This game has no skies" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::AddRecent( const QString &szPath )
{
	QSettings settings;
	QStringList recent = settings.value( RECENT_KEY ).toStringList();
	const QString szClean = QDir::cleanPath( QFileInfo{ szPath }.absoluteFilePath() );
	recent.removeAll( szClean );
	recent.prepend( szClean );
	while ( recent.size() > RECENT_LIMIT )
	{
		recent.removeLast();
	}
	settings.setValue( RECENT_KEY, recent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::ChooseGameInfo()
{
	const QString szStart = QSettings{}.value( LAST_GAME_KEY ).toString();
	const QString szFile = AskOpenFile( this, tr( "Open gameinfo.txt" ), szStart, tr( "Game info (gameinfo.txt);;All files (*)" ) );
	if ( !szFile.isEmpty() )
	{
		OpenGame( szFile );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::ChooseFiles()
{
	const QString szStart = LastFolderOr( m_pFolders->GetFolder().isEmpty() && m_Game ? ToQString( m_Game->m_GameDir ) : m_pFolders->GetFolder() );
	const QStringList files = AskOpenFiles( this, tr( "Open File" ), szStart,
		tr( "Soundscapes and soundscripts (*.txt);;Detail files (*.vbsp);;Sounds (*.wav *.mp3);;Textures (*.vtf);;Models (*.mdl *.vvd *.vtx *.phy *.ani);;Sky faces and textures (*.vmt *.vtf *.png *.tga *.jpg *.jpeg *.bmp *.psd *.hdr);;Particles (*.pcf *.dmx);;"
			"KeyValues (*.txt *.vmt *.vdf *.res);;Hammer maps (*.vmf);;Archives and maps (*.vpk *.bsp *.gcf *.gma *.pak *.wad *.xzp *.zip);;Game info (gameinfo.txt);;All files (*)" ) );
	OpenPaths( files );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::ChooseFolder()
{
	const QString szStart = LastFolderOr( !m_pFolders->GetFolder().isEmpty() ? m_pFolders->GetFolder() : m_Game ? ToQString( m_Game->m_GameDir )
																												: QDir::homePath() );
	const QString szFolder = AskFolder( this, tr( "Open Folder" ), szStart );
	if ( !szFolder.isEmpty() )
	{
		OpenFolder( szFolder );
	}
}

//-----------------------------------------------------------------------------
// Purpose: An archive in a tab of its own, or the tab it's already in. A numbered VPK part opens
//          its _dir file.
//-----------------------------------------------------------------------------
CVpkEditor *CMainWindow::OpenPackage( const QString &szPath )
{
	DebugLog() << "opening archive" << szPath;
	std::string szError;
	auto pPackage = xv::vfs::CPackage::Open( ToPath( szPath ), &szError );
	if ( !pPackage )
	{
		QMessageBox::warning( this, tr( "Cannot open" ), QString::fromStdString( szError ) );
		return nullptr;
	}
	std::error_code ec;
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		auto *pOpen = qobject_cast<CVpkEditor *>( m_pTabs->widget( i ) );
		if ( pOpen && fs::equivalent( pOpen->GetPackage().GetFile(), pPackage->GetFile(), ec ) )
		{
			m_pTabs->setCurrentWidget( pOpen );
			return pOpen;
		}
	}
	// The game the archive belongs to supplies what it lacks, like the textures its materials name.
	AutoMountFor( szPath );
	AddRecent( ToQString( pPackage->GetFile() ) );
	const xv::material::Reader_t pfnGame = [ this ]( std::string_view szGamePath ) { return m_Game ? m_pFileSystem->Read( szGamePath ) : std::nullopt; };
	auto *pEditor = new CVpkEditor( std::move( pPackage ), m_pPlayer, pfnGame );
	connect( pEditor, &CVpkEditor::StatusMessage, this, [ this ]( const QString &szText ) { statusBar()->showMessage( szText, 5000 ); } );
	pEditor->SetCanOpen( []( const QString &szFile ) {
		return szFile.endsWith( u".mdl", Qt::CaseInsensitive ) || !xv::model::ModelPathsFor( szFile.toStdString() ).empty() || szFile.endsWith( u".vbsp", Qt::CaseInsensitive ) || szFile.endsWith( u".vtf", Qt::CaseInsensitive ) || szFile.endsWith( u".vmt", Qt::CaseInsensitive ) || IsParticlePath( szFile ) ||
			xv::sound::FileKindForPath( xv::vfs::NormalizePath( szFile.toStdString() ) ) != FileKind_t::FILE_KIND_UNKNOWN;
	} );
	connect( pEditor, &CVpkEditor::OpenRequested, this, [ this, pEditor ]( const QString &szFile ) { OpenArchiveFile( pEditor, szFile ); } );
	connect( pEditor, &CVpkEditor::ModifiedChanged, this, [ this, pEditor ]( bool bModified ) {
		m_pTabs->setTabText( m_pTabs->indexOf( pEditor ), ( bModified ? QStringLiteral( "*" ) : QString{} ) + pEditor->GetFileName() );
		UpdateTitle();
	} );
	connect( pEditor, &CVpkEditor::MapParticlesRequested, this, [ this, pEditor ] {
		GenerateManifest( CManifestDialog::Format_t::FORMAT_PARTICLES, ToQString( pEditor->GetPackage().GetFile().stem() ), pEditor->GetPackage().GetBsp(), &pEditor->GetPackage() );
	} );
	const int nIndex = m_pTabs->addTab( pEditor, pEditor->GetFileName() );
	m_pTabs->setTabToolTip( nIndex, ToQString( pEditor->GetPackage().GetFile() ) );
	m_pTabs->setCurrentIndex( nIndex );
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose: A map's Stripper config with unsaved changes: save it, drop them, or keep the tab.
//-----------------------------------------------------------------------------
bool CMainWindow::ConfirmClose( CVpkEditor *pEditor )
{
	if ( !pEditor->IsModified() )
	{
		return true;
	}
	m_pTabs->setCurrentWidget( pEditor );
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved Stripper Config" ), tr( "Save the changes to %1's Stripper config?" ).arg( pEditor->GetFileName() ),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	return nAnswer == QMessageBox::Discard || ( nAnswer == QMessageBox::Save && pEditor->Save() );
}

//-----------------------------------------------------------------------------
// Purpose: The files that could be listed: the game's soundscapes, soundscripts or particle files.
//          For a map's particles, the files defining the systems its info_particle_systems use
//          (and what those spawn) come ticked, unless particles_manifest.txt loads them already;
//          files packed in the map come ticked too, since only a manifest loads them.
//-----------------------------------------------------------------------------
CManifestDialog *CMainWindow::MakeManifestDialog( CManifestDialog::Format_t nFormat, const QString &szMap, const xv::bsp::CBsp *pMap, const xv::vfs::CPackage *pPacked )
{
	using Format_t = CManifestDialog::Format_t;
	if ( !m_Game )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "Manifests belong to a game; open the game first." ) );
		return nullptr;
	}
	std::string szManifest;
	std::vector<CManifestDialog::File_t> files;
	if ( nFormat == Format_t::FORMAT_PARTICLES )
	{
		szManifest = szMap.isEmpty() ? std::string( xv::particles::MANIFEST_PATH ) : xv::particles::MapManifestPath( szMap.toStdString() );
		for ( const std::string &szPath : m_pFileSystem->Find( "particles", ".pcf", true ) )
		{
			files.push_back( { QString::fromStdString( szPath ), true, false, {} } );
		}
	}
	else
	{
		const FileKind_t nKind = nFormat == Format_t::FORMAT_SOUNDSCAPES ? FileKind_t::FILE_KIND_SOUNDSCAPE : FileKind_t::FILE_KIND_SOUNDSCRIPT;
		szManifest = std::string( xv::sound::ManifestPathFor( nKind ) );
		for ( const std::string &szPath : m_pFileSystem->Find( "scripts", ".txt" ) )
		{
			if ( xv::sound::FileKindForPath( szPath ) == nKind )
			{
				files.push_back( { QString::fromStdString( szPath ), true, false, {} } );
			}
		}
	}

	QStringList notes;
	if ( !szMap.isEmpty() && ( pMap || pPacked ) )
	{
		std::vector<xv::particles::SystemSummary_t> summaries;
		// Packed files first: the map's own versions of a system win.
		for ( const auto &entry : pPacked ? pPacked->GetEntries() : std::vector<xv::vfs::PackageEntry_t>{} )
		{
			const std::string szPath = xv::vfs::NormalizePath( entry.m_szPath );
			if ( !szPath.starts_with( "particles/" ) || !szPath.ends_with( ".pcf" ) )
			{
				continue;
			}
			if ( const auto bytes = pPacked->Read( entry ); bytes )
			{
				if ( const auto document = ReadParticleFile( *bytes ) )
				{
					auto packed = xv::particles::Summarize( *document, szPath );
					summaries.insert( summaries.end(), packed.begin(), packed.end() );
				}
			}
			auto it = std::ranges::find_if( files, [ &szPath ]( const auto &file ) { return xv::vfs::NormalizePath( file.m_szPath.toStdString() ) == szPath; } );
			if ( it == files.end() )
			{
				it = files.insert( files.end(), { QString::fromStdString( szPath ), true, false, {} } );
			}
			it->m_bSuggested = true;
			it->m_szWhy = tr( "packed in the map" );
		}
		const auto &game = ParticleGame();
		summaries.insert( summaries.end(), game.m_Systems.begin(), game.m_Systems.end() );
		std::vector<std::string> used;
		for ( const auto &entity : pMap ? pMap->GetEntities() : std::vector<xv::bsp::Entity_t>{} )
		{
			if ( xv::kv1::EqualsIgnoreCase( entity.Get( "classname" ), "info_particle_system" ) && !entity.Get( "effect_name" ).empty() )
			{
				used.emplace_back( entity.Get( "effect_name" ) );
			}
		}
		const auto needed = xv::particles::NeededFiles( summaries, used );
		const std::vector<std::string> global = game.m_Manifest.value_or( std::vector<std::string>{} );
		for ( const auto &[ file, systems ] : needed.m_Files )
		{
			auto it = std::ranges::find_if( files, [ &file ]( const auto &candidate ) { return xv::vfs::NormalizePath( candidate.m_szPath.toStdString() ) == xv::vfs::NormalizePath( file ); } );
			if ( it == files.end() )
			{
				it = files.insert( files.end(), { QString::fromStdString( file ), true, false, {} } );
			}
			QStringList names;
			for ( const auto &szSystem : systems )
			{
				names << QString::fromStdString( szSystem );
			}
			const bool bGlobal = std::ranges::find( global, xv::vfs::NormalizePath( file ) ) != global.end();
			const QString szUses = tr( "the map uses %1" ).arg( names.join( QStringLiteral( ", " ) ) );
			it->m_szWhy = it->m_szWhy.isEmpty() ? szUses : it->m_szWhy + QStringLiteral( "; " ) + szUses;
			if ( bGlobal )
			{
				it->m_szWhy += tr( " (particles_manifest.txt loads it already)" );
			}
			it->m_bSuggested = it->m_bSuggested || !bGlobal;
		}
		if ( !needed.m_Missing.empty() )
		{
			QStringList missing;
			for ( const auto &szName : needed.m_Missing )
			{
				missing << QString::fromStdString( szName );
			}
			notes << tr( "No particle file defines: %1" ).arg( missing.join( QStringLiteral( ", " ) ) );
		}
		if ( used.empty() )
		{
			notes << tr( "The map has no info_particle_system with an effect_name." );
		}
	}
	auto *pDialog = new CManifestDialog( nFormat, QString::fromStdString( szManifest ), m_pFileSystem->ReadText( szManifest ).value_or( std::string{} ), std::move( files ), this );
	pDialog->setProperty( "manifestPath", QString::fromStdString( szManifest ) );
	pDialog->SetNotes( notes.join( u'\n' ) );
	return pDialog;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::GenerateManifest( CManifestDialog::Format_t nFormat, const QString &szMap, const xv::bsp::CBsp *pMap, const xv::vfs::CPackage *pPacked )
{
	std::unique_ptr<CManifestDialog> pDialog( MakeManifestDialog( nFormat, szMap, pMap, pPacked ) );
	if ( !pDialog || pDialog->exec() != QDialog::Accepted )
	{
		return;
	}
	const std::string szPath = pDialog->property( "manifestPath" ).toString().toStdString();
	if ( const auto target = WriteGameFile( szPath, pDialog->GetText(), tr( "Manifest" ) ) )
	{
		m_ParticleGame.reset(); // the manifest it lists is cached there
		FilesWritten( { *target } );
		statusBar()->showMessage( tr( "Wrote %1" ).arg( ToQString( *target ) ), 8000 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The game's maps are offered; any name will do. A map the game has suggests what it needs.
//-----------------------------------------------------------------------------
void CMainWindow::ChooseMapParticles()
{
	if ( !m_Game )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "Manifests belong to a game; open the game first." ) );
		return;
	}
	QStringList maps;
	for ( const std::string &szPath : m_pFileSystem->Find( "maps", ".bsp" ) )
	{
		maps << QFileInfo{ QString::fromStdString( szPath ) }.completeBaseName();
	}
	maps.removeDuplicates();
	maps.sort( Qt::CaseInsensitive );
	bool bOk = false;
	const QString szMap = QInputDialog::getItem( this, tr( "A Map's Particles" ), tr( "Make maps/<map>_particles.txt for:" ), maps, 0, true, &bOk ).trimmed();
	if ( !bOk || szMap.isEmpty() )
	{
		return;
	}
	if ( !xv::sky::IsValidName( szMap.toStdString() ) )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "\"%1\" isn't a map name." ).arg( szMap ) );
		return;
	}
	const std::string szBsp = "maps/" + szMap.toStdString() + ".bsp";
	const QString szDisk = DiskPathForGame( QString::fromStdString( szBsp ) );
	std::unique_ptr<xv::vfs::CPackage> pPackage = szDisk.isEmpty() ? nullptr : xv::vfs::CPackage::Open( ToPath( szDisk ) );
	GenerateManifest( CManifestDialog::Format_t::FORMAT_PARTICLES, szMap, pPackage ? pPackage->GetBsp() : nullptr, pPackage.get() );
}

//-----------------------------------------------------------------------------
// Purpose: A file from an archive tab in its editor, as a copy that has never been saved: Save
//          asks where, and the archive is left alone. Each open makes a new copy, since the
//          game's own file of the same path may be open too.
//-----------------------------------------------------------------------------
QWidget *CMainWindow::OpenArchiveFile( CVpkEditor *pArchive, const QString &szPath )
{
	// A model's other file opens the model it belongs to, from the archive or else the game.
	if ( const QString szMdl = ModelForCompanion( szPath, [ this, pArchive ]( const QString &szCandidate ) {
			 const std::string szGamePath = xv::vfs::NormalizePath( szCandidate.toStdString() );
			 return pArchive->GetPackage().Find( szGamePath ) || ( m_Game && m_pFileSystem->Resolve( szGamePath ) ) ? QString::fromStdString( szGamePath ) : QString{};
		 } );
		!szMdl.isEmpty() )
	{
		return OpenArchiveFile( pArchive, szMdl );
	}
	const auto bytes = pArchive->GetPackage().Read( szPath.toStdString() );
	if ( !bytes )
	{
		QMessageBox::warning( this, tr( "Cannot open" ), tr( "Could not read %1 from %2." ).arg( szPath, pArchive->GetFileName() ) );
		return nullptr;
	}
	if ( szPath.endsWith( u".mdl", Qt::CaseInsensitive ) )
	{
		const QPointer<CVpkEditor> pSource = pArchive;
		return AddModelEditor( szPath, [ this, pSource ]( std::string_view szFile ) -> std::optional<std::vector<std::byte>> {if(pSource)return pSource->GetReader()(szFile);return m_Game?m_pFileSystem->Read(szFile):std::nullopt; } );
	}
	const std::string szGamePath = xv::vfs::NormalizePath( szPath.toStdString() );
	const QString szName = QString::fromStdString( szGamePath );
	const std::string szText( reinterpret_cast<const char *>( bytes->data() ), bytes->size() );
	if ( szName.endsWith( u".vbsp" ) )
	{
		auto *pDocument = new CDetailDocument( szName, xv::kv1::Parse( szText ) );
		pDocument->MarkUnsaved();
		return AddDetailEditor( pDocument );
	}
	if ( const FileKind_t nKind = xv::sound::FileKindForPath( szGamePath ); nKind != FileKind_t::FILE_KIND_UNKNOWN )
	{
		auto *pDocument = new CSoundDocument( szName, nKind, xv::kv1::Parse( szText ) );
		pDocument->MarkUnsaved();
		return AddEditor( pDocument );
	}
	if ( auto particles = IsParticlePath( szName ) ? ReadParticleFile( *bytes ) : std::nullopt )
	{
		auto *pDocument = new CParticleDocument( szName, std::move( *particles ) );
		pDocument->MarkUnsaved();
		return AddParticleEditor( pDocument );
	}
	if ( szName.endsWith( u".vtf" ) )
	{
		auto *pDocument = new CTextureDocument( szName, std::vector<std::byte>( *bytes ) );
		if ( pDocument->IsValid() )
		{
			pDocument->MarkUnsaved();
			return AddTextureEditor( pDocument, false );
		}
		delete pDocument;
	}
	if ( szName.endsWith( u".vmt" ) )
	{
		// Its textures come from the archive while that's open, then from the game.
		const QPointer<CVpkEditor> pSource = pArchive;
		const xv::material::Reader_t pfnRead = [ this, pSource ]( std::string_view szFile ) -> std::optional<std::vector<std::byte>> {
			if ( pSource )
			{
				return pSource->GetReader()( szFile );
			}
			return m_Game ? m_pFileSystem->Read( szFile ) : std::nullopt;
		};
		auto *pEditor = new CMaterialEditor( szName, QString::fromStdString( szText ), pfnRead );
		pEditor->MarkUnsaved();
		SetUpMaterial( pEditor );
		return AddMaterialEditor( pEditor );
	}
	QMessageBox::warning( this, tr( "Cannot open" ), tr( "%1 has no editor in this version." ).arg( szPath ) );
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Opens whatever is at path: a gameinfo.txt mounts its game, a folder opens in the folder
//          browser, a soundscape or soundscript opens in an editor, anything else in the preview.
//-----------------------------------------------------------------------------
void CMainWindow::OpenPath( const QString &szPath )
{
	const QFileInfo info{ szPath };
	if ( !info.exists() )
	{
		QMessageBox::warning( this, tr( "Not found" ), tr( "%1 does not exist." ).arg( QDir::toNativeSeparators( szPath ) ) );
		return;
	}
	if ( info.isDir() )
	{
		OpenFolder( szPath );
	}
	else if ( info.fileName().compare( u"gameinfo.txt", Qt::CaseInsensitive ) == 0 )
	{
		OpenGame( szPath );
	}
	else
	{
		OpenDiskFile( szPath );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Opens several paths; the faces of one sky among them open that sky once.
//-----------------------------------------------------------------------------
void CMainWindow::OpenPaths( const QStringList &paths )
{
	QSet<QString> skies;
	for ( const QString &szPath : paths )
	{
		if ( const auto files = QFileInfo{ szPath }.isFile() ? SkyFilesFor( szPath ) : std::nullopt )
		{
			if ( !skies.contains( SkyOrigin( *files ) ) )
			{
				skies.insert( SkyOrigin( *files ) );
				AddRecent( szPath );
				OpenSkyFiles( *files );
			}
			continue;
		}
		OpenPath( szPath );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Files opened from disk still get the game's context (sound lookups, lint, manifests)
//          when they
//          belong to a game and none is mounted yet.
//-----------------------------------------------------------------------------
void CMainWindow::AutoMountFor( const QString &szPath )
{
	if ( m_Game )
	{
		return;
	}
	const QString szGameInfo = FindGameInfo( szPath );
	if ( !szGameInfo.isEmpty() && OpenGame( szGameInfo ) )
	{
		statusBar()->showMessage( tr( "Mounted %1 from %2" ).arg( QString::fromStdString( m_Game->m_szGame ), QDir::toNativeSeparators( szGameInfo ) ), 8000 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::OpenFolder( const QString &szFolder )
{
	m_pFolders->SetFolder( szFolder );
	m_pSides->setCurrentWidget( m_pFolders );
	QSettings{}.setValue( LAST_FOLDER_KEY, m_pFolders->GetFolder() );
	AddRecent( szFolder );
	AutoMountFor( szFolder );
}

//-----------------------------------------------------------------------------
// Purpose: Mounts the game whose gameinfo.txt is given. Returns false (and says why) on failure.
//-----------------------------------------------------------------------------
bool CMainWindow::OpenGame( const QString &szGameInfoPath )
{
	std::string szError;
	auto info = xv::vfs::CGameInfo::Load( ToPath( szGameInfoPath ), &szError );
	if ( !info )
	{
		QMessageBox::warning( this, tr( "Cannot open game" ), tr( "%1\n\n%2" ).arg( szGameInfoPath, QString::fromStdString( szError ) ) );
		return false;
	}
	// Files opened from a VPK belong to the game they came from; files on disk stay open.
	if ( m_Game && m_Game->m_File != info->m_File )
	{
		for ( int i = m_pTabs->count() - 1; i >= 0; i-- )
		{
			auto *pDetails = qobject_cast<CDetailEditor *>( m_pTabs->widget( i ) );
			if ( pDetails && pDetails->GetDocument()->GetDiskPath().empty() && !pDetails->GetDocument()->IsNew() && !CloseTab( i ) )
			{
				return false;
			}
			auto *pEditor = qobject_cast<CSoundEditor *>( m_pTabs->widget( i ) );
			if ( pEditor && pEditor->GetDocument()->GetDiskPath().empty() && !pEditor->GetDocument()->IsNew() && !CloseTab( i ) )
			{
				return false;
			}
			auto *pParticles = qobject_cast<CParticleEditor *>( m_pTabs->widget( i ) );
			if ( pParticles && pParticles->GetDocument()->GetDiskPath().empty() && !pParticles->GetDocument()->IsNew() && !CloseTab( i ) )
			{
				return false;
			}
			IEditorTab *pTab = AsEditorTab( m_pTabs->widget( i ) );
			if ( pTab && pTab->IsFromGame() && !CloseTab( i ) )
			{
				return false;
			}
		}
	}
	DebugLog() << "opening the game" << szGameInfoPath;
	m_Game = std::move( info );
	QSettings{}.setValue( LAST_GAME_KEY, szGameInfoPath );
	AddRecent( szGameInfoPath );
	Remount();
	m_pSides->setCurrentWidget( m_pGameBrowser );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::Remount()
{
	if ( !m_Game )
	{
		return;
	}
	const QString szSelected = m_pFilter->mapToSource( m_pTree->currentIndex() ).data( CVfsModel::ROLE_PATH_ROLE ).toString();

	QApplication::setOverrideCursor( Qt::WaitCursor );
	DebugLog() << "mounting" << ToQString( m_Game->m_File ) << "again, all of it";
	{
		CDebugTimer timer( QStringLiteral( "mounting the search paths" ) );
		m_pFileSystem->Mount( *m_Game );
	}
	{
		CDebugTimer timer( QStringLiteral( "building the Game tree" ) );
		m_pModel->Rebuild( *m_pFileSystem );
	}
	DebugLog() << m_pModel->GetFileCount() << "files in the game," << m_pFileSystem->GetRecords().size() << "search paths";
	{
		CDebugTimer timer( QStringLiteral( "indexing the sounds" ) );
		m_SoundIndex.Rebuild( *m_pFileSystem );
	}
	QApplication::restoreOverrideCursor();
	CDebugTimer timer( QStringLiteral( "updating the open tabs for the game" ) );

	m_ParticleGame.reset();
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pDetails = qobject_cast<CDetailEditor *>( m_pTabs->widget( i ) ) )
		{
			SetUpDetails( pDetails );
		}
		else if ( auto *pEditor = qobject_cast<CSoundEditor *>( m_pTabs->widget( i ) ) )
		{
			pEditor->Relint();
		}
		else if ( auto *pParticles = qobject_cast<CParticleEditor *>( m_pTabs->widget( i ) ) )
		{
			pParticles->SetGame( ParticleGame() );
		}
		else if ( auto *pBrowser = qobject_cast<CModelBrowser *>( m_pTabs->widget( i ) ) )
		{
			pBrowser->SetGame( *m_Game );
		}
		else if ( IEditorTab *pTab = AsEditorTab( m_pTabs->widget( i ) ) )
		{
			pTab->GameChanged( &*m_Game );
		}
	}
	if ( !szSelected.isEmpty() )
	{
		const QModelIndex index = m_pFilter->mapFromSource( m_pModel->IndexForPath( szSelected.toStdString() ) );
		const QSignalBlocker blocker{ m_pTree->selectionModel() };
		m_pTree->setCurrentIndex( index );
		m_pTree->scrollTo( index );
	}
	ShowMounts();
	UpdateGameLabel();
	UpdateTitle();
}

//-----------------------------------------------------------------------------
// Purpose: Adding a saved file to its folder mount and the tree takes a moment; a full Remount
//          reads every VPK and folder again. Only what the file can change is looked at again.
//-----------------------------------------------------------------------------
void CMainWindow::FilesWritten( const std::vector<fs::path> &files )
{
	if ( !m_Game )
	{
		return;
	}
	CDebugTimer timer( QStringLiteral( "taking in the saved files" ) );
	bool bSounds = false, bScripts = false, bArt = false, bParticles = false;
	for ( const fs::path &file : files )
	{
		const auto szPath = m_pFileSystem->AddLooseFile( file );
		DebugLog() << "saved" << ToQString( file ) << ( szPath ? QStringLiteral( "as the game's %1" ).arg( QString::fromStdString( *szPath ) ) : QStringLiteral( "outside the mounted folders" ) );
		if ( !szPath )
		{
			// Outside every mounted folder: a folder the game hasn't mounted yet (a new write folder
			// in custom/) is only found by mounting again; a file elsewhere isn't the game's.
			const fs::path inGame = file.lexically_normal().lexically_relative( m_Game->m_GameDir.parent_path().lexically_normal() );
			if ( !inGame.empty() && *inGame.begin() != ".." )
			{
				Remount();
				return;
			}
			continue;
		}
		if ( const xv::vfs::CMount *pMount = m_pFileSystem->Resolve( *szPath ) )
		{
			m_pModel->AddFile( *szPath, pMount );
		}
		const QString szGamePath = QString::fromStdString( *szPath );
		bSounds |= szGamePath.startsWith( u"sound/" );
		bScripts |= szGamePath.startsWith( u"scripts/" );
		bArt |= szGamePath.startsWith( u"materials/" ) || szGamePath.startsWith( u"models/" );
		bParticles |= IsParticlePath( szGamePath ) || szGamePath.startsWith( u"particles/" );
	}
	if ( bSounds )
	{
		m_SoundIndex.Rebuild( *m_pFileSystem );
	}
	if ( bParticles )
	{
		m_ParticleGame.reset();
	}
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pDetails = qobject_cast<CDetailEditor *>( m_pTabs->widget( i ) ); pDetails && bArt )
		{
			SetUpDetails( pDetails );
		}
		else if ( auto *pEditor = qobject_cast<CSoundEditor *>( m_pTabs->widget( i ) ); pEditor && ( bSounds || bScripts ) )
		{
			pEditor->Relint();
		}
		else if ( auto *pParticles = qobject_cast<CParticleEditor *>( m_pTabs->widget( i ) ); pParticles && bParticles )
		{
			pParticles->SetGame( ParticleGame() );
		}
	}
	UpdateGameLabel();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::UpdateGameLabel()
{
	if ( !m_Game )
	{
		m_pGameLabel->setText( tr( "No game mounted" ) );
		m_pGameLabel->setToolTip( tr( "Files open without a game too; open a game for sound lookups, linting against it and manifests" ) );
		return;
	}
	// The write folder is usually inside the game's own folder: custom/vomit-tools.
	const fs::path inGame = GetWriteFolder().lexically_normal().lexically_relative( m_Game->m_GameDir.lexically_normal() );
	const bool bInside = !inGame.empty() && *inGame.begin() != "..";
	m_pGameLabel->setText( tr( "%1 · %2 files · writing to %3" )
			.arg( QString::fromStdString( m_Game->m_szGame ) )
			.arg( m_pModel->GetFileCount() )
			.arg( bInside ? ToQString( inGame ) : ToQString( GetWriteFolder() ) ) );
	m_pGameLabel->setToolTip( tr( "%1\nWriting to %2" ).arg( ToQString( m_Game->m_File ), ToQString( GetWriteFolder() ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::ShowMounts()
{
	const auto &records = m_pFileSystem->GetRecords();
	m_pMounts->setRowCount( static_cast<int>( records.size() ) );
	for ( int nRow = 0; nRow < static_cast<int>( records.size() ); nRow++ )
	{
		const auto &record = records[ static_cast<std::size_t>( nRow ) ];
		QString szStatus;
		switch ( record.m_nStatus )
		{
			case xv::vfs::MountRecord_t::Status_t::STATUS_MOUNTED:
				szStatus = tr( "%n file(s)", nullptr, record.m_pMount ? static_cast<int>( record.m_pMount->GetFileCount() ) : 0 );
				break;
			case xv::vfs::MountRecord_t::Status_t::STATUS_MISSING:
				szStatus = tr( "missing" );
				break;
			case xv::vfs::MountRecord_t::Status_t::STATUS_FAILED:
				szStatus = tr( "failed: %1" ).arg( QString::fromStdString( record.m_szMessage ) );
				break;
		}
		QStringList ids;
		for ( const auto &szId : record.m_Ids )
		{
			ids << QString::fromStdString( szId );
		}
		m_pMounts->setItem( nRow, 0, new QTableWidgetItem( szStatus ) );
		m_pMounts->setItem( nRow, 1, new QTableWidgetItem( QString::fromStdString( record.m_szSearchPath ) ) );
		m_pMounts->setItem( nRow, 2, new QTableWidgetItem( ids.join( u'+' ) ) );
		m_pMounts->setItem( nRow, 3, new QTableWidgetItem( ToQString( record.m_Location ) ) );
	}
	m_pMounts->resizeColumnsToContents();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::SetPreview( QWidget *pView )
{
	QLayout *pLayout = m_pPreviewHost->layout();
	while ( QLayoutItem *pItem = pLayout->takeAt( 0 ) )
	{
		// Deleted later, not now: the old view may be what asked for this one (a double-click on
		// a material's texture) and still be handling the mouse.
		if ( QWidget *pOld = pItem->widget() )
		{
			pOld->hide();
			pOld->deleteLater();
		}
		delete pItem;
	}
	pLayout->addWidget( pView );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CMainWindow::WithEditButton( QWidget *pView, const QString &szText, const QString &szButton, const std::function<void()> &pfnOpen )
{
	auto *pWrapper = new QWidget;
	auto *pBar = new QHBoxLayout;
	pBar->setContentsMargins( 6, 6, 6, 0 );
	pBar->addWidget( new QLabel( szText ) );
	auto *pEdit = new QPushButton( szButton );
	connect( pEdit, &QPushButton::clicked, this, pfnOpen );
	pBar->addWidget( pEdit );
	pBar->addStretch();
	auto *pLayout = new QVBoxLayout( pWrapper );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addLayout( pBar );
	pLayout->addWidget( pView );
	return pWrapper;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::PreviewFile( const QModelIndex &proxyIndex )
{
	const QModelIndex index = m_pFilter->mapToSource( proxyIndex );
	if ( !index.isValid() || index.data( CVfsModel::ROLE_IS_DIRECTORY_ROLE ).toBool() )
	{
		return;
	}
	PreviewGameFile( index.data( CVfsModel::ROLE_PATH_ROLE ).toString(), index.siblingAtColumn( CVfsModel::COLUMN_SOURCE_COLUMN ).data( Qt::ToolTipRole ).toString() );
}

//-----------------------------------------------------------------------------
// Purpose: A file of the mounted game in the preview, with the button to edit it where it has an
//          editor.
//-----------------------------------------------------------------------------
void CMainWindow::PreviewGameFile( const QString &szPath, const QString &szSource )
{
	// The game folder's own VPKs are loose files in it too; they're shown, not read.
	if ( const QString szDisk = DiskPathForGame( szPath ); !szDisk.isEmpty() && xv::vfs::CPackage::IsPackageFile( ToPath( szDisk ) ) )
	{
		PreviewDiskFile( szDisk );
		return;
	}
	const auto bytes = m_pFileSystem->Read( szPath.toStdString() );
	if ( !bytes )
	{
		SetPreview( Placeholder( tr( "Could not read %1" ).arg( szPath ) ) );
		return;
	}
	const xv::material::Reader_t pfnRead = [ this ]( std::string_view szGamePath ) { return m_pFileSystem->Read( szGamePath ); };
	QWidget *pView = CreateFileView( szPath, szSource, *bytes, m_pPlayer, nullptr, pfnRead );
	LinkMaterial( pView, pfnRead, szPath, [ this, szPath, szSource ] { PreviewGameFile( szPath, szSource ); } );
	if ( szPath.endsWith( u".mdl", Qt::CaseInsensitive ) )
	{
		pView = WithEditButton( pView, tr( "This is a model." ), tr( "Open Model Viewer" ), [ this, szPath ] { OpenModelFile( szPath ); } );
	}
	else if ( const QString szMdl = GameModelForCompanion( szPath ); !szMdl.isEmpty() )
	{
		pView = WithEditButton( pView, tr( "This is part of the model %1." ).arg( szMdl ), tr( "Open Model Viewer" ), [ this, szMdl ] { OpenModelFile( szMdl ); } );
	}
	else if ( szPath.endsWith( u".vbsp", Qt::CaseInsensitive ) )
	{
		pView = WithEditButton( pView, tr( "This is a detail file." ), tr( "Open in Details Editor" ), [ this, szPath ] { OpenDetailFile( szPath ); } );
	}
	else if ( szPath.endsWith( u".vmf", Qt::CaseInsensitive ) )
	{
		pView = WithEditButton( pView, tr( "This is a Hammer map." ), tr( "Open Map Viewer" ), [ this, szPath ] { OpenMapFile( szPath ); } );
	}
	else if ( const FileKind_t nKind = xv::sound::FileKindForPath( szPath.toStdString() ); nKind != FileKind_t::FILE_KIND_UNKNOWN )
	{
		pView = WithEditButton( pView, tr( "This is a %1 file." ).arg( KindName( nKind ) ), tr( "Open in Editor" ), [ this, szPath ] { OpenSoundFile( szPath ); } );
	}
	else if ( const QString szSky = SkyNameFor( szPath ); !szSky.isEmpty() )
	{
		pView = WithEditButton( pView, tr( "This is part of the sky %1." ).arg( szSky ), tr( "Open Sky in Editor" ), [ this, szSky ] { OpenSky( szSky ); } );
	}
	else if ( IsParticlePath( szPath ) && ReadParticleFile( *bytes ) )
	{
		pView = WithEditButton( pView, tr( "This is a particle file." ), tr( "Open in Particle Editor" ), [ this, szPath ] { OpenParticleFile( szPath ); } );
	}
	else if ( szPath.endsWith( u".vtf", Qt::CaseInsensitive ) )
	{
		pView = WithEditButton( pView, tr( "This is a texture." ), tr( "Open in Texture Editor" ), [ this, szPath ] { OpenTextureFile( szPath ); } );
	}
	else if ( szPath.endsWith( u".vmt", Qt::CaseInsensitive ) )
	{
		pView = WithEditButton( pView, tr( "This is a material." ), tr( "Edit Material" ), [ this, szPath ] { OpenMaterialFile( szPath ); } );
	}
	SetPreview( pView );
	m_pTabs->setCurrentWidget( m_pPreviewHost );
}

//-----------------------------------------------------------------------------
// Purpose: The real file behind a game path, when it's a loose file; empty for files in VPKs.
//-----------------------------------------------------------------------------
QString CMainWindow::DiskPathForGame( const QString &szGamePath ) const
{
	const std::string szPath = xv::vfs::NormalizePath( szGamePath.toStdString() );
	const xv::vfs::CMount *pMount = m_pFileSystem->Resolve( szPath );
	const auto disk = pMount ? pMount->DiskPath( szPath ) : std::nullopt;
	return disk ? ToQString( *disk ) : QString{};
}

//-----------------------------------------------------------------------------
// Purpose: A material's textures and included materials open in the preview when double-clicked,
//          with a button back to the material.
//-----------------------------------------------------------------------------
void CMainWindow::LinkMaterial( QWidget *pView, const xv::material::Reader_t &pfnRead, const QString &szMaterial, const std::function<void()> &pfnBack )
{
	auto *pMaterial = qobject_cast<CMaterialView *>( pView );
	if ( !pMaterial )
	{
		return;
	}
	connect( pMaterial, &CMaterialView::OpenRequested, this, [ this, pfnRead, szMaterial, pfnBack ]( const QString &szGamePath ) {
		const auto bytes = pfnRead ? pfnRead( szGamePath.toStdString() ) : std::nullopt;
		if ( !bytes )
		{
			statusBar()->showMessage( tr( "Could not read %1" ).arg( szGamePath ), 5000 );
			return;
		}
		QWidget *pLinked = CreateFileView( szGamePath, {}, *bytes, m_pPlayer, nullptr, pfnRead );
		LinkMaterial( pLinked, pfnRead, szGamePath, pfnBack );
		SetPreview( WithEditButton( pLinked, tr( "%1, named by %2." ).arg( szGamePath, szMaterial ), tr( "Back" ), pfnBack ) );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::PreviewDiskFile( const QString &szPath )
{
	// Archives run to gigabytes: they aren't read just to show what they are.
	if ( xv::vfs::CPackage::IsPackageFile( ToPath( szPath ) ) )
	{
		const QFileInfo info{ szPath };
		SetPreview( WithEditButton( new CFileInfoView( info.fileName(), QDir::toNativeSeparators( info.absolutePath() ), static_cast<std::size_t>( info.size() ) ),
			szPath.endsWith( u".bsp", Qt::CaseInsensitive ) ? tr( "This is a map." ) : tr( "This is an archive." ), tr( "Open" ), [ this, szPath ] { OpenPackage( szPath ); } ) );
		m_pTabs->setCurrentWidget( m_pPreviewHost );
		return;
	}
	const auto data = ReadDiskFile( szPath );
	if ( !data )
	{
		SetPreview( Placeholder( tr( "Could not read %1" ).arg( QDir::toNativeSeparators( szPath ) ) ) );
		return;
	}
	const QFileInfo info{ szPath };
	const xv::material::Reader_t pfnRead = xv::material::DiskReader( ToPath( szPath ), m_Game ? m_pFileSystem.get() : nullptr );
	QWidget *pView = CreateFileView( info.fileName(), QDir::toNativeSeparators( info.absolutePath() ), AsBytes( *data ), m_pPlayer, nullptr, pfnRead );
	LinkMaterial( pView, pfnRead, info.fileName(), [ this, szPath ] { PreviewDiskFile( szPath ); } );
	if ( const auto sky = SkyFilesFor( szPath ) )
	{
		const auto face = xv::sky::FaceFromFileName( info.fileName().toStdString() );
		pView = WithEditButton( pView, tr( "This is the %1 face of the sky %2 (%3 of its 6 faces are here)." ).arg( face ? QString::fromUtf8( xv::sky::Suffix( *face ) ) : QString{}, QString::fromStdString( sky->m_szName ) ).arg( sky->Count() ),
			tr( "Open Sky in Editor" ), [ this, szPath ] { OpenDiskFile( szPath ); } );
	}
	else if ( info.suffix().compare( u"vbsp", Qt::CaseInsensitive ) == 0 )
	{
		pView = WithEditButton( pView, tr( "This is a detail file." ), tr( "Open in Details Editor" ), [ this, szPath ] { OpenDiskFile( szPath ); } );
	}
	else if ( info.suffix().compare( u"vmf", Qt::CaseInsensitive ) == 0 )
	{
		pView = WithEditButton( pView, tr( "This is a Hammer map." ), tr( "Open Map Viewer" ), [ this, szPath ] { OpenDiskFile( szPath ); } );
	}
	else if ( info.suffix().compare( u"mdl", Qt::CaseInsensitive ) == 0 )
	{
		pView = WithEditButton( pView, tr( "This is a model." ), tr( "Open Model Viewer" ), [ this, szPath ] { OpenModelFile( szPath ); } );
	}
	else if ( const QString szMdl = DiskModelForCompanion( szPath ); !szMdl.isEmpty() )
	{
		pView = WithEditButton( pView, tr( "This is part of the model %1." ).arg( QFileInfo( szMdl ).fileName() ), tr( "Open Model Viewer" ), [ this, szMdl ] { OpenModelFile( szMdl ); } );
	}
	else if ( info.suffix().compare( u"txt", Qt::CaseInsensitive ) == 0 )
	{
		const FileKind_t nKind = xv::sound::GuessFileKind( info.fileName().toStdString(), xv::kv1::Parse( data->toStdString() ).m_Document );
		if ( nKind != FileKind_t::FILE_KIND_UNKNOWN )
		{
			pView = WithEditButton( pView, tr( "This is a %1 file." ).arg( KindName( nKind ) ), tr( "Open in Editor" ), [ this, szPath ] { OpenDiskFile( szPath ); } );
		}
	}
	else if ( ReadParticleFile( AsBytes( *data ) ) )
	{
		pView = WithEditButton( pView, tr( "This is a particle file." ), tr( "Open in Particle Editor" ), [ this, szPath ] { OpenDiskFile( szPath ); } );
	}
	else if ( info.suffix().compare( u"vtf", Qt::CaseInsensitive ) == 0 )
	{
		pView = WithEditButton( pView, tr( "This is a texture." ), tr( "Open in Texture Editor" ), [ this, szPath ] { OpenDiskFile( szPath ); } );
	}
	else if ( info.suffix().compare( u"vmt", Qt::CaseInsensitive ) == 0 )
	{
		pView = WithEditButton( pView, tr( "This is a material." ), tr( "Edit Material" ), [ this, szPath ] { OpenDiskFile( szPath ); } );
	}
	else if ( IsImageFile( szPath ) )
	{
		pView = WithEditButton( pView, tr( "This is a picture." ), tr( "Create VTF…" ), [ this, szPath ] { NewTextureFromFiles( { szPath } ); } );
	}
	SetPreview( pView );
	m_pTabs->setCurrentWidget( m_pPreviewHost );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::OpenDiskFile( const QString &szPath )
{
	DebugLog() << "opening" << szPath;
	const QFileInfo info{ szPath };
	if ( info.fileName().compare( u"gameinfo.txt", Qt::CaseInsensitive ) == 0 )
	{
		OpenGame( szPath );
		return;
	}
	if ( xv::vfs::CPackage::IsPackageFile( ToPath( szPath ) ) )
	{
		OpenPackage( szPath );
		return;
	}
	AutoMountFor( szPath );
	AddRecent( szPath );
	if ( const auto sky = SkyFilesFor( szPath ) )
	{
		OpenSkyFiles( *sky );
		return;
	}
	if ( info.suffix().compare( u"mdl", Qt::CaseInsensitive ) == 0 )
	{
		OpenModelFile( info.absoluteFilePath() );
		return;
	}
	if ( info.suffix().compare( u"vmf", Qt::CaseInsensitive ) == 0 )
	{
		OpenMapFile( info.absoluteFilePath() );
		return;
	}
	if ( const QString szMdl = DiskModelForCompanion( info.absoluteFilePath() ); !szMdl.isEmpty() )
	{
		OpenModelFile( szMdl );
		return;
	}
	const fs::path file = ToPath( info.absoluteFilePath() );
	if ( auto *pDetails = DetailEditorFor( {}, file ) )
	{
		m_pTabs->setCurrentWidget( pDetails );
		return;
	}
	if ( CSoundEditor *pEditor = EditorForDisk( file ) )
	{
		m_pTabs->setCurrentWidget( pEditor );
		return;
	}
	if ( CParticleEditor *pEditor = ParticleEditorForDisk( file ) )
	{
		m_pTabs->setCurrentWidget( pEditor );
		return;
	}
	if ( CTextureEditor *pEditor = TextureEditorForDisk( file ) )
	{
		m_pTabs->setCurrentWidget( pEditor );
		return;
	}
	if ( CMaterialEditor *pEditor = MaterialEditorFor( {}, file ) )
	{
		ShowMaterialEditor( pEditor );
		return;
	}
	const auto data = ReadDiskFile( szPath );
	if ( !data )
	{
		QMessageBox::warning( this, tr( "Cannot open" ), tr( "Could not read %1." ).arg( QDir::toNativeSeparators( szPath ) ) );
		return;
	}
	if ( info.suffix().compare( u"vbsp", Qt::CaseInsensitive ) == 0 )
	{
		auto *pDocument = new CDetailDocument( GamePathForDisk( file ), xv::kv1::Parse( data->toStdString() ) );
		pDocument->SetDiskPath( file );
		AddDetailEditor( pDocument );
		return;
	}
	if ( info.suffix().compare( u"txt", Qt::CaseInsensitive ) == 0 )
	{
		auto parsed = xv::kv1::Parse( data->toStdString() );
		const FileKind_t nKind = xv::sound::GuessFileKind( info.fileName().toStdString(), parsed.m_Document );
		if ( nKind != FileKind_t::FILE_KIND_UNKNOWN )
		{
			auto *pDocument = new CSoundDocument( GamePathForDisk( file ), nKind, std::move( parsed ) );
			pDocument->SetDiskPath( file );
			AddEditor( pDocument );
			return;
		}
	}
	if ( auto particles = ReadParticleFile( AsBytes( *data ) ) )
	{
		auto *pDocument = new CParticleDocument( GamePathForDisk( file ), std::move( *particles ) );
		pDocument->SetDiskPath( file );
		AddParticleEditor( pDocument );
		return;
	}
	if ( info.suffix().compare( u"vtf", Qt::CaseInsensitive ) == 0 )
	{
		auto *pDocument = new CTextureDocument( GamePathForDisk( file ), std::vector<std::byte>( AsBytes( *data ).begin(), AsBytes( *data ).end() ) );
		if ( pDocument->IsValid() )
		{
			pDocument->SetDiskPath( file );
			AddTextureEditor( pDocument );
			return;
		}
		delete pDocument;
	}
	if ( info.suffix().compare( u"vmt", Qt::CaseInsensitive ) == 0 )
	{
		// Textures are looked up beside it, and in the game when one is mounted then.
		const xv::material::Reader_t pfnRead = [ this, file ]( std::string_view szGamePath ) { return xv::material::DiskReader( file, m_Game ? m_pFileSystem.get() : nullptr )( szGamePath ); };
		auto *pEditor = new CMaterialEditor( GamePathForDisk( file ), QString::fromUtf8( *data ), pfnRead );
		pEditor->SetDiskPath( file );
		PlaceMaterial( pEditor );
		return;
	}
	PreviewDiskFile( szPath );
}

//-----------------------------------------------------------------------------
// Purpose: Where a file on disk sits in the game ("scripts/soundscapes_x.txt"), as far as can be
//          told.
//-----------------------------------------------------------------------------
QString CMainWindow::GamePathForDisk( const fs::path &file ) const
{
	const fs::path normal = file.lexically_normal();
	for ( const auto &record : m_pFileSystem->GetRecords() )
	{
		if ( !record.m_pMount || record.m_pMount->GetKind() != xv::vfs::CMount::Kind_t::KIND_DIRECTORY )
		{
			continue;
		}
		const fs::path relative = normal.lexically_relative( record.m_Location.lexically_normal() );
		if ( !relative.empty() && *relative.begin() != ".." )
		{
			return QString::fromStdString( xv::vfs::NormalizePath( xv::vfs::ToUtf8( relative ) ) );
		}
	}
	const QString szFolder = QString::fromStdU16String( file.parent_path().filename().u16string() ).toLower();
	const QString szName = QString::fromStdU16String( file.filename().u16string() );
	return szFolder == u"scripts" || szFolder == u"maps" || szFolder == u"particles" ? szFolder + u'/' + szName : szName;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSoundEditor *CMainWindow::EditorFor( const QString &szGamePath ) const
{
	const std::string szWanted = xv::vfs::NormalizePath( szGamePath.toStdString() );
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pEditor = qobject_cast<CSoundEditor *>( m_pTabs->widget( i ) ) )
		{
			if ( xv::vfs::NormalizePath( pEditor->GetDocument()->GetGamePath().toStdString() ) == szWanted )
			{
				return pEditor;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSoundEditor *CMainWindow::EditorForDisk( const fs::path &file ) const
{
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pEditor = qobject_cast<CSoundEditor *>( m_pTabs->widget( i ) ) )
		{
			std::error_code ec;
			const fs::path &open = pEditor->GetDocument()->GetDiskPath();
			if ( !open.empty() && fs::equivalent( open, file, ec ) )
			{
				return pEditor;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSoundEditor *CMainWindow::GetCurrentEditor() const
{
	return qobject_cast<CSoundEditor *>( m_pTabs->currentWidget() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSkyEditor *CMainWindow::GetCurrentSky() const
{
	return qobject_cast<CSkyEditor *>( m_pTabs->currentWidget() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleEditor *CMainWindow::GetCurrentParticles() const
{
	return qobject_cast<CParticleEditor *>( m_pTabs->currentWidget() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleEditor *CMainWindow::ParticleEditorFor( const QString &szGamePath ) const
{
	const std::string szWanted = xv::vfs::NormalizePath( szGamePath.toStdString() );
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pEditor = qobject_cast<CParticleEditor *>( m_pTabs->widget( i ) ) )
		{
			if ( xv::vfs::NormalizePath( pEditor->GetDocument()->GetGamePath().toStdString() ) == szWanted )
			{
				return pEditor;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleEditor *CMainWindow::ParticleEditorForDisk( const fs::path &file ) const
{
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pEditor = qobject_cast<CParticleEditor *>( m_pTabs->widget( i ) ) )
		{
			std::error_code ec;
			const fs::path &open = pEditor->GetDocument()->GetDiskPath();
			if ( !open.empty() && fs::equivalent( open, file, ec ) )
			{
				return pEditor;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: A new, empty skybox editor tab (or one for the given document).
//-----------------------------------------------------------------------------
CSkyEditor *CMainWindow::NewSky( CSkyDocument *pDocument )
{
	if ( !pDocument )
	{
		pDocument = new CSkyDocument;
	}
	auto *pEditor = new CSkyEditor( pDocument );
	m_pUndoGroup->addStack( pDocument->GetUndoStack() );
	const int nIndex = m_pTabs->addTab( pEditor, QString{} );
	const auto updateTab = [ this, pEditor ] {
		const CSkyDocument *pDoc = pEditor->GetDocument();
		const QString szName = pDoc->GetName().isEmpty() ? tr( "New Sky" ) : tr( "Sky: %1" ).arg( pDoc->GetName() );
		m_pTabs->setTabText( m_pTabs->indexOf( pEditor ), ( pDoc->IsModified() && pDoc->GetComplete() ? QStringLiteral( "*" ) : QString{} ) + szName );
		UpdateTitle();
	};
	connect( pDocument, &CSkyDocument::ModifiedChanged, this, updateTab );
	connect( pDocument, &CSkyDocument::NameChanged, this, updateTab );
	connect( pDocument, &CSkyDocument::FacesChanged, this, updateTab );
	connect( pEditor, &CSkyEditor::ExportRequested, this, [ this, pEditor ] { ExportSky( pEditor, false ); } );
	connect( pEditor, &CSkyEditor::ExportToRequested, this, [ this, pEditor ] { ExportSky( pEditor, true ); } );
	connect( pEditor, &CSkyEditor::MaterialRequested, this, [ this ]( const QString &szMaterial ) {
		if ( QDir::isAbsolutePath( szMaterial ) )
		{
			PreviewDiskFile( szMaterial );
		}
		else
		{
			PreviewGameFile( szMaterial, {} );
		}
	} );
	updateTab();
	m_pTabs->setTabToolTip( nIndex, tr( "Skybox editor" ) );
	m_pTabs->setCurrentWidget( pEditor );
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose: Opens a sky the mounted game has (its materials/skybox/<name>rt.vmt and so on).
//-----------------------------------------------------------------------------
CSkyEditor *CMainWindow::OpenSky( const QString &szName )
{
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pSky = qobject_cast<CSkyEditor *>( m_pTabs->widget( i ) ); pSky && pSky->GetDocument()->GetName() == szName && !pSky->GetDocument()->IsModified() )
		{
			m_pTabs->setCurrentWidget( pSky );
			return pSky;
		}
	}
	QApplication::setOverrideCursor( Qt::WaitCursor );
	const auto loaded = xv::sky::LoadSky( *m_pFileSystem, szName.toStdString() );
	QApplication::restoreOverrideCursor();
	if ( std::ranges::all_of( loaded.m_Cube.m_Faces, &xv::texture::CImage::Empty ) )
	{
		QStringList problems;
		for ( const std::string &szProblem : loaded.m_Problems )
		{
			problems << QString::fromStdString( szProblem );
		}
		QMessageBox::warning( this, tr( "Open Sky" ), tr( "Could not open the sky %1:\n\n%2" ).arg( szName, problems.join( u'\n' ) ) );
		return nullptr;
	}
	auto *pDocument = new CSkyDocument;
	std::array<std::optional<CSkyDocument::Slot_t>, 6> faces;
	for ( xv::sky::Face_t nFace : xv::sky::FACES )
	{
		const std::size_t i = xv::sky::IndexOf( nFace );
		if ( !loaded.m_Cube.m_Faces[ i ].Empty() )
		{
			faces[ i ] = CSkyDocument::Slot_t{ std::make_shared<const xv::texture::CImage>( loaded.m_Cube.m_Faces[ i ] ), QString::fromStdString( loaded.m_Textures[ i ] ), {}, loaded.m_Cube.m_Transforms[ i ], QString::fromStdString( loaded.m_Materials[ i ] ) };
		}
	}
	pDocument->SetName( szName );
	pDocument->SetSlots( faces, tr( "Open %1" ).arg( szName ) );
	pDocument->GetUndoStack()->clear();
	pDocument->MarkExported(); // it's what the game already has
	CSkyEditor *pEditor = NewSky( pDocument );
	if ( !loaded.m_Problems.empty() )
	{
		statusBar()->showMessage( QString::fromStdString( loaded.m_Problems.front() ), 10000 );
	}
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose: Opens a sky saved as files on disk (found from one of its faces with
//          xv::sky::findSkyFiles).
//-----------------------------------------------------------------------------
CSkyEditor *CMainWindow::OpenSkyFiles( const xv::sky::CSkyFiles &files )
{
	const QString szOrigin = SkyOrigin( files );
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pSky = qobject_cast<CSkyEditor *>( m_pTabs->widget( i ) ); pSky && pSky->GetDocument()->GetOrigin() == szOrigin )
		{
			m_pTabs->setCurrentWidget( pSky );
			return pSky;
		}
	}
	QApplication::setOverrideCursor( Qt::WaitCursor );
	const auto loaded = xv::sky::LoadSkyFiles( files, m_Game ? m_pFileSystem.get() : nullptr );
	QApplication::restoreOverrideCursor();
	if ( std::ranges::all_of( loaded.m_Cube.m_Faces, &xv::texture::CImage::Empty ) )
	{
		QMessageBox::warning( this, tr( "Open Sky" ), tr( "None of the faces of %1 could be read:\n\n%2" ).arg( QString::fromStdString( files.m_szName ), QString::fromStdString( loaded.m_Problems.empty() ? std::string{} : loaded.m_Problems.front() ) ) );
		return nullptr;
	}
	auto *pDocument = new CSkyDocument;
	std::array<std::optional<CSkyDocument::Slot_t>, 6> faces;
	for ( xv::sky::Face_t nFace : xv::sky::FACES )
	{
		const std::size_t i = xv::sky::IndexOf( nFace );
		if ( !loaded.m_Cube.m_Faces[ i ].Empty() )
		{
			faces[ i ] = CSkyDocument::Slot_t{ std::make_shared<const xv::texture::CImage>( loaded.m_Cube.m_Faces[ i ] ), ToQString( files.m_Faces[ i ] ), {}, loaded.m_Cube.m_Transforms[ i ], QString::fromStdString( loaded.m_Materials[ i ] ) };
		}
	}
	pDocument->SetName( QString::fromStdString( files.m_szName ) );
	pDocument->SetSlots( faces, tr( "Open %1" ).arg( QString::fromStdString( files.m_szName ) ) );
	pDocument->GetUndoStack()->clear();
	pDocument->MarkExported(); // nothing changed yet
	pDocument->SetOrigin( szOrigin );
	// A sky from <folder>/materials/skybox is exported back into <folder>.
	for ( const auto &face : files.m_Faces )
	{
		const fs::path skybox = face.parent_path();
		if ( !face.empty() && files.m_bInSkyboxFolder && QString::fromStdU16String( skybox.parent_path().filename().u16string() ).compare( u"materials", Qt::CaseInsensitive ) == 0 )
		{
			pDocument->SetExportFolder( skybox.parent_path().parent_path() );
			break;
		}
	}
	CSkyEditor *pEditor = NewSky( pDocument );
	if ( !loaded.m_Problems.empty() )
	{
		statusBar()->showMessage( QString::fromStdString( loaded.m_Problems.front() ), 10000 );
	}
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose: Writes the sky's VTFs and VMTs to the write folder, or to a folder the user picks.
//-----------------------------------------------------------------------------
bool CMainWindow::ExportSky( CSkyEditor *pEditor, bool bChooseFolder )
{
	CSkyDocument *pDocument = pEditor->GetDocument();
	if ( !pEditor->CanExport() )
	{
		QStringList errors;
		for ( const auto &issue : pEditor->GetIssues() )
		{
			if ( issue.m_nSeverity == CSkyEditor::Issue_t::Severity_t::SEVERITY_ERROR )
			{
				errors << QStringLiteral( "• " ) + issue.m_szMessage;
			}
		}
		m_pTabs->setCurrentWidget( pEditor );
		QMessageBox::warning( this, tr( "Export Sky" ), tr( "The sky can't be exported yet:\n\n%1" ).arg( errors.join( u'\n' ) ) );
		return false;
	}
	fs::path folder;
	if ( !bChooseFolder && !pDocument->GetExportFolder().empty() )
	{
		folder = pDocument->GetExportFolder();
	}
	else if ( bChooseFolder || !m_Game )
	{
		QSettings settings;
		const QString szStart = settings.value( SKY_EXPORT_KEY, m_Game ? ToQString( GetWriteFolder() ) : QDir::homePath() ).toString();
		const QString szPicked = AskFolder( this, tr( "Export Sky To (a game, mod or custom/ folder; files go under materials/skybox)" ), szStart );
		if ( szPicked.isEmpty() )
		{
			return false;
		}
		settings.setValue( SKY_EXPORT_KEY, szPicked );
		folder = ToPath( szPicked );
	}
	else
	{
		folder = GetWriteFolder();
	}

	// A sky of the same name elsewhere in the game gets replaced for every map that uses it.
	const std::string szMaterial = "materials/skybox/" + pDocument->GetName().toStdString() + "rt.vmt";
	if ( const xv::vfs::CMount *pMount = m_Game ? m_pFileSystem->Resolve( szMaterial ) : nullptr )
	{
		const auto disk = pMount->DiskPath( szMaterial );
		const fs::path relative = disk ? disk->lexically_normal().lexically_relative( folder.lexically_normal() ) : fs::path{};
		const bool bOurs = !relative.empty() && *relative.begin() != "..";
		if ( !bOurs && QMessageBox::question( this, tr( "Export Sky" ), tr( "The game already has a sky named %1 (in %2). Exporting under that name replaces it for every map that uses it, "
																			"as long as the export folder is one of the game's search paths.\n\nExport anyway?" )
																			.arg( pDocument->GetName(), ToQString( pMount->GetLocation() ) ) ) != QMessageBox::Yes )
		{
			return false;
		}
	}

	std::error_code ec;
	const bool bNewFolder = !fs::exists( folder, ec );
	QApplication::setOverrideCursor( Qt::WaitCursor );
	const auto files = xv::sky::ExportSky( pDocument->GetCube(), pDocument->GetExportOptions() );
	QApplication::restoreOverrideCursor();
	if ( files.empty() )
	{
		QMessageBox::critical( this, tr( "Export Sky" ), tr( "The sky could not be encoded." ) );
		return false;
	}
	// Replacing files this sky didn't just write (the ones it was opened from, say) is asked first.
	const auto nExisting = std::ranges::count_if( files, [ &folder, &ec ]( const auto &file ) { return fs::exists( folder / xv::vfs::FromUtf8( file.m_szPath ), ec ); } );
	if ( nExisting > 0 && pDocument->GetLastExportFolder() != folder && QMessageBox::question( this, tr( "Export Sky" ), tr( "%1 of the sky's files already exist in %2 and will be replaced.\n\nExport anyway?" ).arg( nExisting ).arg( ToQString( folder / "materials" / "skybox" ) ) ) != QMessageBox::Yes )
	{
		return false;
	}
	for ( const auto &file : files )
	{
		const fs::path target = folder / xv::vfs::FromUtf8( file.m_szPath );
		fs::create_directories( target.parent_path(), ec );
		QSaveFile out( ToQString( target ) );
		if ( !out.open( QIODevice::WriteOnly ) || out.write( reinterpret_cast<const char *>( file.m_Data.data() ), static_cast<qint64>( file.m_Data.size() ) ) != static_cast<qint64>( file.m_Data.size() ) || !out.commit() )
		{
			QMessageBox::critical( this, tr( "Export Sky" ), tr( "Could not write %1:\n%2" ).arg( ToQString( target ), out.errorString() ) );
			return false;
		}
	}
	pDocument->MarkExported();
	pDocument->SetLastExportFolder( folder );
	// The game only scans custom/ folders when it starts.
	pEditor->ShowExported( ToQString( folder / "materials" / "skybox" ) + ( bNewFolder && m_Game && !bChooseFolder ? tr( " (a new folder: restart the game once so it finds it)" ) : QString{} ) );
	statusBar()->showMessage( tr( "Exported %1 files for the sky %2" ).arg( files.size() ).arg( pDocument->GetName() ), 8000 );
	if ( m_Game )
	{
		Remount();
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Opens a soundscape or soundscript from the mounted game in an editor tab.
//-----------------------------------------------------------------------------
CSoundEditor *CMainWindow::OpenSoundFile( const QString &szGamePath )
{
	if ( CSoundEditor *pEditor = EditorFor( szGamePath ) )
	{
		m_pTabs->setCurrentWidget( pEditor );
		return pEditor;
	}
	const std::string szPath = xv::vfs::NormalizePath( szGamePath.toStdString() );
	const FileKind_t nKind = xv::sound::FileKindForPath( szPath );
	const auto text = m_pFileSystem->ReadText( szPath );
	if ( nKind == FileKind_t::FILE_KIND_UNKNOWN || !text )
	{
		QMessageBox::warning( this, tr( "Cannot edit" ), tr( "%1 is not a soundscape or soundscript file in the mounted game." ).arg( szGamePath ) );
		return nullptr;
	}
	auto *pDocument = new CSoundDocument( QString::fromStdString( szPath ), nKind, xv::kv1::Parse( *text ) );
	if ( const xv::vfs::CMount *pMount = m_pFileSystem->Resolve( szPath ) )
	{
		if ( auto disk = pMount->DiskPath( szPath ) )
		{
			pDocument->SetDiskPath( *disk );
		}
	}
	return AddEditor( pDocument );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSoundEditor *CMainWindow::AddEditor( CSoundDocument *pDocument )
{
	auto *pEditor = new CSoundEditor( pDocument, m_SoundIndex, m_pPlayer );
	pDocument->setParent( pEditor );
	m_pUndoGroup->addStack( pDocument->GetUndoStack() );

	const int nIndex = m_pTabs->addTab( pEditor, pDocument->GetFileName() );
	m_pTabs->setTabToolTip( nIndex, pDocument->GetDiskPath().empty() ? pDocument->GetGamePath() : ToQString( pDocument->GetDiskPath() ) );
	const auto updateTab = [ this, pEditor ] {
		const int i = m_pTabs->indexOf( pEditor );
		const CSoundDocument *pDoc = pEditor->GetDocument();
		m_pTabs->setTabText( i, ( pDoc->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pDoc->GetFileName() );
		m_pTabs->setTabToolTip( i, pDoc->GetDiskPath().empty() ? pDoc->GetGamePath() : ToQString( pDoc->GetDiskPath() ) );
		UpdateTitle();
	};
	connect( pDocument, &CSoundDocument::ModifiedChanged, this, updateTab );
	connect( pEditor, &CSoundEditor::AddToManifestRequested, this, qOverload<CSoundDocument *>( &CMainWindow::AddToManifest ) );
	updateTab();
	m_pTabs->setCurrentWidget( pEditor );
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::NewSoundFile( FileKind_t nKind )
{
	const QString szPrefix = nKind == FileKind_t::FILE_KIND_SOUNDSCAPE ? QStringLiteral( "soundscapes_" ) : QStringLiteral( "game_sounds_" );
	bool bOk = false;
	const QString szName = QInputDialog::getText( this, tr( "New %1 File" ).arg( KindName( nKind ) ),
		tr( "File name (scripts/%1<name>.txt), e.g. your map's name:" ).arg( szPrefix ), QLineEdit::Normal, {}, &bOk )
							   .trimmed()
							   .toLower();
	if ( !bOk )
	{
		return;
	}
	// The prefix goes on by itself; typed anyway, it isn't doubled.
	QString szBare = szName;
	if ( szBare.startsWith( szPrefix ) )
	{
		szBare.remove( 0, szPrefix.size() );
	}
	if ( szBare.endsWith( QStringLiteral( ".txt" ) ) )
	{
		szBare.chop( 4 );
	}
	if ( szBare.isEmpty() )
	{
		QMessageBox::warning( this, tr( "No name" ), tr( "Give the file a name, such as your map's: it becomes scripts/%1<name>.txt." ).arg( szPrefix ) );
		return;
	}
	const QString &szFileName = szBare;
	if ( !QRegularExpression( QStringLiteral( "^[a-z0-9_.-]+$" ) ).match( szFileName ).hasMatch() )
	{
		QMessageBox::warning( this, tr( "Invalid name" ), tr( "Use letters, digits, '_', '-' and '.' only." ) );
		return;
	}
	const QString szGamePath = QStringLiteral( "scripts/%1%2.txt" ).arg( szPrefix, szFileName );
	if ( m_Game && m_pFileSystem->Resolve( szGamePath.toStdString() ) )
	{
		if ( QMessageBox::question( this, tr( "File exists" ), tr( "%1 already exists in the game. Open it instead?" ).arg( szGamePath ) ) == QMessageBox::Yes )
		{
			OpenSoundFile( szGamePath );
		}
		return;
	}
	auto *pDocument = new CSoundDocument( szGamePath, nKind, xv::kv1::Parse( "" ) );
	pDocument->MarkUnsaved();
	CSoundEditor *pEditor = AddEditor( pDocument );
	const QString szEntry = nKind == FileKind_t::FILE_KIND_SOUNDSCAPE ? szFileName + QStringLiteral( ".default" ) : szFileName + QStringLiteral( ".sound" );
	pDocument->InsertEntry( 0, EntryTemplate( *pDocument, szEntry.toStdString() ), tr( "New %1" ).arg( szEntry ) );
	pEditor->SelectEntry( 0 );
}

//-----------------------------------------------------------------------------
// Purpose: The mounted game's particle operators, materials and manifest, collected when first
//          needed.
//-----------------------------------------------------------------------------
const ParticleGame_t &CMainWindow::ParticleGame()
{
	if ( m_ParticleGame )
	{
		return *m_ParticleGame;
	}
	ParticleGame_t game;
	if ( m_Game )
	{
		CDebugTimer timer( QStringLiteral( "reading the game's particle files" ) );
		QApplication::setOverrideCursor( Qt::WaitCursor );
		auto pSchema = std::make_shared<xv::particles::CSchema>();
		for ( const std::string &szPath : m_pFileSystem->Find( "particles", ".pcf", true ) )
		{
			const auto bytes = m_pFileSystem->Read( szPath );
			const auto document = bytes ? ReadParticleFile( *bytes ) : std::nullopt;
			if ( !document )
			{
				continue;
			}
			pSchema->Add( *document );
			auto summaries = xv::particles::Summarize( *document, szPath );
			game.m_Systems.insert( game.m_Systems.end(), std::make_move_iterator( summaries.begin() ), std::make_move_iterator( summaries.end() ) );
			for ( std::int32_t nSystem : xv::particles::Systems( *document ) )
			{
				pSchema->AddSystemFile( document->m_Elements[ static_cast<std::size_t>( nSystem ) ].m_szName, szPath );
			}
		}
		pSchema->Finish();
		game.m_pSchema = std::move( pSchema );
		game.m_pfnFileExists = [ pFileSystem = m_pFileSystem.get() ]( std::string_view szPath ) { return pFileSystem->Resolve( szPath ) != nullptr; };
		game.m_pfnReadFile = [ pFileSystem = m_pFileSystem.get() ]( std::string_view szPath ) { return pFileSystem->Read( szPath ); };
		game.m_Manifest = xv::particles::ManifestFiles( xv::kv1::Parse( m_pFileSystem->ReadText( xv::particles::MANIFEST_PATH ).value_or( std::string{} ) ).m_Document );
		for ( const auto &[ path, mount ] : m_pFileSystem->Files() )
		{
			if ( path.starts_with( "materials/" ) && path.ends_with( ".vmt" ) && !path.starts_with( "materials/skybox/" ) )
			{
				game.m_Materials << QString::fromStdString( path.substr( 10 ) );
			}
			else if ( path.starts_with( "models/" ) && path.ends_with( ".mdl" ) )
			{
				game.m_Models << QString::fromStdString( path );
			}
		}
		QApplication::restoreOverrideCursor();
	}
	m_ParticleGame = std::move( game );
	return *m_ParticleGame;
}

//-----------------------------------------------------------------------------
// Purpose: Edit > Preferences: one window, raised again when it is already open.
//-----------------------------------------------------------------------------
CPreferencesDialog *CMainWindow::ShowPreferences()
{
	if ( !m_pPreferences )
	{
		m_pPreferences = new CPreferencesDialog( this );
	}
	m_pPreferences->show();
	m_pPreferences->raise();
	m_pPreferences->activateWindow();
	return m_pPreferences;
}

//-----------------------------------------------------------------------------
// Purpose: Shows the search over every particle system, with query typed in.
//-----------------------------------------------------------------------------
CParticleSearch *CMainWindow::FindParticleSystem( const QString &szQuery )
{
	if ( !m_pParticleSearch )
	{
		// The game's files as they are on disk, except the open ones, which may have unsaved changes.
		m_pParticleSearch = new CParticleSearch( [ this ] {
			std::vector<xv::particles::SystemSummary_t> systems;
			std::set<std::string> open;
			for ( int i = 0; i < m_pTabs->count(); i++ )
			{
				if ( auto *pEditor = qobject_cast<CParticleEditor *>( m_pTabs->widget( i ) ) )
				{
					const std::string szPath = xv::vfs::NormalizePath( pEditor->GetDocument()->GetGamePath().toStdString() );
					open.insert( szPath );
					auto summaries = xv::particles::Summarize( pEditor->GetDocument()->GetDocument(), szPath );
					systems.insert( systems.end(), summaries.begin(), summaries.end() );
				}
			}
			for ( const auto &system : ParticleGame().m_Systems )
			{
				if ( !open.contains( system.m_szFile ) )
				{
					systems.push_back( system );
				}
			}
			return systems;
		},
			this );
		connect( m_pParticleSearch, &CParticleSearch::OpenRequested, this, [ this ]( const QString &szFile, const QString &szSystem ) {
			CParticleEditor *pEditor = ParticleEditorFor( szFile );
			if ( !pEditor )
			{
				pEditor = OpenParticleFile( szFile );
			}
			if ( pEditor )
			{
				m_pTabs->setCurrentWidget( pEditor );
				if ( const auto found = xv::particles::FindSystem( pEditor->GetDocument()->GetDocument(), szSystem.toStdString() ) )
				{
					pEditor->SelectSystem( *found );
				}
			}
		} );
	}
	m_pParticleSearch->show();
	m_pParticleSearch->raise();
	m_pParticleSearch->activateWindow();
	if ( !szQuery.isEmpty() )
	{
		m_pParticleSearch->SetQuery( szQuery );
	}
	return m_pParticleSearch;
}

//-----------------------------------------------------------------------------
// Purpose: Opens a particle file from the mounted game ("particles/fire_01.pcf") in an editor tab.
//-----------------------------------------------------------------------------
CParticleEditor *CMainWindow::OpenParticleFile( const QString &szGamePath )
{
	if ( CParticleEditor *pEditor = ParticleEditorFor( szGamePath ) )
	{
		m_pTabs->setCurrentWidget( pEditor );
		return pEditor;
	}
	const std::string szPath = xv::vfs::NormalizePath( szGamePath.toStdString() );
	const auto bytes = m_pFileSystem->Read( szPath );
	auto document = bytes ? ReadParticleFile( *bytes ) : std::nullopt;
	if ( !document )
	{
		QMessageBox::warning( this, tr( "Cannot edit" ), tr( "%1 is not a particle file this version can read." ).arg( szGamePath ) );
		return nullptr;
	}
	auto *pParticles = new CParticleDocument( QString::fromStdString( szPath ), std::move( *document ) );
	if ( const xv::vfs::CMount *pMount = m_pFileSystem->Resolve( szPath ) )
	{
		if ( auto disk = pMount->DiskPath( szPath ) )
		{
			pParticles->SetDiskPath( *disk );
		}
	}
	return AddParticleEditor( pParticles );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleEditor *CMainWindow::AddParticleEditor( CParticleDocument *pDocument )
{
	auto *pEditor = new CParticleEditor( pDocument );
	pEditor->SetGame( ParticleGame() );
	m_pUndoGroup->addStack( pDocument->GetUndoStack() );
	m_pTabs->addTab( pEditor, pDocument->GetFileName() );
	const auto updateTab = [ this, pEditor ] {
		const int i = m_pTabs->indexOf( pEditor );
		const CParticleDocument *pDoc = pEditor->GetDocument();
		m_pTabs->setTabText( i, ( pDoc->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pDoc->GetFileName() );
		m_pTabs->setTabToolTip( i, pDoc->GetDiskPath().empty() ? pDoc->GetGamePath() : ToQString( pDoc->GetDiskPath() ) );
		UpdateTitle();
	};
	connect( pDocument, &CParticleDocument::ModifiedChanged, this, updateTab );
	connect( pEditor, &CParticleEditor::AddToManifestRequested, this, qOverload<CParticleDocument *>( &CMainWindow::AddToManifest ) );
	connect( pEditor, &CParticleEditor::AddToMapManifestRequested, this, &CMainWindow::ChooseMapManifest );
	connect( pEditor, &CParticleEditor::SearchRequested, this, &CMainWindow::FindParticleSystem );
	updateTab();
	m_pTabs->setCurrentWidget( pEditor );
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::NewParticleFile()
{
	bool bOk = false;
	const QString szName = QInputDialog::getText( this, tr( "New Particle File" ),
		tr( "File name (particles/<name>.pcf), e.g. your map's or mod's name:" ), QLineEdit::Normal, {}, &bOk )
							   .trimmed()
							   .toLower();
	if ( !bOk || szName.isEmpty() )
	{
		return;
	}
	if ( !QRegularExpression( QStringLiteral( "^[a-z0-9_.-]+$" ) ).match( szName ).hasMatch() )
	{
		QMessageBox::warning( this, tr( "Invalid name" ), tr( "Use letters, digits, '_', '-' and '.' only." ) );
		return;
	}
	const QString szGamePath = QStringLiteral( "particles/%1.pcf" ).arg( szName );
	if ( m_Game && m_pFileSystem->Resolve( szGamePath.toStdString() ) )
	{
		if ( QMessageBox::question( this, tr( "File exists" ), tr( "%1 already exists in the game. Open it instead?" ).arg( szGamePath ) ) == QMessageBox::Yes )
		{
			OpenParticleFile( szGamePath );
		}
		return;
	}
	auto *pDocument = new CParticleDocument( szGamePath, xv::particles::EmptyFile() );
	pDocument->MarkUnsaved();
	CParticleEditor *pEditor = AddParticleEditor( pDocument );
	pEditor->NewSystem( szName );
	pDocument->GetUndoStack()->clear();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::WriteParticles( CParticleEditor *pEditor, const fs::path &target )
{
	CParticleDocument *pDocument = pEditor->GetDocument();
	std::string szError;
	const auto bytes = pDocument->Serialize( &szError );
	if ( !bytes )
	{
		QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not encode %1:\n%2" ).arg( pDocument->GetFileName(), QString::fromStdString( szError ) ) );
		return false;
	}
	std::error_code ec;
	fs::create_directories( target.parent_path(), ec );
	QSaveFile file( ToQString( target ) );
	if ( !file.open( QIODevice::WriteOnly ) || file.write( reinterpret_cast<const char *>( bytes->data() ), static_cast<qint64>( bytes->size() ) ) != static_cast<qint64>( bytes->size() ) || !file.commit() )
	{
		QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not write %1:\n%2" ).arg( ToQString( target ), file.errorString() ) );
		return false;
	}
	pDocument->SetDiskPath( target );
	pDocument->MarkSaved();
	statusBar()->showMessage( tr( "Saved %1" ).arg( ToQString( target ) ), 8000 );
	FilesWritten( { target } );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::Save( CParticleEditor *pEditor )
{
	CParticleDocument *pDocument = pEditor->GetDocument();
	if ( pDocument->IsNew() || ( pDocument->GetDiskPath().empty() && !m_Game ) )
	{
		return SaveAs( pEditor );
	}
	// A file from a VPK is saved as a copy in the write folder, which the game reads first.
	const fs::path target = pDocument->GetDiskPath().empty() ? WriteTarget( pDocument->GetGamePath() ) : pDocument->GetDiskPath();
	if ( !WriteParticles( pEditor, target ) )
	{
		return false;
	}
	if ( m_Game && !pEditor->GetListedInManifest() && !pEditor->property( "manifestOffered" ).toBool() && CPreferences::Instance().IsManifestReminder() )
	{
		pEditor->setProperty( "manifestOffered", true );
		if ( AskAddToManifest( tr( "%1 is not listed in %2, so the game only loads it for maps that ask for it.\n\nAdd it now?" )
					 .arg( pDocument->GetGamePath(), QString::fromUtf8( xv::particles::MANIFEST_PATH ) ) ) )
		{
			AddToManifest( pDocument ); // takes the manifest into the game too
			return true;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Save As also converts: binary 1 or 2, or keyvalues2 text for reading.
//-----------------------------------------------------------------------------
bool CMainWindow::SaveAs( CParticleEditor *pEditor )
{
	CParticleDocument *pDocument = pEditor->GetDocument();
	const fs::path current = !pDocument->GetDiskPath().empty() ? pDocument->GetDiskPath()
		: m_Game                                               ? WriteTarget( pDocument->GetGamePath() )
															   : ToPath( QDir::homePath() ) / xv::vfs::FromUtf8( pDocument->GetFileName().toStdString() );
	const QString szBinary2 = tr( "Particle file, binary encoding 2 (*.pcf)" );
	const QString szBinary1 = tr( "Particle file, binary encoding 1 (*.pcf)" );
	const QString szText = tr( "keyvalues2 text, for reading and comparing (*.txt)" );
	QString szFilter = pDocument->GetDocument().m_nEncodingVersion == 1 ? szBinary1 : szBinary2;
	const QString szFile = AskSavePath( current, QStringList{ szBinary2, szBinary1, szText }.join( QStringLiteral( ";;" ) ), &szFilter );
	if ( szFile.isEmpty() )
	{
		return false;
	}
	if ( szFilter == szText )
	{
		QSaveFile out( szFile );
		const QByteArray bytes = QByteArray::fromStdString( xv::dmx::WriteKeyValues2( xv::particles::Compact( pDocument->GetDocument() ) ) );
		if ( !out.open( QIODevice::WriteOnly ) || out.write( bytes ) != bytes.size() || !out.commit() )
		{
			QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not write %1:\n%2" ).arg( szFile, out.errorString() ) );
			return false;
		}
		statusBar()->showMessage( tr( "Wrote %1 as keyvalues2 text; the editor keeps the binary file" ).arg( QDir::toNativeSeparators( szFile ) ), 8000 );
		return true;
	}
	const int nEncoding = szFilter == szBinary1 ? 1 : 2;
	if ( nEncoding != pDocument->GetDocument().m_nEncodingVersion )
	{
		pDocument->Edit( tr( "Convert to binary encoding %1" ).arg( nEncoding ), [ nEncoding ]( xv::dmx::Document_t &doc ) { doc.m_nEncodingVersion = nEncoding; } );
	}
	if ( !WriteParticles( pEditor, ToPath( szFile ) ) )
	{
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::AddToManifest( CParticleDocument *pDocument )
{
	if ( !m_Game )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "Manifests belong to a game; open the game first." ) );
		return;
	}
	const std::string szManifestPath{ xv::particles::MANIFEST_PATH };
	auto parsed = xv::kv1::Parse( m_pFileSystem->ReadText( szManifestPath ).value_or( std::string{} ) );
	if ( !xv::particles::AddToManifest( parsed.m_Document, pDocument->GetGamePath().toStdString() ) )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "%1 is already listed." ).arg( pDocument->GetGamePath() ) );
		return;
	}
	if ( const auto target = WriteGameFile( szManifestPath, xv::kv1::Write( parsed.m_Document ), tr( "Manifest" ) ) )
	{
		FilesWritten( { *target } );
		statusBar()->showMessage( tr( "Added %1 to %2" ).arg( pDocument->GetGamePath(), ToQString( *target ) ), 8000 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Asks which map (the game's maps are offered; any name will do), then lists the file in
//          its manifest.
//-----------------------------------------------------------------------------
void CMainWindow::ChooseMapManifest( CParticleDocument *pDocument )
{
	if ( !m_Game )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "Manifests belong to a game; open the game first." ) );
		return;
	}
	QStringList maps;
	for ( const std::string &szPath : m_pFileSystem->Find( "maps", ".bsp" ) )
	{
		maps << QFileInfo{ QString::fromStdString( szPath ) }.completeBaseName();
	}
	maps.removeDuplicates();
	maps.sort( Qt::CaseInsensitive );
	bool bOk = false;
	const QString szMap = QInputDialog::getItem( this, tr( "Add to a Map's Manifest" ), tr( "The map that loads %1:" ).arg( pDocument->GetFileName() ), maps, 0, true, &bOk ).trimmed();
	if ( bOk && !szMap.isEmpty() )
	{
		AddToMapManifest( pDocument, szMap );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Lists a particle file in a map's manifest.
//-----------------------------------------------------------------------------
bool CMainWindow::AddToMapManifest( CParticleDocument *pDocument, const QString &szMap )
{
	if ( !m_Game || !xv::sky::IsValidName( szMap.toStdString() ) )
	{
		QMessageBox::information( this, tr( "Manifest" ), m_Game ? tr( "\"%1\" isn't a map name." ).arg( szMap ) : tr( "Manifests belong to a game; open the game first." ) );
		return false;
	}
	const std::string szManifestPath = xv::particles::MapManifestPath( szMap.toStdString() );
	auto parsed = xv::kv1::Parse( m_pFileSystem->ReadText( szManifestPath ).value_or( std::string{} ) );
	if ( !xv::particles::AddToManifest( parsed.m_Document, pDocument->GetGamePath().toStdString() ) )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "%1 is already listed for %2." ).arg( pDocument->GetGamePath(), szMap ) );
		return false;
	}
	if ( const auto target = WriteGameFile( szManifestPath, xv::kv1::Write( parsed.m_Document ), tr( "Manifest" ) ) )
	{
		FilesWritten( { *target } );
		statusBar()->showMessage( tr( "Added %1 to %2" ).arg( pDocument->GetGamePath(), ToQString( *target ) ), 8000 );
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Writes a file the game reads (a manifest): in place when it's loose, else an overriding
//          copy in
//          the write folder. Returns where it went, or nothing (after saying why) on failure.
//-----------------------------------------------------------------------------
std::optional<fs::path> CMainWindow::WriteGameFile( const std::string &szGamePath, std::string_view szBytes, const QString &szTitle )
{
	fs::path target = WriteTarget( QString::fromStdString( szGamePath ) );
	if ( const xv::vfs::CMount *pMount = m_pFileSystem->Resolve( szGamePath ) )
	{
		if ( auto disk = pMount->DiskPath( szGamePath ) )
		{
			target = *disk;
		}
	}
	std::error_code ec;
	fs::create_directories( target.parent_path(), ec );
	QSaveFile file( ToQString( target ) );
	if ( !file.open( QIODevice::WriteOnly ) || file.write( szBytes.data(), static_cast<qint64>( szBytes.size() ) ) != static_cast<qint64>( szBytes.size() ) || !file.commit() )
	{
		QMessageBox::critical( this, szTitle, tr( "Could not write %1:\n%2" ).arg( ToQString( target ), file.errorString() ) );
		return std::nullopt;
	}
	return target;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
fs::path CMainWindow::GetWriteFolder() const
{
	if ( !m_Game )
	{
		return {};
	}
	const QString szStored = QSettings{}.value( WriteFolderKey( m_Game->m_File ) ).toString();
	return szStored.isEmpty() ? m_Game->m_GameDir / "custom" / "vomit-tools" : ToPath( szStored );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
fs::path CMainWindow::WriteTarget( const QString &szGamePath ) const
{
	return GetWriteFolder() / xv::vfs::FromUtf8( szGamePath.toStdString() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::ChooseWriteFolder()
{
	if ( !m_Game )
	{
		QMessageBox::information( this, tr( "No game open" ), tr( "The write folder belongs to a game; open one first." ) );
		return;
	}
	const QString szFolder = AskFolder( this, tr( "Write Folder" ), ToQString( GetWriteFolder() ) );
	if ( szFolder.isEmpty() )
	{
		return;
	}
	QSettings{}.setValue( WriteFolderKey( m_Game->m_File ), szFolder );
	QMessageBox::information( this, tr( "Write Folder" ),
		tr( "Files edited from VPKs and new files are now saved under\n%1\n\n"
			"The game only sees them if that folder is one of its search paths, "
			"such as a folder inside custom/. The game scans custom/ when it starts." )
			.arg( szFolder ) );
	Remount();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::WriteDocument( CSoundEditor *pEditor, const fs::path &target )
{
	CSoundDocument *pDocument = pEditor->GetDocument();
	std::error_code ec;
	fs::create_directories( target.parent_path(), ec );
	QSaveFile file( ToQString( target ) );
	const std::string szBytes = pDocument->GetSerialize();
	if ( !file.open( QIODevice::WriteOnly ) || file.write( szBytes.data(), static_cast<qint64>( szBytes.size() ) ) != static_cast<qint64>( szBytes.size() ) || !file.commit() )
	{
		QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not write %1:\n%2" ).arg( ToQString( target ), file.errorString() ) );
		return false;
	}
	pDocument->SetDiskPath( target );
	pDocument->MarkSaved();
	statusBar()->showMessage( tr( "Saved %1" ).arg( ToQString( target ) ), 8000 );
	FilesWritten( { target } );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::Save( CSoundEditor *pEditor )
{
	CSoundDocument *pDocument = pEditor->GetDocument();
	if ( pDocument->IsNew() || ( pDocument->GetDiskPath().empty() && !m_Game ) )
	{
		return SaveAs( pEditor );
	}
	const fs::path target = pDocument->GetDiskPath().empty() ? WriteTarget( pDocument->GetGamePath() ) : pDocument->GetDiskPath();
	if ( !WriteDocument( pEditor, target ) )
	{
		return false;
	}
	// A file with nothing in it yet isn't worth listing.
	const bool bHasEntries = std::ranges::any_of( pDocument->GetDocument().m_Roots, &xv::kv1::CNode::IsBlock );
	if ( m_Game && bHasEntries && !pEditor->GetListedInManifest() && !pEditor->property( "manifestOffered" ).toBool() && CPreferences::Instance().IsManifestReminder() )
	{
		pEditor->setProperty( "manifestOffered", true );
		if ( AskAddToManifest( tr( "%1 is not listed in %2, so the game won't load it.\n\nAdd it now?" )
					 .arg( pDocument->GetGamePath(), QString::fromUtf8( xv::sound::ManifestPathFor( pDocument->GetKind() ) ) ) ) )
		{
			AddToManifest( pDocument ); // takes the manifest into the game too
			return true;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::SaveAs( CSoundEditor *pEditor )
{
	CSoundDocument *pDocument = pEditor->GetDocument();
	const fs::path current = !pDocument->GetDiskPath().empty() ? pDocument->GetDiskPath()
		: m_Game                                               ? WriteTarget( pDocument->GetGamePath() )
															   : ToPath( QDir::homePath() ) / xv::vfs::FromUtf8( pDocument->GetFileName().toStdString() );
	const QString szFile = AskSavePath( current, tr( "Text files (*.txt);;All files (*)" ) );
	if ( szFile.isEmpty() || !WriteDocument( pEditor, ToPath( szFile ) ) )
	{
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Asks whether to list a saved file in its manifest, with "Don't remind me again"
//          (Preferences → Interface turns it back on).
//-----------------------------------------------------------------------------
bool CMainWindow::AskAddToManifest( const QString &szText )
{
	QMessageBox box( QMessageBox::Question, tr( "Add to manifest?" ), szText, QMessageBox::Yes | QMessageBox::No, this );
	box.setObjectName( QStringLiteral( "manifestQuestion" ) );
	auto *pStop = new QCheckBox( tr( "Don't remind me again" ) );
	pStop->setToolTip( tr( "Preferences → Interface brings it back" ) );
	box.setCheckBox( pStop );
	const bool bYes = box.exec() == QMessageBox::Yes;
	if ( pStop->isChecked() )
	{
		CPreferences::Instance().SetManifestReminder( false );
	}
	return bYes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::AddToManifest( CSoundDocument *pDocument )
{
	if ( !m_Game )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "Manifests belong to a game; open the game first." ) );
		return;
	}
	if ( std::ranges::none_of( pDocument->GetDocument().m_Roots, &xv::kv1::CNode::IsBlock ) )
	{
		QMessageBox::warning( this, tr( "Manifest" ), tr( "%1 has nothing in it yet; add a %2 before listing it in the manifest." ).arg( pDocument->GetGamePath(), QString::fromUtf8( pDocument->GetSchema().m_szEntryNoun.data(), static_cast<qsizetype>( pDocument->GetSchema().m_szEntryNoun.size() ) ) ) );
		return;
	}
	const FileKind_t nKind = pDocument->GetKind();
	const std::string szManifestPath{ xv::sound::ManifestPathFor( nKind ) };
	auto parsed = xv::kv1::Parse( m_pFileSystem->ReadText( szManifestPath ).value_or( std::string{} ) );
	if ( std::ranges::none_of( parsed.m_Document.m_Roots, &xv::kv1::CNode::IsBlock ) )
	{
		parsed.m_Document.AppendRoot( xv::kv1::CNode::MakeBlock( nKind == FileKind_t::FILE_KIND_SOUNDSCAPE ? "soundscapes_manifest" : "game_sounds_manifest" ) );
	}
	if ( !xv::sound::AddToManifest( parsed.m_Document, nKind, pDocument->GetGamePath().toStdString() ) )
	{
		QMessageBox::information( this, tr( "Manifest" ), tr( "%1 is already listed." ).arg( pDocument->GetGamePath() ) );
		return;
	}

	// Edit the loose manifest in place; a manifest inside a VPK gets an overriding copy in the write folder.
	if ( const auto target = WriteGameFile( szManifestPath, xv::kv1::Write( parsed.m_Document ), tr( "Manifest" ) ) )
	{
		FilesWritten( { *target } );
		statusBar()->showMessage( tr( "Added %1 to %2" ).arg( pDocument->GetGamePath(), ToQString( *target ) ), 8000 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::ConfirmClose( CSoundEditor *pEditor )
{
	if ( !pEditor->GetDocument()->IsModified() )
	{
		return true;
	}
	m_pTabs->setCurrentWidget( pEditor );
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved changes" ),
		tr( "Save changes to %1?" ).arg( pEditor->GetDocument()->GetFileName() ),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	if ( nAnswer == QMessageBox::Save )
	{
		return Save( pEditor );
	}
	return nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::ConfirmClose( CSkyEditor *pEditor )
{
	const CSkyDocument *pDocument = pEditor->GetDocument();
	if ( !pDocument->IsModified() || std::ranges::none_of( xv::sky::FACES, [ pDocument ]( xv::sky::Face_t nFace ) { return pDocument->Slot( nFace ).m_pImage != nullptr; } ) )
	{
		return true;
	}
	m_pTabs->setCurrentWidget( pEditor );
	const auto nAnswer = QMessageBox::question( this, tr( "Unexported changes" ),
		tr( "The sky %1 has changes that haven't been exported. Export them now?" ).arg( pDocument->GetName().isEmpty() ? tr( "(unnamed)" ) : pDocument->GetName() ),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	if ( nAnswer == QMessageBox::Save )
	{
		return ExportSky( pEditor, false );
	}
	return nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::ConfirmClose( CParticleEditor *pEditor )
{
	if ( !pEditor->GetDocument()->IsModified() )
	{
		return true;
	}
	m_pTabs->setCurrentWidget( pEditor );
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved changes" ),
		tr( "Save changes to %1?" ).arg( pEditor->GetDocument()->GetFileName() ),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	if ( nAnswer == QMessageBox::Save )
	{
		return Save( pEditor );
	}
	return nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::CloseTab( int nIndex )
{
	if ( IEditorTab *pTab = AsEditorTab( m_pTabs->widget( nIndex ) ) )
	{
		if ( !pTab->ConfirmClose() )
		{
			return false;
		}
		pTab->StopWork();
		if ( QUndoStack *pStack = pTab->GetUndoStack() )
		{
			m_pUndoGroup->removeStack( pStack );
		}
		QWidget *pWidget = m_pTabs->widget( nIndex );
		m_pTabs->removeTab( nIndex );
		pWidget->deleteLater();
		return true;
	}
	if ( auto *pBrowser = qobject_cast<CModelBrowser *>( m_pTabs->widget( nIndex ) ) )
	{
		m_pTabs->removeTab( nIndex );
		pBrowser->deleteLater();
		return true;
	}
	if ( auto *pModel = qobject_cast<CModelEditor *>( m_pTabs->widget( nIndex ) ) )
	{
		if ( !ConfirmClose( pModel ) )
		{
			return false;
		}
		m_pUndoGroup->removeStack( pModel->GetDocument()->GetUndoStack() );
		m_pTabs->removeTab( nIndex );
		pModel->deleteLater();
		return true;
	}
	if ( auto *pPackage = qobject_cast<CVpkEditor *>( m_pTabs->widget( nIndex ) ) )
	{
		if ( !ConfirmClose( pPackage ) )
		{
			return false;
		}
		m_pTabs->removeTab( nIndex );
		pPackage->deleteLater();
		return true;
	}
	if ( auto *pDetails = qobject_cast<CDetailEditor *>( m_pTabs->widget( nIndex ) ) )
	{
		if ( !ConfirmClose( pDetails ) )
		{
			return false;
		}
		m_pUndoGroup->removeStack( pDetails->GetDocument()->GetUndoStack() );
		m_pTabs->removeTab( nIndex );
		pDetails->deleteLater();
		return true;
	}
	if ( auto *pMaterial = qobject_cast<CMaterialEditor *>( m_pTabs->widget( nIndex ) ) )
	{
		if ( !ConfirmClose( pMaterial ) )
		{
			return false;
		}
		m_pTabs->removeTab( m_pTabs->indexOf( pMaterial ) );
		pMaterial->deleteLater();
		return true;
	}
	if ( auto *pTexture = qobject_cast<CTextureEditor *>( m_pTabs->widget( nIndex ) ) )
	{
		if ( !ConfirmClose( pTexture ) )
		{
			return false;
		}
		m_pUndoGroup->removeStack( pTexture->GetDocument()->GetUndoStack() );
		m_pTabs->removeTab( m_pTabs->indexOf( pTexture ) );
		pTexture->deleteLater();
		return true;
	}
	if ( auto *pSky = qobject_cast<CSkyEditor *>( m_pTabs->widget( nIndex ) ) )
	{
		if ( !ConfirmClose( pSky ) )
		{
			return false;
		}
		m_pUndoGroup->removeStack( pSky->GetDocument()->GetUndoStack() );
		m_pTabs->removeTab( m_pTabs->indexOf( pSky ) );
		pSky->deleteLater();
		return true;
	}
	if ( auto *pParticles = qobject_cast<CParticleEditor *>( m_pTabs->widget( nIndex ) ) )
	{
		if ( !ConfirmClose( pParticles ) )
		{
			return false;
		}
		m_pUndoGroup->removeStack( pParticles->GetDocument()->GetUndoStack() );
		m_pTabs->removeTab( m_pTabs->indexOf( pParticles ) );
		pParticles->deleteLater();
		return true;
	}
	auto *pEditor = qobject_cast<CSoundEditor *>( m_pTabs->widget( nIndex ) );
	if ( !pEditor )
	{
		return true;
	}
	if ( !ConfirmClose( pEditor ) )
	{
		return false;
	}
	m_pUndoGroup->removeStack( pEditor->GetDocument()->GetUndoStack() );
	m_pTabs->removeTab( m_pTabs->indexOf( pEditor ) );
	pEditor->deleteLater();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::closeEvent( QCloseEvent *pEvent )
{
	for ( int i = 0; i < m_pTabs->count(); ++i )
	{
		if ( IEditorTab *pTab = AsEditorTab( m_pTabs->widget( i ) ); pTab && !pTab->ConfirmClose() )
		{
			pEvent->ignore();
			return;
		}
		if ( auto *pPackage = qobject_cast<CVpkEditor *>( m_pTabs->widget( i ) ); pPackage && !ConfirmClose( pPackage ) )
		{
			pEvent->ignore();
			return;
		}
		if ( auto *pDetails = qobject_cast<CDetailEditor *>( m_pTabs->widget( i ) ); pDetails && !ConfirmClose( pDetails ) )
		{
			pEvent->ignore();
			return;
		}
		if ( auto *pModel = qobject_cast<CModelEditor *>( m_pTabs->widget( i ) ); pModel && !ConfirmClose( pModel ) )
		{
			pEvent->ignore();
			return;
		}
	}
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pEditor = qobject_cast<CSoundEditor *>( m_pTabs->widget( i ) ); pEditor && !ConfirmClose( pEditor ) )
		{
			pEvent->ignore();
			return;
		}
		if ( auto *pSky = qobject_cast<CSkyEditor *>( m_pTabs->widget( i ) ); pSky && !ConfirmClose( pSky ) )
		{
			pEvent->ignore();
			return;
		}
		if ( auto *pParticles = qobject_cast<CParticleEditor *>( m_pTabs->widget( i ) ); pParticles && !ConfirmClose( pParticles ) )
		{
			pEvent->ignore();
			return;
		}
		if ( auto *pTexture = qobject_cast<CTextureEditor *>( m_pTabs->widget( i ) ); pTexture && !ConfirmClose( pTexture ) )
		{
			pEvent->ignore();
			return;
		}
		if ( auto *pMaterial = qobject_cast<CMaterialEditor *>( m_pTabs->widget( i ) ); pMaterial && !ConfirmClose( pMaterial ) )
		{
			pEvent->ignore();
			return;
		}
	}
	SaveSession();
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( IEditorTab *pTab = AsEditorTab( m_pTabs->widget( i ) ) )
		{
			pTab->StopWork();
		}
	}
	m_pPlayer->Stop();
	pEvent->accept();
}

//-----------------------------------------------------------------------------
// Purpose: An entry is "<kind>\t<disk or game>\t<path>"; the folder's is "folder\tdisk\t<path>".
//-----------------------------------------------------------------------------
QString CMainWindow::SessionEntryFor( int nIndex ) const
{
	const auto entry = []( const char *pszKind, const fs::path &disk, const QString &szGamePath ) {
		return !disk.empty() ? QStringLiteral( "%1\tdisk\t%2" ).arg( QLatin1String( pszKind ), ToQString( disk ) ) : QStringLiteral( "%1\tgame\t%2" ).arg( QLatin1String( pszKind ), szGamePath );
	};
	QWidget *pTab = m_pTabs->widget( nIndex );
	if ( const IEditorTab *pEditorTab = AsEditorTab( pTab ) )
	{
		return pEditorTab->GetSessionEntry();
	}
	if ( auto *pModel = qobject_cast<CModelEditor *>( pTab ); pModel && !pModel->GetOrigin().isEmpty() )
	{
		return entry( "model", QFileInfo( pModel->GetOrigin() ).isAbsolute() ? ToPath( pModel->GetOrigin() ) : fs::path{}, pModel->GetPath() );
	}
	if ( auto *pEditor = qobject_cast<CSoundEditor *>( pTab ); pEditor && !pEditor->GetDocument()->IsNew() )
	{
		return entry( "sound", pEditor->GetDocument()->GetDiskPath(), pEditor->GetDocument()->GetGamePath() );
	}
	if ( auto *pEditor = qobject_cast<CParticleEditor *>( pTab ); pEditor && !pEditor->GetDocument()->IsNew() )
	{
		return entry( "particles", pEditor->GetDocument()->GetDiskPath(), pEditor->GetDocument()->GetGamePath() );
	}
	if ( auto *pEditor = qobject_cast<CTextureEditor *>( pTab ); pEditor && !pEditor->GetDocument()->IsNew() )
	{
		return entry( "texture", pEditor->GetDocument()->GetDiskPath(), pEditor->GetDocument()->GetGamePath() );
	}
	if ( auto *pEditor = qobject_cast<CMaterialEditor *>( pTab ); pEditor && !pEditor->IsNew() )
	{
		return entry( "material", pEditor->GetDiskPath(), pEditor->GetGamePath() );
	}
	if ( auto *pEditor = qobject_cast<CDetailEditor *>( pTab ); pEditor && !pEditor->GetDocument()->IsNew() )
	{
		return entry( "detail", pEditor->GetDocument()->GetDiskPath(), pEditor->GetDocument()->GetGamePath() );
	}
	if ( auto *pPackage = qobject_cast<CVpkEditor *>( pTab ) )
	{
		return entry( "vpk", pPackage->GetPackage().GetFile(), {} );
	}
	if ( auto *pSky = qobject_cast<CSkyEditor *>( pTab ) )
	{
		// A sky from files reopens from one of its faces; one from the game, by its name.
		for ( xv::sky::Face_t nFace : xv::sky::FACES )
		{
			const auto &slot = pSky->GetDocument()->Slot( nFace );
			if ( !slot.m_pImage )
			{
				continue;
			}
			if ( !pSky->GetDocument()->GetOrigin().isEmpty() && QFileInfo{ slot.m_szSource }.isAbsolute() )
			{
				return entry( "sky", ToPath( slot.m_szSource ), {} );
			}
			if ( pSky->GetDocument()->GetOrigin().isEmpty() && !slot.m_szMaterial.isEmpty() && !pSky->GetDocument()->GetName().isEmpty() )
			{
				return entry( "sky", {}, pSky->GetDocument()->GetName() );
			}
			break;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: Keeps what is open for the next start.
//-----------------------------------------------------------------------------
void CMainWindow::SaveSession() const
{
	QStringList entries;
	QString szCurrent;
	for ( int i = 0; i < m_pTabs->count(); ++i )
	{
		const QString szEntry = SessionEntryFor( i );
		if ( !szEntry.isEmpty() && !entries.contains( szEntry ) )
		{
			entries << szEntry;
		}
		if ( i == m_pTabs->currentIndex() )
		{
			szCurrent = szEntry;
		}
	}
	DebugLog() << "keeping" << entries.size() << "tabs for the next start";
	QSettings settings;
	settings.setValue( SESSION_KEY, entries );
	settings.setValue( SESSION_CURRENT_KEY, szCurrent );
	settings.setValue( SESSION_SIDE_KEY, m_pSides->currentWidget() == m_pFolders ? QStringLiteral( "folder" ) : QStringLiteral( "game" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QStringList CMainWindow::GetLastSession()
{
	QStringList entries;
	const QString szFolder = QSettings{}.value( LAST_FOLDER_KEY ).toString();
	if ( !szFolder.isEmpty() && QFileInfo{ szFolder }.isDir() )
	{
		entries << QStringLiteral( "folder\tdisk\t" ) + szFolder;
	}
	if ( CPreferences::Instance().IsReopenSession() )
	{
		entries << QSettings{}.value( SESSION_KEY ).toStringList();
	}
	return entries;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMainWindow::SessionEntryName( const QString &szEntry )
{
	const QString szPath = szEntry.section( u'\t', 2 );
	return szEntry.startsWith( u"folder\t" ) ? QDir{ szPath }.dirName() : QFileInfo{ szPath }.fileName();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::RestoreSessionEntry( const QString &szEntry )
{
	DebugLog() << "reopening" << QString( szEntry ).replace( u'\t', u' ' );
	const QString szKind = szEntry.section( u'\t', 0, 0 );
	const bool bDisk = szEntry.section( u'\t', 1, 1 ) == u"disk";
	const QString szPath = szEntry.section( u'\t', 2 );
	if ( szPath.isEmpty() )
	{
		return;
	}
	if ( szKind == u"folder" )
	{
		// Shown without taking the side panel from the game; the side comes back at the end.
		if ( QFileInfo{ szPath }.isDir() )
		{
			m_pFolders->SetFolder( szPath );
			AutoMountFor( szPath );
		}
		return;
	}
	QWidget *pBefore = m_pTabs->currentWidget();
	if ( bDisk )
	{
		if ( !QFileInfo{ szPath }.isFile() )
		{
			return;
		}
		OpenDiskFile( szPath );
	}
	else if ( !m_Game || ( szKind != u"sky" && !m_pFileSystem->Resolve( xv::vfs::NormalizePath( szPath.toStdString() ) ) ) )
	{
		return;
	}
	else if ( szKind == u"sound" )
	{
		OpenSoundFile( szPath );
	}
	else if ( szKind == u"particles" )
	{
		OpenParticleFile( szPath );
	}
	else if ( szKind == u"texture" )
	{
		OpenTextureFile( szPath );
	}
	else if ( szKind == u"material" )
	{
		OpenMaterialFile( szPath );
	}
	else if ( szKind == u"model" )
	{
		OpenModelFile( szPath );
	}
	else if ( szKind == u"map" )
	{
		OpenMapFile( szPath );
	}
	else if ( szKind == u"detail" )
	{
		OpenDetailFile( szPath );
	}
	else if ( szKind == u"sky" )
	{
		OpenSky( szPath );
	}
	if ( szEntry == QSettings{}.value( SESSION_CURRENT_KEY ).toString() && m_pTabs->currentWidget() != pBefore )
	{
		m_pRestoredCurrent = m_pTabs->currentWidget();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::FinishRestoringSession()
{
	if ( m_pRestoredCurrent )
	{
		m_pTabs->setCurrentWidget( m_pRestoredCurrent );
	}
	const QString szSide = QSettings{}.value( SESSION_SIDE_KEY ).toString();
	if ( szSide == u"folder" && !m_pFolders->GetFolder().isEmpty() )
	{
		m_pSides->setCurrentWidget( m_pFolders );
	}
	else if ( szSide == u"game" && m_Game )
	{
		m_pSides->setCurrentWidget( m_pGameBrowser );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::dragEnterEvent( QDragEnterEvent *pEvent )
{
	const auto urls = pEvent->mimeData()->urls();
	if ( !urls.isEmpty() && std::ranges::all_of( urls, &QUrl::isLocalFile ) )
	{
		pEvent->acceptProposedAction();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::dropEvent( QDropEvent *pEvent )
{
	QStringList paths;
	for ( const QUrl &url : pEvent->mimeData()->urls() )
	{
		paths << url.toLocalFile();
	}
	OpenPaths( paths );
	pEvent->acceptProposedAction();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::UpdateTitle()
{
	QString szTitle = QStringLiteral( "vomit-tools" );
	if ( m_Game )
	{
		szTitle.prepend( QString::fromStdString( m_Game->m_szGame ) + QStringLiteral( " — " ) );
	}
	if ( auto *pDetails = GetCurrentDetails() )
	{
		szTitle.prepend( m_pTabs->tabText( m_pTabs->indexOf( pDetails ) ) + QStringLiteral( " — " ) );
	}
	else if ( CSoundEditor *pEditor = GetCurrentEditor() )
	{
		const CSoundDocument *pDocument = pEditor->GetDocument();
		szTitle.prepend( ( pDocument->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pDocument->GetFileName() + QStringLiteral( " — " ) );
	}
	else if ( CSkyEditor *pSky = GetCurrentSky() )
	{
		szTitle.prepend( m_pTabs->tabText( m_pTabs->indexOf( pSky ) ) + QStringLiteral( " — " ) );
	}
	else if ( CParticleEditor *pParticles = GetCurrentParticles() )
	{
		const CParticleDocument *pDocument = pParticles->GetDocument();
		szTitle.prepend( ( pDocument->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pDocument->GetFileName() + QStringLiteral( " — " ) );
	}
	else if ( CTextureEditor *pTexture = GetCurrentTexture() )
	{
		szTitle.prepend( ( pTexture->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pTexture->GetDocument()->GetFileName() + QStringLiteral( " — " ) );
	}
	else if ( CMaterialEditor *pMaterial = GetCurrentMaterial() )
	{
		szTitle.prepend( ( pMaterial->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pMaterial->GetFileName() + QStringLiteral( " — " ) );
	}
	else if ( auto *pPackage = qobject_cast<CVpkEditor *>( m_pTabs->currentWidget() ) )
	{
		szTitle.prepend( pPackage->GetFileName() + QStringLiteral( " — " ) );
	}
	else if ( auto *pModel = qobject_cast<CModelEditor *>( m_pTabs->currentWidget() ) )
	{
		szTitle.prepend( m_pTabs->tabText( m_pTabs->indexOf( pModel ) ) + QStringLiteral( " — " ) );
	}
	else if ( const IEditorTab *pTab = AsEditorTab( m_pTabs->currentWidget() ) )
	{
		szTitle.prepend( pTab->GetTabTitle() + QStringLiteral( " — " ) );
	}
	setWindowTitle( szTitle );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTextureEditor *CMainWindow::GetCurrentTexture() const
{
	return qobject_cast<CTextureEditor *>( m_pTabs->currentWidget() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTextureEditor *CMainWindow::TextureEditorFor( const QString &szGamePath ) const
{
	const std::string szWanted = xv::vfs::NormalizePath( szGamePath.toStdString() );
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pEditor = qobject_cast<CTextureEditor *>( m_pTabs->widget( i ) ) )
		{
			if ( pEditor->GetDocument()->GetDiskPath().empty() && xv::vfs::NormalizePath( pEditor->GetDocument()->GetGamePath().toStdString() ) == szWanted )
			{
				return pEditor;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTextureEditor *CMainWindow::TextureEditorForDisk( const fs::path &file ) const
{
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pEditor = qobject_cast<CTextureEditor *>( m_pTabs->widget( i ) ) )
		{
			std::error_code ec;
			const fs::path &open = pEditor->GetDocument()->GetDiskPath();
			if ( !open.empty() && fs::equivalent( open, file, ec ) )
			{
				return pEditor;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Opens a texture from the mounted game ("materials/x.vtf") in an editor tab.
//-----------------------------------------------------------------------------
CTextureEditor *CMainWindow::OpenTextureFile( const QString &szGamePath, bool bFindMaterial )
{
	const std::string szPath = xv::vfs::NormalizePath( szGamePath.toStdString() );
	fs::path disk;
	if ( const xv::vfs::CMount *pMount = m_pFileSystem->Resolve( szPath ) )
	{
		disk = pMount->DiskPath( szPath ).value_or( fs::path{} );
	}
	if ( CTextureEditor *pEditor = disk.empty() ? TextureEditorFor( szGamePath ) : TextureEditorForDisk( disk ) )
	{
		m_pTabs->setCurrentWidget( pEditor );
		return pEditor;
	}
	const auto bytes = m_pFileSystem->Read( szPath );
	auto *pDocument = new CTextureDocument( QString::fromStdString( szPath ), bytes ? std::vector<std::byte>( bytes->begin(), bytes->end() ) : std::vector<std::byte>{} );
	if ( !pDocument->IsValid() )
	{
		delete pDocument;
		QMessageBox::warning( this, tr( "Cannot edit" ), tr( "%1 is not a texture this version can read." ).arg( szGamePath ) );
		return nullptr;
	}
	pDocument->SetDiskPath( disk );
	return AddTextureEditor( pDocument, bFindMaterial );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTextureEditor *CMainWindow::AddTextureEditor( CTextureDocument *pDocument, bool bFindMaterial )
{
	auto *pEditor = new CTextureEditor( pDocument );
	pDocument->setParent( pEditor );
	m_pUndoGroup->addStack( pDocument->GetUndoStack() );
	m_pTabs->addTab( pEditor, pDocument->GetFileName() );
	const auto updateTab = [ this, pEditor ] {
		const int i = m_pTabs->indexOf( pEditor );
		const CTextureDocument *pDoc = pEditor->GetDocument();
		m_pTabs->setTabText( i, ( pEditor->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pDoc->GetFileName() );
		QString szTip = pDoc->GetDiskPath().empty() ? pDoc->GetGamePath() : ToQString( pDoc->GetDiskPath() );
		if ( const CMaterialEditor *pMaterial = pEditor->GetMaterial() )
		{
			szTip += u'\n' + ( pMaterial->GetDiskPath().empty() ? pMaterial->GetGamePath() : ToQString( pMaterial->GetDiskPath() ) );
		}
		m_pTabs->setTabToolTip( i, szTip );
		UpdateTitle();
	};
	connect( pEditor, &CTextureEditor::ModifiedChanged, this, updateTab );
	connect( pEditor, &CTextureEditor::MakeMaterialRequested, this, [ this, pEditor ] { MakeMaterialFor( pEditor ); } );
	connect( pEditor, &CTextureEditor::SaveMaterialAsRequested, this, [ this, pEditor ] {
		if ( CMaterialEditor *pMaterial = pEditor->GetMaterial() )
		{
			SaveAs( pMaterial );
		}
	} );
	connect( pEditor, &CTextureEditor::CloseMaterialRequested, this, [ this, pEditor ] { CloseMaterial( pEditor ); } );
	// The material of the same name beside it goes with it.
	if ( bFindMaterial )
	{
		fs::path disk = pDocument->GetDiskPath();
		QString szGamePath = pDocument->GetGamePath();
		szGamePath.chop( QFileInfo{ szGamePath }.suffix().size() );
		szGamePath += QStringLiteral( "vmt" );
		std::optional<QString> text;
		if ( !disk.empty() )
		{
			disk.replace_extension( ".vmt" );
			disk = xv::vfs::FindCaseInsensitive( disk );
			std::error_code ec;
			if ( fs::is_regular_file( disk, ec ) )
			{
				if ( const auto data = ReadDiskFile( ToQString( disk ) ) )
				{
					text = QString::fromUtf8( *data );
					szGamePath = GamePathForDisk( disk );
				}
			}
		}
		else if ( const auto game = m_pFileSystem->ReadText( xv::vfs::NormalizePath( szGamePath.toStdString() ) ) )
		{
			text = QString::fromStdString( *game );
			if ( const xv::vfs::CMount *pMount = m_pFileSystem->Resolve( xv::vfs::NormalizePath( szGamePath.toStdString() ) ) )
			{
				disk = pMount->DiskPath( xv::vfs::NormalizePath( szGamePath.toStdString() ) ).value_or( fs::path{} );
			}
		}
		if ( text )
		{
			auto *pMaterial = new CMaterialEditor( szGamePath, *text, ReaderFor( pEditor, disk ) );
			pMaterial->SetDiskPath( disk );
			SetUpMaterial( pMaterial );
			pEditor->SetMaterial( pMaterial );
		}
	}
	updateTab();
	m_pTabs->setCurrentWidget( pEditor );
	return pEditor;
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The game path a texture has under materials/ ("materials/brick/wall.vtf"): from where
//          it is on disk, else its game path; empty when it isn't under a materials folder.
//-----------------------------------------------------------------------------
std::string MaterialsPathOf( const CTextureDocument *pDocument )
{
	for ( const fs::path &file : { pDocument->GetDiskPath(), pDocument->GetSuggestedPath() } )
	{
		for ( fs::path dir = file.parent_path(); !file.empty() && !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path() )
		{
			if ( QString::fromStdU16String( dir.filename().u16string() ).compare( u"materials", Qt::CaseInsensitive ) == 0 )
			{
				return xv::vfs::NormalizePath( "materials/" + xv::vfs::ToUtf8( file.lexically_relative( dir ) ) );
			}
		}
	}
	const std::string szGame = xv::vfs::NormalizePath( pDocument->GetGamePath().toStdString() );
	return szGame.starts_with( "materials/" ) ? szGame : std::string{};
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The textures a material in a texture's tab reads: that texture as edited first.
//-----------------------------------------------------------------------------
xv::material::Reader_t CMainWindow::ReaderFor( CTextureEditor *pTexture, const fs::path &material )
{
	const QPointer<CTextureDocument> pDocument = pTexture ? pTexture->GetDocument() : nullptr;
	return [ this, pDocument, material ]( std::string_view szName ) -> std::optional<std::vector<std::byte>> {
		if ( pDocument )
		{
			const std::string szOwn = MaterialsPathOf( pDocument );
			if ( !szOwn.empty() && xv::vfs::NormalizePath( szName ) == szOwn )
			{
				return pDocument->GetBytes();
			}
		}
		return material.empty() ? m_pFileSystem->Read( szName ) : xv::material::DiskReader( material, m_Game ? m_pFileSystem.get() : nullptr )( szName );
	};
}

//-----------------------------------------------------------------------------
// Purpose: What a material editor offers: the game's surface properties and texture names.
//-----------------------------------------------------------------------------
void CMainWindow::SetUpMaterial( CMaterialEditor *pMaterial )
{
	pMaterial->GetForm()->SetDetailTypes( [ this ] {
		std::vector<Choice_t> choices;
		const auto add = [ & ]( const xv::kv1::CDocument &document ) {
			for ( const auto &type : xv::detail::Types( document ) )
			{
				const auto szName = QString::fromStdString( type.m_szName );
				if ( std::ranges::none_of( choices, [ & ]( const Choice_t &choice ) { return choice.m_szValue.compare( szName, Qt::CaseInsensitive ) == 0; } ) )
				{
					choices.push_back( { szName, szName } );
				}
			}
		};
		for ( int i = 0; i < m_pTabs->count(); ++i )
		{
			if ( auto *pDetails = qobject_cast<CDetailEditor *>( m_pTabs->widget( i ) ) )
			{
				add( pDetails->GetDocument()->GetDocument() );
			}
		}
		if ( !DetailEditorFor( QStringLiteral( "detail.vbsp" ) ) )
		{
			if ( const auto text = m_pFileSystem->ReadText( "detail.vbsp" ) )
			{
				add( xv::kv1::Parse( *text ).m_Document );
			}
		}
		return choices;
	} );
	if ( !pMaterial->property( "detailLinkConnected" ).toBool() )
	{
		pMaterial->setProperty( "detailLinkConnected", true );
		connect( pMaterial, &CMaterialEditor::OpenDetailTypeRequested, this, [ this ]( const QString &szName ) {
			for ( int i = 0; i < m_pTabs->count(); ++i )
			{
				if ( auto *pDetails = qobject_cast<CDetailEditor *>( m_pTabs->widget( i ) ) )
				{
					for ( const auto &type : xv::detail::Types( pDetails->GetDocument()->GetDocument() ) )
					{
						if ( QString::fromStdString( type.m_szName ).compare( szName, Qt::CaseInsensitive ) == 0 )
						{
							m_pTabs->setCurrentWidget( pDetails );
							pDetails->SelectType( szName );
							return;
						}
					}
				}
			}
			if ( auto *pDetails = OpenDetailFile( QStringLiteral( "detail.vbsp" ) ) )
			{
				pDetails->SelectType( szName );
			}
		} );
	}
	pMaterial->SetSurfaceProps( SurfaceProps() );
	pMaterial->SetTextureNames( [ this ] { return TextureNames(); } );
}

//-----------------------------------------------------------------------------
// Purpose: Puts a material in the tab of the texture it uses (opening that), else in a tab of
//          its own.
//-----------------------------------------------------------------------------
CMaterialEditor *CMainWindow::PlaceMaterial( CMaterialEditor *pMaterial )
{
	SetUpMaterial( pMaterial );
	// The texture it is drawn with: its base texture, else its normal map.
	const auto parsed = xv::kv1::Parse( pMaterial->GetText().toStdString() );
	const auto root = std::ranges::find_if( parsed.m_Document.m_Roots, &xv::kv1::CNode::IsBlock );
	std::string szTexture;
	for ( const char *pszKey : { "$basetexture", "$bumpmap", "$normalmap" } )
	{
		const xv::kv1::CNode *pNode = root == parsed.m_Document.m_Roots.end() ? nullptr : root->Find( pszKey );
		if ( szTexture.empty() && pNode && pNode->IsPair() && xv::material::TextureKind( pNode->GetValue() ) == xv::material::TextureKind_t::TEXTURE_KIND_FILE )
		{
			szTexture = xv::material::TexturePath( pNode->GetValue() );
		}
	}
	CTextureEditor *pTexture = nullptr;
	if ( !szTexture.empty() )
	{
		// Beside the material's own materials folder first, then in the game.
		fs::path disk;
		for ( fs::path dir = pMaterial->GetDiskPath().parent_path(); !pMaterial->GetDiskPath().empty() && !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path() )
		{
			if ( QString::fromStdU16String( dir.filename().u16string() ).compare( u"materials", Qt::CaseInsensitive ) == 0 )
			{
				std::error_code ec;
				const fs::path candidate = xv::vfs::FindCaseInsensitive( dir.parent_path() / xv::vfs::FromUtf8( szTexture ) );
				if ( fs::is_regular_file( candidate, ec ) )
				{
					disk = candidate;
				}
				break;
			}
		}
		if ( !disk.empty() )
		{
			pTexture = TextureEditorForDisk( disk );
			if ( !pTexture )
			{
				if ( const auto data = ReadDiskFile( ToQString( disk ) ) )
				{
					auto *pDocument = new CTextureDocument( GamePathForDisk( disk ), std::vector<std::byte>( AsBytes( *data ).begin(), AsBytes( *data ).end() ) );
					if ( pDocument->IsValid() )
					{
						pDocument->SetDiskPath( disk );
						pTexture = AddTextureEditor( pDocument, false );
					}
					else
					{
						delete pDocument;
					}
				}
			}
		}
		else if ( m_pFileSystem->Resolve( szTexture ) )
		{
			pTexture = OpenTextureFile( QString::fromStdString( szTexture ), false );
		}
	}
	// With the texture, unless it already has another material with changes.
	if ( pTexture )
	{
		CMaterialEditor *pHad = pTexture->GetMaterial();
		std::error_code ec;
		const bool bSame = pHad && ( !pHad->GetDiskPath().empty() && !pMaterial->GetDiskPath().empty() ? fs::equivalent( pHad->GetDiskPath(), pMaterial->GetDiskPath(), ec ) : xv::vfs::NormalizePath( pHad->GetGamePath().toStdString() ) == xv::vfs::NormalizePath( pMaterial->GetGamePath().toStdString() ) );
		if ( bSame )
		{
			delete pMaterial;
			ShowMaterialEditor( pHad );
			return pHad;
		}
		if ( !pHad || !pHad->IsModified() )
		{
			pMaterial->SetReader( ReaderFor( pTexture, pMaterial->GetDiskPath() ) );
			pTexture->SetMaterial( pMaterial );
			ShowMaterialEditor( pMaterial );
			return pMaterial;
		}
	}
	return AddMaterialEditor( pMaterial );
}

//-----------------------------------------------------------------------------
// Purpose: Shows a material's editor, in its own tab or its texture's.
//-----------------------------------------------------------------------------
void CMainWindow::ShowMaterialEditor( CMaterialEditor *pEditor )
{
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( m_pTabs->widget( i ) == pEditor )
		{
			m_pTabs->setCurrentIndex( i );
			return;
		}
		if ( auto *pTexture = qobject_cast<CTextureEditor *>( m_pTabs->widget( i ) ); pTexture && pTexture->GetMaterial() == pEditor )
		{
			m_pTabs->setCurrentIndex( i );
			pTexture->ShowMaterial();
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Puts a texture's material away, asking first about unsaved changes.
//-----------------------------------------------------------------------------
bool CMainWindow::CloseMaterial( CTextureEditor *pEditor )
{
	CMaterialEditor *pMaterial = pEditor->GetMaterial();
	if ( !pMaterial || !ConfirmClose( pMaterial ) )
	{
		return false;
	}
	pEditor->SetMaterial( nullptr );
	pEditor->ShowTexture();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The textures the game has, as materials name them.
//-----------------------------------------------------------------------------
QStringList CMainWindow::TextureNames() const
{
	QStringList names;
	for ( const auto &[ szPath, pMount ] : m_pFileSystem->Files() )
	{
		if ( szPath.starts_with( "materials/" ) && szPath.ends_with( ".vtf" ) )
		{
			names << QString::fromStdString( szPath.substr( 10, szPath.size() - 14 ) );
		}
	}
	return names;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::ChooseNewTexture()
{
	const QString szStart = LastFolderOr( m_pFolders->GetFolder().isEmpty() && m_Game ? ToQString( m_Game->m_GameDir ) : m_pFolders->GetFolder() );
	const QStringList files = AskOpenFiles( this, tr( "Create VTF from Pictures" ), szStart,
		PictureFilter( true ) );
	if ( !files.isEmpty() )
	{
		NewTextureFromFiles( files );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Makes a new texture from pictures on disk, after asking how. It is named after the
//          first picture and saves next to it unless Save As says otherwise.
//-----------------------------------------------------------------------------
CTextureEditor *CMainWindow::NewTextureFromFiles( const QStringList &files )
{
	if ( files.isEmpty() )
	{
		return nullptr;
	}
	const QFileInfo first{ files.front() };
	const fs::path target = ToPath( first.absolutePath() ) / xv::vfs::FromUtf8( ( first.completeBaseName() + QStringLiteral( ".vtf" ) ).toStdString() );
	QString szGamePath = GamePathForDisk( target );
	if ( szGamePath.isEmpty() || !szGamePath.contains( u'/' ) )
	{
		szGamePath = QStringLiteral( "materials/%1.vtf" ).arg( first.completeBaseName().toLower() );
	}
	// An empty 1×1 texture to import into, so the editor's own import does the work.
	const xv::texture::CImage blank{ 1, 1 };
	auto *pDocument = new CTextureDocument( szGamePath, xv::texture::WriteVtf( blank, { .m_nFormat = xv::texture::VtfFormat_t::BGRA8888, .m_bMipmaps = false, .m_bLowRes = false } ) );
	CTextureEditor *pEditor = AddTextureEditor( pDocument );
	if ( !pEditor->ImportFiles( files ) )
	{
		m_pUndoGroup->removeStack( pDocument->GetUndoStack() );
		m_pTabs->removeTab( m_pTabs->indexOf( pEditor ) );
		pEditor->deleteLater();
		return nullptr;
	}
	pDocument->GetUndoStack()->clear();
	pDocument->SetSuggestedPath( target ); // Save asks where, starting beside the pictures
	pDocument->MarkUnsaved();
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::WriteTexture( CTextureEditor *pEditor, const fs::path &target )
{
	CTextureDocument *pDocument = pEditor->GetDocument();
	const std::vector<std::byte> &bytes = pDocument->GetBytes();
	std::error_code ec;
	fs::create_directories( target.parent_path(), ec );
	QSaveFile file( ToQString( target ) );
	if ( !file.open( QIODevice::WriteOnly ) || file.write( reinterpret_cast<const char *>( bytes.data() ), static_cast<qint64>( bytes.size() ) ) != static_cast<qint64>( bytes.size() ) || !file.commit() )
	{
		QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not write %1:\n%2" ).arg( ToQString( target ), file.errorString() ) );
		return false;
	}
	pDocument->SetDiskPath( target );
	pDocument->MarkSaved();
	statusBar()->showMessage( tr( "Saved %1" ).arg( ToQString( target ) ), 8000 );
	FilesWritten( { target } );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A texture from a VPK is saved as a copy in the write folder, which the game reads first.
//-----------------------------------------------------------------------------
bool CMainWindow::Save( CTextureEditor *pEditor )
{
	CTextureDocument *pDocument = pEditor->GetDocument();
	CMaterialEditor *pMaterial = pEditor->GetMaterial();
	const bool bMaterial = pMaterial && pMaterial->IsModified();
	// The texture, when it has changes or nothing else does; then its material.
	if ( pDocument->IsModified() || !bMaterial )
	{
		// A new texture always asks where it goes.
		if ( pDocument->IsNew() || ( pDocument->GetDiskPath().empty() && !m_Game ) )
		{
			if ( !SaveAs( pEditor ) )
			{
				return false;
			}
		}
		else
		{
			const fs::path target = pDocument->GetDiskPath().empty() ? WriteTarget( pDocument->GetGamePath() ) : pDocument->GetDiskPath();
			if ( !WriteTexture( pEditor, target ) )
			{
				return false;
			}
		}
	}
	if ( bMaterial )
	{
		// A new material starts beside where the texture went.
		if ( pMaterial->IsNew() && !pDocument->GetDiskPath().empty() )
		{
			fs::path beside = pDocument->GetDiskPath();
			beside.replace_extension( ".vmt" );
			pMaterial->SetSuggestedPath( beside );
		}
		return Save( pMaterial );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::SaveAs( CTextureEditor *pEditor )
{
	CTextureDocument *pDocument = pEditor->GetDocument();
	const fs::path current = !pDocument->GetDiskPath().empty() ? pDocument->GetDiskPath()
		: !pDocument->GetSuggestedPath().empty()               ? pDocument->GetSuggestedPath()
		: m_Game                                               ? WriteTarget( pDocument->GetGamePath() )
															   : ToPath( QDir::homePath() ) / xv::vfs::FromUtf8( pDocument->GetFileName().toStdString() );
	const QString szFile = AskSavePath( current, tr( "Textures (*.vtf)" ), nullptr, pDocument->GetSuggestedPath().parent_path() );
	if ( szFile.isEmpty() || !WriteTexture( pEditor, ToPath( szFile ) ) )
	{
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::ConfirmClose( CTextureEditor *pEditor )
{
	if ( !pEditor->IsModified() )
	{
		return true;
	}
	m_pTabs->setCurrentWidget( pEditor );
	QStringList names;
	if ( pEditor->GetDocument()->IsModified() )
	{
		names << pEditor->GetDocument()->GetFileName();
	}
	if ( pEditor->GetMaterial() && pEditor->GetMaterial()->IsModified() )
	{
		names << pEditor->GetMaterial()->GetFileName();
	}
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved changes" ),
		tr( "Save changes to %1?" ).arg( names.join( tr( " and " ) ) ),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	if ( nAnswer == QMessageBox::Save )
	{
		return Save( pEditor );
	}
	return nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose: The Save As window. Qt's has the places files usually go in its side list; the
//          desktop's chooser has the user's own places instead.
//-----------------------------------------------------------------------------
QString CMainWindow::AskSavePath( const fs::path &start, const QString &szFilters, QString *pFilter, const fs::path &extra )
{
	// Where the side panel's tree has a folder (or a file in one) picked, it starts there; where the
	// file would otherwise go stays in the side list.
	fs::path from = start;
	std::vector<fs::path> made;
	if ( fs::path picked = GetSelectedFolder(); !picked.empty() )
	{
		std::error_code ec;
		for ( fs::path dir = picked; !dir.empty() && !fs::exists( dir, ec ) && dir != dir.parent_path(); dir = dir.parent_path() )
		{
			made.push_back( dir );
		}
		fs::create_directories( picked, ec );
		if ( fs::is_directory( picked, ec ) )
		{
			const fs::path name = fs::is_directory( start, ec ) ? fs::path{} : start.filename();
			from = name.empty() ? picked : picked / name;
		}
	}
	FileDialog_t dialog;
	dialog.m_nMode = FILE_DIALOG_MODE_SAVE;
	dialog.m_szTitle = tr( "Save As" );
	dialog.m_szStart = ToQString( from );
	dialog.m_szFilters = szFilters;
	dialog.m_szFilter = pFilter ? *pFilter : QString{};
	dialog.m_szObjectName = QStringLiteral( "saveAsDialog" );
	QList<QUrl> &places = dialog.m_Places;
	const auto add = [ &places ]( const fs::path &folder ) {
		std::error_code ec;
		if ( !folder.empty() && fs::is_directory( folder, ec ) )
		{
			const QUrl url = QUrl::fromLocalFile( ToQString( folder ) );
			if ( !places.contains( url ) )
			{
				places << url;
			}
		}
	};
	add( extra );
	if ( from != start )
	{
		add( start.has_extension() ? start.parent_path() : start );
	}
	if ( m_Game )
	{
		std::error_code ec;
		fs::create_directories( GetWriteFolder(), ec );
		add( GetWriteFolder() );
		add( m_Game->m_GameDir / "materials" );
		add( m_Game->m_GameDir );
	}
	if ( !m_pFolders->GetFolder().isEmpty() )
	{
		add( ToPath( m_pFolders->GetFolder() ) );
	}
	add( ToPath( QDir::homePath() ) );
	add( ToPath( QStandardPaths::writableLocation( QStandardPaths::PicturesLocation ) ) );
	add( ToPath( QStandardPaths::writableLocation( QStandardPaths::DesktopLocation ) ) );
	const QStringList picked = AskForFiles( this, dialog );
	const bool bChosen = !picked.isEmpty();
	// Folders made only to start in go again unless the file goes in one of them.
	const fs::path chosen = bChosen ? ToPath( picked.front() ).lexically_normal() : fs::path{};
	for ( const fs::path &dir : made )
	{
		std::error_code ec;
		const fs::path inside = chosen.lexically_relative( dir.lexically_normal() );
		if ( chosen.empty() || inside.empty() || *inside.begin() == ".." )
		{
			fs::remove( dir, ec ); // only when still empty
		}
	}
	if ( !bChosen )
	{
		return {};
	}
	if ( pFilter )
	{
		*pFilter = dialog.m_szFilter;
	}
	// Names typed without an ending get the type's.
	QString szFile = picked.front();
	const QRegularExpressionMatch match = QRegularExpression( QStringLiteral( R"(\*\.(\w+))" ) ).match( dialog.m_szFilter );
	if ( QFileInfo{ szFile }.suffix().isEmpty() && match.hasMatch() )
	{
		szFile += u'.' + match.captured( 1 );
	}
	return szFile;
}

//-----------------------------------------------------------------------------
// Purpose: A folder picked in the Folder tree, or one in the Game tree as it sits in the write folder.
//-----------------------------------------------------------------------------
fs::path CMainWindow::GetSelectedFolder() const
{
	if ( m_pSides->currentWidget() == m_pFolders )
	{
		const QString szPicked = m_pFolders->GetSelectedPath();
		if ( szPicked.isEmpty() )
		{
			return {};
		}
		const QFileInfo info{ szPicked };
		return ToPath( info.isDir() ? info.absoluteFilePath() : info.absolutePath() );
	}
	if ( m_pSides->currentWidget() == m_pGameBrowser && m_Game && m_pTree->selectionModel() )
	{
		const QModelIndex proxyIndex = m_pTree->currentIndex();
		if ( !proxyIndex.isValid() || !m_pTree->selectionModel()->isSelected( proxyIndex ) )
		{
			return {};
		}
		const QModelIndex index = m_pFilter->mapToSource( proxyIndex ).siblingAtColumn( 0 );
		QString szPath = index.data( CVfsModel::ROLE_PATH_ROLE ).toString();
		if ( !index.data( CVfsModel::ROLE_IS_DIRECTORY_ROLE ).toBool() )
		{
			szPath = szPath.section( u'/', 0, -2 );
		}
		return szPath.isEmpty() ? GetWriteFolder() : WriteTarget( szPath );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialEditor *CMainWindow::GetCurrentMaterial() const
{
	return qobject_cast<CMaterialEditor *>( m_pTabs->currentWidget() );
}

//-----------------------------------------------------------------------------
// Purpose: The tab with this material: by its file on disk, else (from a VPK) by game path.
//-----------------------------------------------------------------------------
CMaterialEditor *CMainWindow::MaterialEditorFor( const QString &szGamePath, const fs::path &disk ) const
{
	const std::string szWanted = xv::vfs::NormalizePath( szGamePath.toStdString() );
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		auto *pEditor = qobject_cast<CMaterialEditor *>( m_pTabs->widget( i ) );
		if ( auto *pTexture = qobject_cast<CTextureEditor *>( m_pTabs->widget( i ) ) )
		{
			pEditor = pTexture->GetMaterial();
		}
		if ( !pEditor )
		{
			continue;
		}
		std::error_code ec;
		if ( !disk.empty() ? !pEditor->GetDiskPath().empty() && fs::equivalent( pEditor->GetDiskPath(), disk, ec )
						   : pEditor->GetDiskPath().empty() && !szWanted.empty() && xv::vfs::NormalizePath( pEditor->GetGamePath().toStdString() ) == szWanted )
		{
			return pEditor;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Opens a material from the mounted game in an editor tab.
//-----------------------------------------------------------------------------
CMaterialEditor *CMainWindow::OpenMaterialFile( const QString &szGamePath )
{
	const std::string szPath = xv::vfs::NormalizePath( szGamePath.toStdString() );
	fs::path disk;
	if ( const xv::vfs::CMount *pMount = m_pFileSystem->Resolve( szPath ) )
	{
		disk = pMount->DiskPath( szPath ).value_or( fs::path{} );
	}
	if ( CMaterialEditor *pEditor = MaterialEditorFor( szGamePath, disk ) )
	{
		ShowMaterialEditor( pEditor );
		return pEditor;
	}
	const auto text = m_pFileSystem->ReadText( szPath );
	if ( !text )
	{
		QMessageBox::warning( this, tr( "Cannot edit" ), tr( "Could not read %1." ).arg( szGamePath ) );
		return nullptr;
	}
	auto *pEditor = new CMaterialEditor( QString::fromStdString( szPath ), QString::fromStdString( *text ), [ this ]( std::string_view szName ) { return m_pFileSystem->Read( szName ); } );
	pEditor->SetDiskPath( disk );
	return PlaceMaterial( pEditor );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialEditor *CMainWindow::AddMaterialEditor( CMaterialEditor *pEditor )
{
	m_pTabs->addTab( pEditor, pEditor->GetFileName() );
	const auto updateTab = [ this, pEditor ] {
		const int i = m_pTabs->indexOf( pEditor );
		m_pTabs->setTabText( i, ( pEditor->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pEditor->GetFileName() );
		m_pTabs->setTabToolTip( i, pEditor->GetDiskPath().empty() ? pEditor->GetGamePath() : ToQString( pEditor->GetDiskPath() ) );
		UpdateTitle();
	};
	connect( pEditor, &CMaterialEditor::ModifiedChanged, this, updateTab );
	updateTab();
	m_pTabs->setCurrentWidget( pEditor );
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose: The surface properties the mounted game knows ($surfaceprop values), from the files
//          its surface properties manifest lists.
//-----------------------------------------------------------------------------
std::vector<Choice_t> CMainWindow::SurfaceProps() const
{
	std::vector<Choice_t> surfaces;
	if ( !m_Game )
	{
		return surfaces;
	}
	const auto manifest = m_pFileSystem->ReadText( "scripts/surfaceproperties_manifest.txt" );
	if ( !manifest )
	{
		return surfaces;
	}
	for ( const xv::kv1::CNode &root : xv::kv1::Parse( *manifest ).m_Document.m_Roots )
	{
		for ( const xv::kv1::CNode &entry : root.GetChildren() )
		{
			if ( !entry.IsPair() || !xv::kv1::EqualsIgnoreCase( entry.GetKey(), "file" ) )
			{
				continue;
			}
			const auto file = m_pFileSystem->ReadText( xv::vfs::NormalizePath( entry.GetValue() ) );
			for ( const xv::kv1::CNode &surface : file ? xv::kv1::Parse( *file ).m_Document.m_Roots : std::vector<xv::kv1::CNode>{} )
			{
				if ( !surface.IsBlock() )
				{
					continue;
				}
				const QString szName = QString::fromStdString( surface.GetKey() );
				const xv::kv1::CNode *pBase = surface.Find( "base" );
				const QString szMeaning = pBase && pBase->IsPair() ? tr( "like %1" ).arg( QString::fromStdString( pBase->GetValue() ) ) : QString{};
				if ( std::ranges::none_of( surfaces, [ &szName ]( const Choice_t &choice ) { return choice.m_szValue.compare( szName, Qt::CaseInsensitive ) == 0; } ) )
				{
					surfaces.push_back( { szName, szMeaning } );
				}
			}
		}
	}
	std::ranges::sort( surfaces, []( const Choice_t &a, const Choice_t &b ) { return a.m_szValue.compare( b.m_szValue, Qt::CaseInsensitive ) < 0; } );
	return surfaces;
}

//-----------------------------------------------------------------------------
// Purpose: A new, unsaved material for a texture, beside it.
//-----------------------------------------------------------------------------
CMaterialEditor *CMainWindow::MakeMaterialFor( CTextureEditor *pTexture )
{
	// The one it has goes, after asking about its changes.
	if ( CMaterialEditor *pHad = pTexture->GetMaterial(); pHad && !ConfirmClose( pHad ) )
	{
		return nullptr;
	}
	const CTextureDocument *pDocument = pTexture->GetDocument();
	const auto &info = pDocument->GetVtf().m_Info;
	const bool bAlpha = ( info.m_nFlags & ( xv::texture::vtf_flag::ONEBITALPHA | xv::texture::vtf_flag::EIGHTBITALPHA ) ) != 0;
	const std::string szUnderMaterials = MaterialsPathOf( pDocument );
	CMakeMaterialDialog dialog( szUnderMaterials.empty() ? pDocument->GetGamePath() : QString::fromStdString( szUnderMaterials ), bAlpha, ( info.m_nFlags & xv::texture::vtf_flag::NORMAL ) != 0, SurfaceProps(), this );
	if ( dialog.exec() != QDialog::Accepted )
	{
		return nullptr;
	}
	QString szGamePath = pDocument->GetGamePath();
	szGamePath.chop( QFileInfo{ szGamePath }.suffix().size() );
	szGamePath += QStringLiteral( "vmt" );
	const fs::path texture = !pDocument->GetDiskPath().empty() ? pDocument->GetDiskPath() : pDocument->GetSuggestedPath();
	fs::path beside = texture;
	beside.replace_extension( ".vmt" );
	auto *pEditor = new CMaterialEditor( szGamePath, dialog.GetText(), ReaderFor( pTexture, texture.empty() ? fs::path{} : beside ) );
	pEditor->SetSuggestedPath( texture.empty() ? fs::path{} : beside );
	pEditor->MarkUnsaved();
	SetUpMaterial( pEditor );
	pTexture->SetMaterial( pEditor );
	pTexture->ShowMaterial();
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::WriteMaterial( CMaterialEditor *pEditor, const fs::path &target )
{
	std::error_code ec;
	fs::create_directories( target.parent_path(), ec );
	QSaveFile file( ToQString( target ) );
	const QByteArray bytes = pEditor->GetText().toUtf8();
	if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() || !file.commit() )
	{
		QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not write %1:\n%2" ).arg( ToQString( target ), file.errorString() ) );
		return false;
	}
	pEditor->SetDiskPath( target );
	pEditor->MarkSaved();
	statusBar()->showMessage( tr( "Saved %1" ).arg( ToQString( target ) ), 8000 );
	FilesWritten( { target } );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A material from a VPK is saved as a copy in the write folder; a new one asks where.
//-----------------------------------------------------------------------------
bool CMainWindow::Save( CMaterialEditor *pEditor )
{
	if ( pEditor->IsNew() || ( pEditor->GetDiskPath().empty() && !m_Game ) )
	{
		return SaveAs( pEditor );
	}
	if ( !WriteMaterial( pEditor, pEditor->GetDiskPath().empty() ? WriteTarget( pEditor->GetGamePath() ) : pEditor->GetDiskPath() ) )
	{
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::SaveAs( CMaterialEditor *pEditor )
{
	const fs::path current = !pEditor->GetDiskPath().empty() ? pEditor->GetDiskPath()
		: !pEditor->GetSuggestedPath().empty()               ? pEditor->GetSuggestedPath()
		: m_Game                                             ? WriteTarget( pEditor->GetGamePath() )
															 : ToPath( QDir::homePath() ) / xv::vfs::FromUtf8( pEditor->GetFileName().toStdString() );
	const QString szFile = AskSavePath( current, tr( "Materials (*.vmt)" ), nullptr, pEditor->GetSuggestedPath().parent_path() );
	if ( szFile.isEmpty() || !WriteMaterial( pEditor, ToPath( szFile ) ) )
	{
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::ConfirmClose( CMaterialEditor *pEditor )
{
	if ( !pEditor->IsModified() )
	{
		return true;
	}
	ShowMaterialEditor( pEditor );
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved changes" ), tr( "Save changes to %1?" ).arg( pEditor->GetFileName() ),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	if ( nAnswer == QMessageBox::Save )
	{
		return Save( pEditor );
	}
	return nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose: File > Convert a Folder of Textures: every picture into a VTF, or back.
//-----------------------------------------------------------------------------
void CMainWindow::ChooseBatchConvert()
{
	const QString szStart = !m_pFolders->GetFolder().isEmpty() ? m_pFolders->GetFolder() : m_Game ? ToQString( m_Game->m_GameDir / "materials" )
																								  : QDir::homePath();
	CBatchConvertDialog dialog( szStart, this );
	if ( dialog.exec() != QDialog::Accepted )
	{
		return;
	}
	const BatchJob_t job = dialog.GetJob();
	QProgressDialog progress( tr( "Converting…" ), tr( "Stop" ), 0, 0, this );
	progress.setWindowModality( Qt::WindowModal );
	progress.setMinimumDuration( 300 );
	const BatchResult_t result = RunBatch( job, [ &progress ]( int nDone, int nTotal ) {
		progress.setMaximum( nTotal );
		progress.setValue( nDone );
		QCoreApplication::processEvents();
		return !progress.wasCanceled();
	} );
	progress.close();
	QString szReport = tr( "Converted %n file(s) into %1.", nullptr, result.m_nDone ).arg( QDir::toNativeSeparators( job.m_szOutput ) );
	if ( !result.m_Failures.isEmpty() )
	{
		szReport += u'\n' + tr( "%n couldn't be:", nullptr, static_cast<int>( result.m_Failures.size() ) ) + u'\n' + result.m_Failures.mid( 0, 15 ).join( u'\n' );
	}
	QMessageBox::information( this, tr( "Convert a Folder" ), szReport );
	if ( m_Game )
	{
		Remount();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Find an open detail file by its disk identity or mounted game path.
//-----------------------------------------------------------------------------
CDetailEditor *CMainWindow::DetailEditorFor( const QString &szGamePath, const fs::path &disk ) const
{
	for ( int i = 0; i < m_pTabs->count(); ++i )
	{
		auto *pEditor = qobject_cast<CDetailEditor *>( m_pTabs->widget( i ) );
		if ( !pEditor )
		{
			continue;
		}
		const auto *pDocument = pEditor->GetDocument();
		if ( !disk.empty() && pDocument->GetDiskPath().lexically_normal() == disk.lexically_normal() )
		{
			return pEditor;
		}
		if ( disk.empty() && !szGamePath.isEmpty() && pDocument->GetGamePath().compare( szGamePath, Qt::CaseInsensitive ) == 0 )
		{
			return pEditor;
		}
	}
	return nullptr;
}
//-----------------------------------------------------------------------------
// Purpose: Return the active details tab for global save and undo actions.
//-----------------------------------------------------------------------------
CDetailEditor *CMainWindow::GetCurrentDetails() const
{
	return qobject_cast<CDetailEditor *>( m_pTabs->currentWidget() );
}
//-----------------------------------------------------------------------------
// Purpose: Open a detail file through the game's mount precedence.
//-----------------------------------------------------------------------------
CDetailEditor *CMainWindow::OpenDetailFile( const QString &szGamePath )
{
	const auto szPath = xv::vfs::NormalizePath( szGamePath.toStdString() );
	fs::path disk;
	if ( const auto *pMount = m_pFileSystem->Resolve( szPath ) )
	{
		disk = pMount->DiskPath( szPath ).value_or( fs::path{} );
	}
	if ( auto *pEditor = DetailEditorFor( QString::fromStdString( szPath ), disk ) )
	{
		m_pTabs->setCurrentWidget( pEditor );
		return pEditor;
	}
	const auto text = m_pFileSystem->ReadText( szPath );
	if ( !text )
	{
		QMessageBox::warning( this, tr( "Cannot open" ), tr( "Could not read %1." ).arg( szGamePath ) );
		return nullptr;
	}
	auto *pDocument = new CDetailDocument( QString::fromStdString( szPath ), xv::kv1::Parse( *text ) );
	pDocument->SetDiskPath( disk );
	return AddDetailEditor( pDocument );
}
//-----------------------------------------------------------------------------
// Purpose: Start a new detail file without writing until the user saves it.
//-----------------------------------------------------------------------------
CDetailEditor *CMainWindow::NewDetailFile()
{
	auto *pDocument = new CDetailDocument( QStringLiteral( "detail.vbsp" ), xv::kv1::Parse( "detail\n{\n}\n" ) );
	pDocument->MarkUnsaved();
	return AddDetailEditor( pDocument );
}
//-----------------------------------------------------------------------------
// Purpose: Resolve local assets beside a detail file before the mounted game's assets.
//-----------------------------------------------------------------------------
void CMainWindow::SetUpDetails( CDetailEditor *pEditor )
{
	const auto disk = pEditor->GetDocument()->GetDiskPath();
	const auto reader = [ this, disk ]( std::string_view szPath ) -> std::optional<std::vector<std::byte>> {
		const auto szGamePath = QString::fromUtf8( szPath.data(), static_cast<qsizetype>( szPath.size() ) );
		if ( auto *pTexture = TextureEditorFor( szGamePath ) )
		{
			return pTexture->GetDocument()->GetBytes();
		}
		if ( auto *pMaterial = MaterialEditorFor( szGamePath, {} ) )
		{
			const auto bytes = pMaterial->GetText().toUtf8();
			return std::vector<std::byte>( reinterpret_cast<const std::byte *>( bytes.constData() ), reinterpret_cast<const std::byte *>( bytes.constData() + bytes.size() ) );
		}
		return xv::material::DiskReader( disk.empty() ? fs::path{} : disk.parent_path() / "materials" / "detail.vmt", m_Game ? m_pFileSystem.get() : nullptr )( szPath );
	};
	if ( !m_Game && disk.empty() )
	{
		pEditor->SetContext( reader, {}, false );
		return;
	}
	std::vector<std::string> materials = m_Game ? m_pFileSystem->Find( "materials", ".vmt", true ) : std::vector<std::string>{};
	if ( !disk.empty() )
	{
		std::error_code ec;
		for ( fs::recursive_directory_iterator it( disk.parent_path() / "materials", fs::directory_options::skip_permission_denied, ec ), end; it != end && !ec; it.increment( ec ) )
		{
			if ( it->is_regular_file( ec ) && QString::fromStdString( xv::vfs::ToUtf8( it->path().extension() ) ).compare( u".vmt", Qt::CaseInsensitive ) == 0 )
			{
				materials.push_back( xv::vfs::NormalizePath( xv::vfs::ToUtf8( it->path().lexically_relative( disk.parent_path() ) ) ) );
			}
		}
	}
	std::ranges::sort( materials );
	materials.erase( std::unique( materials.begin(), materials.end() ), materials.end() );
	pEditor->SetContext( reader, std::move( materials ) );
}
//-----------------------------------------------------------------------------
// Purpose: Attach detail history, resource context and generated textures to the main window.
//-----------------------------------------------------------------------------
CDetailEditor *CMainWindow::AddDetailEditor( CDetailDocument *pDocument )
{
	auto *pEditor = new CDetailEditor( pDocument );
	connect( pEditor, &CDetailEditor::OpenModelRequested, this, &CMainWindow::OpenModelFile );
	connect( pEditor->GetPreview(), &CDetailPreview::OpenModelRequested, this, &CMainWindow::OpenModelFile );
	m_pUndoGroup->addStack( pDocument->GetUndoStack() );
	m_pTabs->addTab( pEditor, pDocument->GetFileName() );
	connect( pDocument, &CDetailDocument::ModifiedChanged, this, [ this, pEditor ] {
		const auto *pDoc = pEditor->GetDocument();
		const int i = m_pTabs->indexOf( pEditor );
		m_pTabs->setTabText( i, ( pDoc->IsModified() ? QStringLiteral( "*" ) : QString{} ) + pDoc->GetFileName() );
		m_pTabs->setTabToolTip( i, pDoc->GetDiskPath().empty() ? pDoc->GetGamePath() : ToQString( pDoc->GetDiskPath() ) );
		UpdateTitle();
	} );
	connect( pEditor, &CDetailEditor::OpenTextureRequested, this, [ this, pEditor ]( const QString &szPath ) {
		const auto disk = pEditor->GetDocument()->GetDiskPath();
		if ( !disk.empty() )
		{
			const auto local = xv::vfs::FindCaseInsensitive( disk.parent_path() / xv::vfs::FromUtf8( szPath.toStdString() ) );
			std::error_code ec;
			if ( fs::is_regular_file( local, ec ) )
			{
				OpenDiskFile( ToQString( local ) );
				return;
			}
		}
		OpenTextureFile( szPath );
	} );
	connect( pEditor, &CDetailEditor::PackedTexture, this, [ this ]( const QString &szMaterial, const std::vector<std::byte> &bytes ) {
		const QString szTexture = QStringLiteral( "materials/%1.vtf" ).arg( szMaterial );
		auto *pTextureDocument = new CTextureDocument( szTexture, bytes );
		pTextureDocument->MarkUnsaved();
		if ( m_Game )
		{
			pTextureDocument->SetSuggestedPath( WriteTarget( szTexture ) );
		}
		auto *pTexture = AddTextureEditor( pTextureDocument, false );
		xv::kv1::CDocument document;
		auto root = xv::kv1::CNode::MakeBlock( "UnlitGeneric" );
		root.AppendChild( xv::kv1::CNode::MakePair( "$basetexture", szMaterial.toStdString() ) );
		root.AppendChild( xv::kv1::CNode::MakePair( "$vertexcolor", "1" ) );
		root.AppendChild( xv::kv1::CNode::MakePair( "$nocull", "1" ) );
		root.AppendChild( xv::kv1::CNode::MakePair( "$translucent", "1" ) );
		document.AppendRoot( std::move( root ) );
		auto *pMaterial = new CMaterialEditor( QStringLiteral( "materials/%1.vmt" ).arg( szMaterial ), QString::fromStdString( xv::kv1::Write( document ) ), ReaderFor( pTexture, {} ) );
		pMaterial->MarkUnsaved();
		if ( m_Game )
		{
			pMaterial->SetSuggestedPath( WriteTarget( pMaterial->GetGamePath() ) );
		}
		SetUpMaterial( pMaterial );
		pTexture->SetMaterial( pMaterial );
		statusBar()->showMessage( tr( "Save the new texture and material, then set the map's detailmaterial to %1." ).arg( szMaterial ), 15000 );
	} );
	connect( pEditor, &CDetailEditor::RefreshRequested, this, [ this, pEditor ] { SetUpDetails( pEditor ); } );
	SetUpDetails( pEditor );
	if ( pDocument->IsNew() )
	{
		pDocument->MarkUnsaved();
	}
	m_pTabs->setCurrentWidget( pEditor );
	return pEditor;
}
//-----------------------------------------------------------------------------
// Purpose: Atomically save original detail tokens to the chosen loose file.
//-----------------------------------------------------------------------------
bool CMainWindow::WriteDetail( CDetailEditor *pEditor, const fs::path &target )
{
	std::error_code ec;
	fs::create_directories( target.parent_path(), ec );
	QSaveFile file( ToQString( target ) );
	const auto bytes = pEditor->GetDocument()->GetSerialize();
	if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes.data(), static_cast<qint64>( bytes.size() ) ) != static_cast<qint64>( bytes.size() ) || !file.commit() )
	{
		QMessageBox::critical( this, tr( "Save failed" ), file.errorString() );
		return false;
	}
	pEditor->GetDocument()->SetDiskPath( target );
	pEditor->GetDocument()->SetGamePath( GamePathForDisk( target ) );
	pEditor->GetDocument()->MarkSaved();
	FilesWritten( { target } );
	SetUpDetails( pEditor );
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Save loose files in place and VPK files to the configured write folder.
//-----------------------------------------------------------------------------
bool CMainWindow::Save( CDetailEditor *pEditor )
{
	const auto *pDocument = pEditor->GetDocument();
	if ( pDocument->IsNew() || ( pDocument->GetDiskPath().empty() && !m_Game ) )
	{
		return SaveAs( pEditor );
	}
	return WriteDetail( pEditor, pDocument->GetDiskPath().empty() ? WriteTarget( pDocument->GetGamePath() ) : pDocument->GetDiskPath() );
}
//-----------------------------------------------------------------------------
// Purpose: Choose the destination for a new or copied detail file.
//-----------------------------------------------------------------------------
bool CMainWindow::SaveAs( CDetailEditor *pEditor )
{
	const auto *pDocument = pEditor->GetDocument();
	const fs::path start = !pDocument->GetDiskPath().empty() ? pDocument->GetDiskPath() : m_Game ? WriteTarget( pDocument->GetGamePath() )
																								 : ToPath( QDir::homePath() ) / "detail.vbsp";
	const QString szPath = AskSavePath( start, tr( "Detail files (*.vbsp);;All files (*)" ) );
	return !szPath.isEmpty() && WriteDetail( pEditor, ToPath( szPath ) );
}
//-----------------------------------------------------------------------------
// Purpose: Protect unsaved detail edits when closing a tab or the application.
//-----------------------------------------------------------------------------
bool CMainWindow::ConfirmClose( CDetailEditor *pEditor )
{
	if ( !pEditor->GetDocument()->IsModified() )
	{
		return true;
	}
	m_pTabs->setCurrentWidget( pEditor );
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved changes" ), tr( "Save changes to %1?" ).arg( pEditor->GetDocument()->GetFileName() ), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	return nAnswer == QMessageBox::Save ? Save( pEditor ) : nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose: Adds a fully loaded read-only studio viewer tab.
//-----------------------------------------------------------------------------
CModelEditor *CMainWindow::AddModelEditor( const QString &szPath, xv::material::Reader_t pfnRead, const QString &szOrigin )
{
	auto *pEditor = new CModelEditor( szPath, std::move( pfnRead ) );
	if ( !pEditor->IsValid() )
	{
		QMessageBox::warning( this, tr( "Cannot open model" ), pEditor->GetError() );
		delete pEditor;
		return nullptr;
	}
	pEditor->SetOrigin( szOrigin );
	connect( pEditor, &CModelEditor::WorkflowRequested, this, [ this, pEditor ] {if(auto *p=pEditor->CreateWorkflow(m_Game)){p->setAttribute(Qt::WA_DeleteOnClose);p->setWindowModality(Qt::WindowModal);connect(p,&CModelWorkflow::OpenModelRequested,this,&CMainWindow::OpenModelFile);p->show();} } );
	connect( pEditor, &CModelEditor::OpenMaterialRequested, this, [ this, pEditor ]( const QString &szMaterial ) {
		if ( !pEditor->GetOrigin().isEmpty() && !QFileInfo( pEditor->GetOrigin() ).isAbsolute() )
		{
			OpenMaterialFile( szMaterial );
			return;
		}
		if ( QFileInfo( pEditor->GetOrigin() ).isAbsolute() )
			for ( fs::path dir = ToPath( pEditor->GetOrigin() ).parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path() )
				if ( ToQString( dir.filename() ).compare( u"models", Qt::CaseInsensitive ) == 0 )
				{
					const auto file = xv::vfs::FindCaseInsensitive( dir.parent_path() / ToPath( szMaterial ) );
					if ( fs::is_regular_file( file ) )
					{
						OpenDiskFile( ToQString( file ) );
						return;
					}
					break;
				}
		const auto bytes = pEditor->ReadFile( szMaterial.toStdString() );
		if ( !bytes )
			return;
		const QPointer<CModelEditor> pSource = pEditor;
		auto *pMaterial = new CMaterialEditor( szMaterial, QString::fromUtf8( reinterpret_cast<const char *>( bytes->data() ), bytes->size() ), [ this, pSource ]( std::string_view szFile ) -> std::optional<std::vector<std::byte>> { return pSource ? pSource->ReadFile( szFile ) : m_Game ? m_pFileSystem->Read( szFile )
																																																																							  : std::nullopt; } );
		pMaterial->MarkUnsaved();
		SetUpMaterial( pMaterial );
		AddMaterialEditor( pMaterial );
	} );
	const int nIndex = m_pTabs->addTab( pEditor, QFileInfo( szPath ).fileName() );
	m_pTabs->setTabToolTip( nIndex, szOrigin.isEmpty() ? szPath : szOrigin );
	m_pUndoGroup->addStack( pEditor->GetDocument()->GetUndoStack() );
	connect( pEditor, &CModelEditor::ModifiedChanged, this, [ this, pEditor ]( bool bModified ) {
		m_pTabs->setTabText( m_pTabs->indexOf( pEditor ), ( bModified ? QStringLiteral( "*" ) : QString{} ) + QFileInfo( pEditor->GetPath() ).fileName() );
		UpdateTitle();
	} );
	QStringList surfaces;
	for ( const Choice_t &choice : SurfaceProps() )
	{
		surfaces << choice.m_szValue;
	}
	pEditor->SetSurfaceProps( surfaces );
	m_pTabs->setCurrentIndex( nIndex );
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::ConfirmClose( CModelEditor *pEditor )
{
	if ( !pEditor->IsModified() )
	{
		return true;
	}
	m_pTabs->setCurrentWidget( pEditor );
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved changes" ), tr( "Save changes to %1?" ).arg( QFileInfo( pEditor->GetPath() ).fileName() ),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save );
	return nAnswer == QMessageBox::Save ? Save( pEditor ) : nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose: A model from disk is saved where it is; one from the game (a VPK) as copies in the
//          write folder, which the game reads first. Only the changed files are written; the
//          others still match them (the checksum doesn't change).
//-----------------------------------------------------------------------------
bool CMainWindow::Save( CModelEditor *pEditor )
{
	const QString &szOrigin = pEditor->GetOrigin();
	if ( szOrigin.isEmpty() || ( !QFileInfo( szOrigin ).isAbsolute() && !m_Game ) )
	{
		return SaveAs( pEditor );
	}
	const fs::path mdl = QFileInfo( szOrigin ).isAbsolute() ? ToPath( szOrigin ) : WriteTarget( szOrigin );
	return WriteModel( pEditor, mdl, false );
}

//-----------------------------------------------------------------------------
// Purpose: All the files go beside the new .mdl, named after it.
//-----------------------------------------------------------------------------
bool CMainWindow::SaveAs( CModelEditor *pEditor )
{
	const QString &szOrigin = pEditor->GetOrigin();
	const fs::path current = QFileInfo( szOrigin ).isAbsolute() ? ToPath( szOrigin )
		: m_Game                                                ? WriteTarget( pEditor->GetPath() )
																: ToPath( QDir::homePath() ) / xv::vfs::FromUtf8( QFileInfo( pEditor->GetPath() ).fileName().toStdString() );
	const QString szFile = AskSavePath( current, tr( "Models (*.mdl)" ) );
	if ( szFile.isEmpty() )
	{
		return false;
	}
	return WriteModel( pEditor, ToPath( szFile ), true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMainWindow::WriteModel( CModelEditor *pEditor, const fs::path &mdl, bool bAll )
{
	CModelDocument *pDocument = pEditor->GetDocument();
	fs::path target = mdl;
	if ( target.extension() != ".mdl" )
	{
		target += ".mdl";
	}
	const fs::path stem = target.parent_path() / target.stem();
	// A model saved under a new name points its animation blocks at the new .ani.
	if ( bAll && pDocument->Has( ".ani" ) )
	{
		fs::path root;
		for ( fs::path dir = target.parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path() )
		{
			if ( ToQString( dir.filename() ).compare( u"models", Qt::CaseInsensitive ) == 0 )
			{
				root = dir.parent_path();
				break;
			}
		}
		const fs::path ani = stem.string() + ".ani";
		if ( !root.empty() )
		{
			const std::string szAni = xv::vfs::NormalizePath( xv::vfs::ToUtf8( ani.lexically_relative( root ) ) );
			const QString szError = pDocument->Edit( tr( "Rename Animation File" ), [ szAni ]( xv::model::Files_t &files ) { xv::model::SetAnimationFile( files.at( ".mdl" ), szAni ); } );
			if ( !szError.isEmpty() )
			{
				QMessageBox::critical( this, tr( "Save failed" ), szError );
				return false;
			}
		}
		else
		{
			QMessageBox::warning( this, tr( "Animation file" ), tr( "The model isn't being saved in a models folder, so its animation blocks still name %1." ).arg( QString::fromStdString( pDocument->PathOf( ".ani" ) ) ) );
		}
	}
	if ( const QString szProblem = pDocument->Check(); !szProblem.isEmpty() )
	{
		QMessageBox::critical( this, tr( "Save refused" ), szProblem );
		return false;
	}
	std::vector<std::string> kinds;
	if ( bAll )
	{
		for ( const auto &[ szKind, bytes ] : pDocument->GetFiles() )
		{
			kinds.push_back( szKind );
		}
	}
	else
	{
		kinds = pDocument->GetModifiedKinds();
	}
	std::vector<fs::path> written;
	std::error_code ec;
	fs::create_directories( target.parent_path(), ec );
	for ( const std::string &szKind : kinds )
	{
		// In place, the .ani keeps its own name; everything else is named after the .mdl.
		fs::path file = stem.string() + szKind;
		if ( szKind == ".ani" && !bAll )
		{
			file = target.parent_path() / xv::vfs::FromUtf8( fs::path( pDocument->PathOf( ".ani" ) ).filename().string() );
		}
		const auto &bytes = pDocument->GetFiles().at( szKind );
		QSaveFile out( ToQString( file ) );
		if ( !out.open( QIODevice::WriteOnly ) || out.write( reinterpret_cast<const char *>( bytes.data() ), static_cast<qint64>( bytes.size() ) ) != static_cast<qint64>( bytes.size() ) || !out.commit() )
		{
			QMessageBox::critical( this, tr( "Save failed" ), tr( "Could not write %1:\n%2" ).arg( ToQString( file ), out.errorString() ) );
			return false;
		}
		written.push_back( file );
	}
	pDocument->MarkSaved();
	if ( bAll )
	{
		pEditor->SetOrigin( ToQString( target ) );
		m_pTabs->setTabToolTip( m_pTabs->indexOf( pEditor ), ToQString( target ) );
	}
	statusBar()->showMessage( tr( "Saved %1 (%2 files)" ).arg( ToQString( target ) ).arg( written.size() ), 8000 );
	FilesWritten( written );
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: One browser for the game; asking again goes back to it.
//-----------------------------------------------------------------------------
CModelBrowser *CMainWindow::OpenModelBrowser()
{
	if ( !m_Game )
	{
		QMessageBox::information( this, tr( "Models" ), tr( "The model browser shows a game's models; open the game first." ) );
		return nullptr;
	}
	for ( int i = 0; i < m_pTabs->count(); i++ )
		if ( auto *p = qobject_cast<CModelBrowser *>( m_pTabs->widget( i ) ) )
		{
			m_pTabs->setCurrentWidget( p );
			return p;
		}
	auto *pBrowser = new CModelBrowser( *m_Game );
	pBrowser->SetDetailsReader( [ this ]( std::string_view szPath ) { return m_pFileSystem->Read( szPath ); }, [ this ]( std::string_view szPath ) {const auto *p=m_pFileSystem->Resolve(szPath);return p?ToQString(p->GetLocation()):QString{}; } );
	connect( pBrowser, &CModelBrowser::OpenMaterialRequested, this, &CMainWindow::OpenMaterialFile );
	connect( pBrowser, &CModelBrowser::ShowInGameBrowser, this, [ this ]( const QString &szPath ) {
		if ( auto *pSearch = findChild<QLineEdit *>( QStringLiteral( "gameFileSearch" ) ) )
			pSearch->clear();
		const auto index = m_pFilter->mapFromSource( m_pModel->IndexForPath( szPath.toStdString() ) );
		if ( index.isValid() )
		{
			m_pSides->setCurrentWidget( m_pGameBrowser );
			m_pTree->setCurrentIndex( index );
			m_pTree->scrollTo( index );
		}
	} );
	connect( pBrowser, &CModelBrowser::OpenModelRequested, this, &CMainWindow::OpenModelFile );
	m_pTabs->setCurrentIndex( m_pTabs->addTab( pBrowser, tr( "Models" ) ) );
	return pBrowser;
}

//-----------------------------------------------------------------------------
// Purpose: The mounted game's .mdl a model's other file belongs to; empty when it has none.
//-----------------------------------------------------------------------------
QString CMainWindow::GameModelForCompanion( const QString &szPath ) const
{
	return ModelForCompanion( szPath, [ this ]( const QString &szMdl ) {
		const std::string szGamePath = xv::vfs::NormalizePath( szMdl.toStdString() );
		return m_Game && m_pFileSystem->Resolve( szGamePath ) ? QString::fromStdString( szGamePath ) : QString{};
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Resolves a studio model from the game or disk with its own companions taking priority.
//-----------------------------------------------------------------------------
CModelEditor *CMainWindow::OpenModelFile( const QString &szPath )
{
	const bool bDisk = QFileInfo( szPath ).isAbsolute();
	const QString szOrigin = bDisk ? QFileInfo( szPath ).absoluteFilePath() : QString::fromStdString( xv::vfs::NormalizePath( szPath.toStdString() ) );
	for ( int i = 0; i < m_pTabs->count(); i++ )
		if ( auto *p = qobject_cast<CModelEditor *>( m_pTabs->widget( i ) ); p && p->GetOrigin() == szOrigin )
		{
			m_pTabs->setCurrentWidget( p );
			return p;
		}
	if ( !bDisk )
		return m_Game ? AddModelEditor( szOrigin, [ this ]( std::string_view szFile ) { return m_pFileSystem->Read( szFile ); }, szOrigin ) : nullptr;
	const fs::path file = ToPath( szOrigin );
	fs::path root;
	for ( fs::path dir = file.parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path() )
		if ( QString::fromStdString( xv::vfs::ToUtf8( dir.filename() ) ).compare( u"models", Qt::CaseInsensitive ) == 0 )
		{
			root = dir.parent_path();
			break;
		}
	const QString szGamePath = root.empty() ? QFileInfo( szOrigin ).fileName() : ToQString( file.lexically_relative( root ) );
	return AddModelEditor( szGamePath, [ this, file, root, szGamePath ]( std::string_view szFile ) -> std::optional<std::vector<std::byte>> {
		fs::path candidate;
		const std::string szNormalized=xv::vfs::NormalizePath(szFile);
		if(!root.empty())candidate=root/xv::vfs::FromUtf8(szNormalized);
		else if(szNormalized.find('/')==std::string::npos)candidate=file.parent_path()/xv::vfs::FromUtf8(szNormalized);
		if(!candidate.empty())if(auto bytes=ReadDiskFile(ToQString(xv::vfs::FindCaseInsensitive(candidate)))){const auto data=AsBytes(*bytes);return std::vector<std::byte>(data.begin(),data.end());}
		return m_Game?m_pFileSystem->Read(szFile):std::nullopt; }, szOrigin );
}

//-----------------------------------------------------------------------------
// Purpose: A map from disk (an absolute path) or from the game. Its materials, models and
//          game data come from the mounted game, if any.
//-----------------------------------------------------------------------------
CMapEditor *CMainWindow::OpenMapFile( const QString &szPath )
{
	const bool bDisk = QFileInfo( szPath ).isAbsolute();
	const QString szOrigin = bDisk ? QFileInfo( szPath ).absoluteFilePath() : QString::fromStdString( xv::vfs::NormalizePath( szPath.toStdString() ) );
	for ( int i = 0; i < m_pTabs->count(); i++ )
	{
		if ( auto *pMap = qobject_cast<CMapEditor *>( m_pTabs->widget( i ) ); pMap && pMap->GetPath() == szOrigin )
		{
			m_pTabs->setCurrentWidget( pMap );
			return pMap;
		}
	}
	CMapDocument::Source_t source;
	source.m_szName = QFileInfo( szOrigin ).fileName();
	if ( bDisk )
	{
		const auto data = ReadDiskFile( szOrigin );
		if ( !data )
		{
			QMessageBox::warning( this, tr( "Cannot open" ), tr( "Could not read %1." ).arg( QDir::toNativeSeparators( szOrigin ) ) );
			return nullptr;
		}
		source.m_Text.assign( data->begin(), data->end() );
		source.m_DiskPath = ToPath( szOrigin );
	}
	else
	{
		const auto bytes = m_Game ? m_pFileSystem->Read( szOrigin.toStdString() ) : std::nullopt;
		if ( !bytes )
		{
			QMessageBox::warning( this, tr( "Cannot open" ), tr( "Could not read %1." ).arg( szOrigin ) );
			return nullptr;
		}
		source.m_Text.assign( reinterpret_cast<const char *>( bytes->data() ), reinterpret_cast<const char *>( bytes->data() + bytes->size() ) );
	}
	source.m_Game = m_Game;
	// Preferences > Maps can name the game data in place of the game's Hammer configuration.
	for ( const QString &szFgd : CPreferences::Instance().GetMapPreferences().m_Fgds )
	{
		source.m_Fgds.push_back( ToPath( szFgd ) );
	}
	auto *pEditor = new CMapEditor( szOrigin, !bDisk );
	AddMapTab( pEditor );
	pEditor->Load( std::move( source ) );
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose: File > New > Map: a map with nothing in it yet, for the mounted game, in a tab of its
//          own; saving it asks where it goes.
//-----------------------------------------------------------------------------
CMapEditor *CMainWindow::NewMap()
{
	CMapDocument::Source_t source;
	const std::string szText = xv::map::NewVmf();
	source.m_Text.assign( szText.begin(), szText.end() );
	source.m_Game = m_Game;
	for ( const QString &szFgd : CPreferences::Instance().GetMapPreferences().m_Fgds )
	{
		source.m_Fgds.push_back( ToPath( szFgd ) );
	}
	auto *pEditor = new CMapEditor( QString{}, false );
	source.m_szName = pEditor->GetTabTitle();
	AddMapTab( pEditor );
	pEditor->Load( std::move( source ) );
	return pEditor;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapEditor *CMainWindow::GetCurrentMap() const
{
	return qobject_cast<CMapEditor *>( m_pTabs->currentWidget() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMainWindow::UpdateMapMenu()
{
	if ( m_pMapMenu )
	{
		m_pMapMenu->menuAction()->setVisible( GetCurrentMap() != nullptr );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A map tab added and shown, its messages, undo and title joined to the window's.
//-----------------------------------------------------------------------------
void CMainWindow::AddMapTab( CMapEditor *pEditor )
{
	connect( pEditor, &CMapEditor::OpenInstanceRequested, this, [ this ]( const QString &szPath ) { OpenMapFile( szPath ); } );
	connect( &pEditor->GetDocument(), &CMapDocument::Saved, this, [ this ]( const QString &szPath ) {
		for ( int n = 0; n < m_pTabs->count(); ++n )
			if ( auto *pMap = qobject_cast<CMapEditor *>( m_pTabs->widget( n ) ) )
				pMap->GetDocument().RefreshInstancesUsing( std::filesystem::path{ szPath.toStdString() } );
	} );
	connect( pEditor, &CMapEditor::StatusMessage, this, [ this ]( const QString &szText ) { statusBar()->showMessage( szText, 8000 ); } );
	m_pUndoGroup->addStack( pEditor->GetUndoStack() );
	connect( pEditor, &CMapEditor::TitleChanged, this, [ this, pEditor ] {
		const int nTab = m_pTabs->indexOf( pEditor );
		m_pTabs->setTabText( nTab, pEditor->GetTabTitle() );
		m_pTabs->setTabToolTip( nTab, pEditor->IsFromGame() ? pEditor->GetPath() : QDir::toNativeSeparators( pEditor->GetPath() ) );
		UpdateTitle();
		SaveSession();
	} );
	const int nIndex = m_pTabs->addTab( pEditor, pEditor->GetTabTitle() );
	m_pTabs->setTabToolTip( nIndex, pEditor->IsFromGame() ? pEditor->GetPath() : QDir::toNativeSeparators( pEditor->GetPath() ) );
	m_pTabs->setCurrentIndex( nIndex );
}
