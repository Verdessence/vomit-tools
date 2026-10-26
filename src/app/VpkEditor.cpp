//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See VpkEditor.h.
//
//===========================================================================//
#include "VpkEditor.h"

#include <algorithm>
#include <unordered_map>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTabWidget>
#include <QStyle>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include "BspViews.h"
#include "FileActions.h"
#include "FileDialogs.h"
#include "FileViews.h"
#include "MaterialView.h"
#include "QtPaths.h"
#include "bsp/Bsp.h"
#include "vfs/Package.h"
#include "vfs/Vfs.h"

namespace
{

constexpr auto EXPORT_FOLDER_KEY = "vpkExportFolder";

enum Column_t
{
	COLUMN_NAME,
	COLUMN_SIZE,
	COLUMN_STORED,
	COLUMN_COUNT
};

enum Role_t
{
	ROLE_PATH = Qt::UserRole + 1, // a file's path in the archive, or a folder's (no trailing slash)
	ROLE_IS_FOLDER,
	ROLE_SIZE,
	ROLE_ENTRY // a file's PackageEntry_t, as a quintptr
};

//-----------------------------------------------------------------------------
// Purpose: Where a file's data sits, for the Stored In column: the archive itself, or one of a
//          VPK's numbered parts (pak01_003.vpk). A path the archive holds twice says so.
//-----------------------------------------------------------------------------
QString StoredIn( const xv::vfs::CPackage &package, const xv::vfs::PackageEntry_t &entry )
{
	const QString szFile = ToQString( package.GetFile().filename() );
	if ( entry.m_nArchive < 0 )
	{
		return entry.m_bDuplicate ? QObject::tr( "%1 (path packed twice)" ).arg( szFile ) : szFile;
	}
	QString szPart = szFile;
	szPart.replace( QRegularExpression( QStringLiteral( "_dir\\.vpk$" ), QRegularExpression::CaseInsensitiveOption ), QStringLiteral( "_%1.vpk" ).arg( entry.m_nArchive, 3, 10, QLatin1Char( '0' ) ) );
	return szPart;
}

} // namespace

//-----------------------------------------------------------------------------
// The archive as a folder tree. Built once; archives aren't changed here.
//-----------------------------------------------------------------------------
class CPackageModel : public QAbstractItemModel
{
public:
	explicit CPackageModel( const xv::vfs::CPackage &package, QObject *pParent )
		: QAbstractItemModel( pParent ),
		  m_Package( package ),
		  m_pRoot( std::make_unique<Node_t>() )
	{
		std::unordered_map<std::string, Node_t *> folders{ { "", m_pRoot.get() } };
		for ( const auto &entry : package.GetEntries() )
		{
			const std::string &szPath = entry.m_szPath;
			Node_t *pParent = m_pRoot.get();
			std::size_t nStart = 0;
			for ( auto nSlash = szPath.find( '/' ); nSlash != std::string::npos; nSlash = szPath.find( '/', nStart ) )
			{
				auto [ it, bInserted ] = folders.try_emplace( szPath.substr( 0, nSlash ), nullptr );
				if ( bInserted )
				{
					auto pNode = std::make_unique<Node_t>();
					pNode->m_szName = QString::fromStdString( szPath.substr( nStart, nSlash - nStart ) );
					pNode->m_szPath = QString::fromStdString( it->first );
					pNode->m_pParent = pParent;
					it->second = pParent->m_Children.emplace_back( std::move( pNode ) ).get();
				}
				pParent = it->second;
				nStart = nSlash + 1;
			}
			auto pFile = std::make_unique<Node_t>();
			pFile->m_szName = QString::fromStdString( szPath.substr( nStart ) );
			pFile->m_szPath = QString::fromStdString( szPath );
			pFile->m_pEntry = &entry;
			pFile->m_pParent = pParent;
			pParent->m_Children.push_back( std::move( pFile ) );
		}
		// Folder sizes are their files' sizes added up; rows are recorded for index().
		const auto arrange = []( auto &self, Node_t &node ) -> std::uint64_t {
			std::uint64_t nTotal = node.m_pEntry ? node.m_pEntry->m_nSize : 0;
			for ( int nRow = 0; nRow < static_cast<int>( node.m_Children.size() ); nRow++ )
			{
				Node_t &child = *node.m_Children[ static_cast<std::size_t>( nRow ) ];
				child.m_nRow = nRow;
				nTotal += self( self, child );
			}
			node.m_nSize = nTotal;
			return nTotal;
		};
		arrange( arrange, *m_pRoot );
	}

	[[nodiscard]] QModelIndex IndexForPath( const QString &szPath ) const
	{
		const Node_t *pNode = m_pRoot.get();
		for ( const QString &szPart : szPath.split( u'/', Qt::SkipEmptyParts ) )
		{
			const auto it = std::ranges::find_if( pNode->m_Children, [ &szPart ]( const auto &child ) { return child->m_szName.compare( szPart, Qt::CaseInsensitive ) == 0; } );
			if ( it == pNode->m_Children.end() )
			{
				return {};
			}
			pNode = it->get();
		}
		return pNode == m_pRoot.get() ? QModelIndex{} : createIndex( pNode->m_nRow, 0, const_cast<Node_t *>( pNode ) );
	}

	[[nodiscard]] QModelIndex index( int nRow, int nColumn, const QModelIndex &parent = {} ) const override
	{
		const Node_t *pParent = NodeAt( parent );
		if ( nRow < 0 || nRow >= static_cast<int>( pParent->m_Children.size() ) || nColumn < 0 || nColumn >= COLUMN_COUNT )
		{
			return {};
		}
		return createIndex( nRow, nColumn, pParent->m_Children[ static_cast<std::size_t>( nRow ) ].get() );
	}

	[[nodiscard]] QModelIndex parent( const QModelIndex &child ) const override
	{
		const Node_t *pNode = child.isValid() ? static_cast<Node_t *>( child.internalPointer() ) : nullptr;
		if ( !pNode || pNode->m_pParent == m_pRoot.get() )
		{
			return {};
		}
		return createIndex( pNode->m_pParent->m_nRow, 0, pNode->m_pParent );
	}

	[[nodiscard]] int rowCount( const QModelIndex &parent = {} ) const override
	{
		return parent.column() > 0 ? 0 : static_cast<int>( NodeAt( parent )->m_Children.size() );
	}

	[[nodiscard]] int columnCount( const QModelIndex & = {} ) const override
	{
		return COLUMN_COUNT;
	}

	[[nodiscard]] QVariant data( const QModelIndex &index, int nRole = Qt::DisplayRole ) const override
	{
		if ( !index.isValid() )
		{
			return {};
		}
		const Node_t *pNode = static_cast<Node_t *>( index.internalPointer() );
		switch ( nRole )
		{
			case ROLE_PATH:
				return pNode->m_szPath;
			case ROLE_IS_FOLDER:
				return !pNode->m_pEntry;
			case ROLE_SIZE:
				return QVariant::fromValue<qulonglong>( pNode->m_nSize );
			case ROLE_ENTRY:
				return QVariant::fromValue( reinterpret_cast<quintptr>( pNode->m_pEntry ) );
			case Qt::DecorationRole:
				if ( index.column() == COLUMN_NAME )
				{
					return QApplication::style()->standardIcon( pNode->m_pEntry ? QStyle::SP_FileIcon : QStyle::SP_DirIcon );
				}
				return {};
			case Qt::TextAlignmentRole:
				return index.column() == COLUMN_SIZE ? QVariant( Qt::AlignRight | Qt::AlignVCenter ) : QVariant{};
			case Qt::ToolTipRole:
				return pNode->m_szPath;
			case Qt::DisplayRole:
				switch ( index.column() )
				{
					case COLUMN_NAME:
						return pNode->m_szName;
					case COLUMN_SIZE:
						return QLocale{}.formattedDataSize( static_cast<qint64>( pNode->m_nSize ) );
					case COLUMN_STORED:
						return pNode->m_pEntry ? StoredIn( m_Package, *pNode->m_pEntry ) : QString{};
					default:
						return {};
				}
			default:
				return {};
		}
	}

	[[nodiscard]] QVariant headerData( int nSection, Qt::Orientation nOrientation, int nRole = Qt::DisplayRole ) const override
	{
		if ( nOrientation != Qt::Horizontal || nRole != Qt::DisplayRole )
		{
			return {};
		}
		switch ( nSection )
		{
			case COLUMN_NAME:
				return QObject::tr( "Name" );
			case COLUMN_SIZE:
				return QObject::tr( "Size" );
			case COLUMN_STORED:
				return QObject::tr( "Stored In" );
			default:
				return {};
		}
	}

private:
	struct Node_t
	{
		QString m_szName;
		QString m_szPath;
		const xv::vfs::PackageEntry_t *m_pEntry = nullptr; // null for folders
		std::uint64_t m_nSize = 0;
		Node_t *m_pParent = nullptr;
		int m_nRow = 0;
		std::vector<std::unique_ptr<Node_t>> m_Children;
	};

	[[nodiscard]] const Node_t *NodeAt( const QModelIndex &index ) const
	{
		return index.isValid() ? static_cast<const Node_t *>( index.internalPointer() ) : m_pRoot.get();
	}

	const xv::vfs::CPackage &m_Package;
	std::unique_ptr<Node_t> m_pRoot;
};

//-----------------------------------------------------------------------------
// The search over full paths, and sorting with folders first.
//-----------------------------------------------------------------------------
class CPackageFilter : public QSortFilterProxyModel
{
public:
	using QSortFilterProxyModel::QSortFilterProxyModel;

	// Text without * or ? is looked for anywhere in a path; a pattern must match a whole path,
	// with a * put in front so "skybox/*.vtf" finds materials/skybox/sky_dayrt.vtf.
	void SetSearch( const QString &szSearch )
	{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 10, 0 )
		beginFilterChange();
#endif
		m_szSearch = szSearch.trimmed();
		m_bPattern = m_szSearch.contains( u'*' ) || m_szSearch.contains( u'?' );
		if ( m_bPattern )
		{
			const QString szPattern = m_szSearch.startsWith( u'*' ) ? m_szSearch : u'*' + m_szSearch;
			m_Pattern = QRegularExpression( QRegularExpression::wildcardToRegularExpression( szPattern, QRegularExpression::NonPathWildcardConversion ), QRegularExpression::CaseInsensitiveOption );
		}
#if QT_VERSION >= QT_VERSION_CHECK( 6, 10, 0 )
		endFilterChange( QSortFilterProxyModel::Direction::Rows );
#else
		invalidateRowsFilter(); // CI builds on Qt 6.8, before begin/endFilterChange
#endif
	}
	[[nodiscard]] const QString &GetSearch() const { return m_szSearch; }

	[[nodiscard]] bool Matches( const QString &szPath ) const
	{
		if ( m_szSearch.isEmpty() )
		{
			return true;
		}
		return m_bPattern ? m_Pattern.match( szPath ).hasMatch() : szPath.contains( m_szSearch, Qt::CaseInsensitive );
	}

protected:
	[[nodiscard]] bool filterAcceptsRow( int nRow, const QModelIndex &parent ) const override
	{
		if ( m_szSearch.isEmpty() )
		{
			return true;
		}
		const QModelIndex index = sourceModel()->index( nRow, COLUMN_NAME, parent );
		// Folders show when something in them matches (recursive filtering), not for their own name.
		return !index.data( ROLE_IS_FOLDER ).toBool() && Matches( index.data( ROLE_PATH ).toString() );
	}

	[[nodiscard]] bool lessThan( const QModelIndex &left, const QModelIndex &right ) const override
	{
		const bool bLeftFolder = left.data( ROLE_IS_FOLDER ).toBool();
		const bool bRightFolder = right.data( ROLE_IS_FOLDER ).toBool();
		if ( bLeftFolder != bRightFolder )
		{
			// Folders stay on top whichever way the column is sorted.
			return ( sortOrder() == Qt::AscendingOrder ) == bLeftFolder;
		}
		if ( left.column() == COLUMN_SIZE )
		{
			return left.data( ROLE_SIZE ).toULongLong() < right.data( ROLE_SIZE ).toULongLong();
		}
		return QString::localeAwareCompare( left.data().toString(), right.data().toString() ) < 0;
	}

private:
	QString m_szSearch;
	bool m_bPattern = false;
	QRegularExpression m_Pattern;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVpkEditor::CVpkEditor( std::unique_ptr<xv::vfs::CPackage> pPackage, CSoundPlayer *pPlayer, xv::material::Reader_t pfnGame, QWidget *pParent )
	: QWidget( pParent ),
	  m_pPackage( std::move( pPackage ) ),
	  m_pPlayer( pPlayer )
{
	m_pfnRead = [ this, pfnGame ]( std::string_view szPath ) -> std::optional<std::vector<std::byte>> {
		if ( auto bytes = m_pPackage->Read( szPath ) )
		{
			return bytes;
		}
		return pfnGame ? pfnGame( szPath ) : std::nullopt;
	};

	m_pModel = new CPackageModel( *m_pPackage, this );
	m_pFilter = new CPackageFilter( this );
	m_pFilter->setSourceModel( m_pModel );
	m_pFilter->setRecursiveFilteringEnabled( true );
	m_pFilter->setSortCaseSensitivity( Qt::CaseInsensitive );

	m_pSearch = new QLineEdit;
	m_pSearch->setPlaceholderText( tr( "Search files… (part of a path, or a pattern like *.wav)" ) );
	m_pSearch->setClearButtonEnabled( true );
	m_pSearchDelay = new QTimer( this );
	m_pSearchDelay->setSingleShot( true );
	m_pSearchDelay->setInterval( 200 );
	connect( m_pSearch, &QLineEdit::textChanged, m_pSearchDelay, qOverload<>( &QTimer::start ) );
	connect( m_pSearchDelay, &QTimer::timeout, this, &CVpkEditor::ApplySearch );
	auto *pFind = new QAction( this );
	pFind->setShortcut( QKeySequence::Find );
	pFind->setShortcutContext( Qt::WidgetWithChildrenShortcut );
	connect( pFind, &QAction::triggered, this, [ this ] {
		m_pSearch->setFocus();
		m_pSearch->selectAll();
	} );
	addAction( pFind );

	m_pCount = new QLabel;
	auto *pExportSelected = new QPushButton( tr( "Export…" ) );
	pExportSelected->setToolTip( tr( "Writes the picked files and folders to a folder on disk" ) );
	connect( pExportSelected, &QPushButton::clicked, this, &CVpkEditor::ExportSelected );
	auto *pExportAll = new QPushButton( tr( "Export All…" ) );
	pExportAll->setToolTip( tr( "Writes every file the search lets through to a folder on disk, with its folders" ) );
	connect( pExportAll, &QPushButton::clicked, this, &CVpkEditor::ExportAll );

	auto *pBar = new QHBoxLayout;
	pBar->setContentsMargins( 6, 6, 6, 0 );
	pBar->addWidget( m_pSearch, 1 );
	pBar->addWidget( m_pCount );
	pBar->addWidget( pExportSelected );
	pBar->addWidget( pExportAll );
	if ( m_pPackage->GetBsp() )
	{
		auto *pParticles = new QPushButton( tr( "Particles Manifest…" ) );
		pParticles->setObjectName( QStringLiteral( "mapParticlesManifest" ) );
		pParticles->setToolTip( tr( "Make maps/%1_particles.txt with the particle files this map uses" ).arg( ToQString( m_pPackage->GetFile().stem() ) ) );
		connect( pParticles, &QPushButton::clicked, this, &CVpkEditor::MapParticlesRequested );
		pBar->addWidget( pParticles );
	}

	m_pTree = new QTreeView;
	m_pTree->setModel( m_pFilter );
	m_pTree->setUniformRowHeights( true );
	m_pTree->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_pTree->setSortingEnabled( true );
	m_pTree->sortByColumn( COLUMN_NAME, Qt::AscendingOrder );
	m_pTree->header()->setSectionResizeMode( QHeaderView::Interactive );
	m_pTree->header()->setStretchLastSection( false );
	m_pTree->setColumnWidth( COLUMN_NAME, 280 );
	m_pTree->setColumnWidth( COLUMN_SIZE, 90 );
	m_pTree->setColumnWidth( COLUMN_STORED, 140 );
	m_pTree->header()->restoreState( QSettings{}.value( QStringLiteral( "vpkColumns" ) ).toByteArray() );
	connect( m_pTree->header(), &QHeaderView::sectionResized, this, [ this ] { QSettings{}.setValue( QStringLiteral( "vpkColumns" ), m_pTree->header()->saveState() ); } );
	m_pTree->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pTree, &QTreeView::customContextMenuRequested, this, &CVpkEditor::ShowContextMenu );
	connect( m_pTree, &QTreeView::activated, this, [ this ]( const QModelIndex &index ) {
		const QString szPath = index.siblingAtColumn( COLUMN_NAME ).data( ROLE_PATH ).toString();
		if ( !index.data( ROLE_IS_FOLDER ).toBool() && m_pfnCanOpen && m_pfnCanOpen( szPath ) )
		{
			emit OpenRequested( szPath );
		}
	} );
	connect( m_pTree->selectionModel(), &QItemSelectionModel::currentChanged, this, [ this ]( const QModelIndex &current ) {
		if ( current.isValid() && !current.data( ROLE_IS_FOLDER ).toBool() )
		{
			PreviewEntry( *reinterpret_cast<const xv::vfs::PackageEntry_t *>( current.siblingAtColumn( COLUMN_NAME ).data( ROLE_ENTRY ).value<quintptr>() ) );
		}
	} );

	m_pPreviewHost = new QWidget;
	auto *pPreviewLayout = new QVBoxLayout( m_pPreviewHost );
	pPreviewLayout->setContentsMargins( 0, 0, 0, 0 );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( m_pTree );
	pSplitter->addWidget( m_pPreviewHost );
	pSplitter->setStretchFactor( 0, 2 );
	pSplitter->setStretchFactor( 1, 3 );
	pSplitter->setSizes( { 520, 780 } );

	auto *pFiles = new QWidget;
	auto *pFilesLayout = new QVBoxLayout( pFiles );
	pFilesLayout->setContentsMargins( 0, 0, 0, 0 );
	pFilesLayout->addLayout( pBar );
	pFilesLayout->addWidget( pSplitter, 1 );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	if ( const xv::bsp::CBsp *pBsp = m_pPackage->GetBsp() )
	{
		// A map: its packed files, then what else it holds.
		auto *pTabs = new QTabWidget;
		pTabs->setDocumentMode( true );
		pTabs->addTab( pFiles, tr( "Packed Files (%1)" ).arg( m_pPackage->GetEntries().size() ) );
		m_pEntities = new CEntityView( *pBsp, m_pPackage->GetFile() );
		connect( m_pEntities, &CEntityView::ConfigModifiedChanged, this, &CVpkEditor::ModifiedChanged );
		pTabs->addTab( m_pEntities, tr( "Entities (%1)" ).arg( pBsp->GetEntities().size() ) );
		pTabs->addTab( new CLumpView( *pBsp ), tr( "Lumps" ) );
		pLayout->addWidget( pTabs );
	}
	else
	{
		pLayout->addWidget( pFiles );
	}

	m_nMatches = static_cast<int>( m_pPackage->GetEntries().size() );
	m_pCount->setText( tr( "%n file(s), %1", nullptr, m_nMatches ).arg( QLocale{}.formattedDataSize( static_cast<qint64>( m_pPackage->GetTotalSize() ) ) ) );
	auto *pInfo = new QLabel( tr( "%1\n%2 archive, %n file(s).\n\nPick a file to preview it. Search with the box above; export with the buttons or the right-click menu.", nullptr, m_nMatches )
			.arg( QDir::toNativeSeparators( ToQString( m_pPackage->GetFile() ) ), QString::fromStdString( m_pPackage->GetFormat() ) ) );
	pInfo->setAlignment( Qt::AlignCenter );
	pInfo->setWordWrap( true );
	SetPreviewWidget( pInfo );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVpkEditor::~CVpkEditor() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CVpkEditor::IsModified() const
{
	return m_pEntities && m_pEntities->IsConfigModified();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CVpkEditor::Save()
{
	return m_pEntities && m_pEntities->SaveConfig();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CVpkEditor::GetFileName() const
{
	return ToQString( m_pPackage->GetFile().filename() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CVpkEditor::SetSearch( const QString &szSearch )
{
	m_pSearch->setText( szSearch );
	m_pSearchDelay->stop();
	ApplySearch();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CVpkEditor::GetSearch() const
{
	return m_pFilter->GetSearch();
}

//-----------------------------------------------------------------------------
// Purpose: Filters the tree and counts what's left; a short list of results is opened out so
//          the files show without clicking down to them.
//-----------------------------------------------------------------------------
void CVpkEditor::ApplySearch()
{
	m_pFilter->SetSearch( m_pSearch->text() );
	m_nMatches = 0;
	std::uint64_t nBytes = 0;
	for ( const auto &entry : m_pPackage->GetEntries() )
	{
		if ( m_pFilter->Matches( QString::fromStdString( entry.m_szPath ) ) )
		{
			m_nMatches++;
			nBytes += entry.m_nSize;
		}
	}
	const QString szSize = QLocale{}.formattedDataSize( static_cast<qint64>( nBytes ) );
	if ( GetSearch().isEmpty() )
	{
		m_pCount->setText( tr( "%n file(s), %1", nullptr, m_nMatches ).arg( szSize ) );
		m_pTree->collapseAll();
	}
	else
	{
		m_pCount->setText( tr( "%n match(es), %1", nullptr, m_nMatches ).arg( szSize ) );
		if ( m_nMatches <= 300 )
		{
			m_pTree->expandAll();
		}
	}
	if ( !m_szPreviewed.isEmpty() )
	{
		const QModelIndex proxy = m_pFilter->mapFromSource( m_pModel->IndexForPath( m_szPreviewed ) );
		if ( proxy.isValid() )
		{
			m_pTree->scrollTo( proxy );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The picked file over its preview, with where it's stored, its size and its CRC.
//-----------------------------------------------------------------------------
void CVpkEditor::Preview( const QString &szPath )
{
	if ( const xv::vfs::PackageEntry_t *pEntry = m_pPackage->Find( szPath.toStdString() ) )
	{
		PreviewEntry( *pEntry );
		return;
	}
	emit StatusMessage( tr( "%1 isn't in %2" ).arg( szPath, GetFileName() ) );
}

//-----------------------------------------------------------------------------
// Purpose: By entry rather than path, so each copy of a path packed twice shows its own data.
//-----------------------------------------------------------------------------
void CVpkEditor::PreviewEntry( const xv::vfs::PackageEntry_t &entry )
{
	const xv::vfs::PackageEntry_t *it = &entry;
	const auto bytes = m_pPackage->Read( entry );
	if ( !bytes )
	{
		emit StatusMessage( tr( "Could not read %1 from %2" ).arg( QString::fromStdString( entry.m_szPath ), GetFileName() ) );
		return;
	}
	m_szPreviewed = QString::fromStdString( it->m_szPath );

	// Picked in the tree, unless it is already (or a copy of the same path is).
	const QModelIndex current = m_pTree->currentIndex().siblingAtColumn( COLUMN_NAME );
	const bool bShown = current.isValid() && current.data( ROLE_PATH ).toString().compare( m_szPreviewed, Qt::CaseInsensitive ) == 0;
	const QModelIndex proxy = bShown ? QModelIndex{} : m_pFilter->mapFromSource( m_pModel->IndexForPath( m_szPreviewed ) );
	if ( proxy.isValid() )
	{
		m_pTree->setCurrentIndex( proxy );
		m_pTree->scrollTo( proxy );
	}

	const QString szStored = StoredIn( *m_pPackage, *it );
	QWidget *pView = CreateFileView( m_szPreviewed, szStored, *bytes, m_pPlayer, nullptr, m_pfnRead );
	if ( auto *pMaterial = qobject_cast<CMaterialView *>( pView ) )
	{
		// A material's textures open here when the archive has them.
		connect( pMaterial, &CMaterialView::OpenRequested, this, [ this ]( const QString &szGamePath ) {
			if ( m_pPackage->Read( szGamePath.toStdString() ) )
			{
				QTimer::singleShot( 0, this, [ this, szGamePath ] { Preview( szGamePath ); } );
			}
			else
			{
				emit StatusMessage( tr( "%1 isn't in %2" ).arg( szGamePath, GetFileName() ) );
			}
		} );
	}

	auto *pWrapper = new QWidget;
	auto *pHeader = new QHBoxLayout;
	pHeader->setContentsMargins( 6, 6, 6, 0 );
	auto *pLabel = new QLabel( tr( "<b>%1</b> — %2, in %3, CRC %4" )
			.arg( m_szPreviewed.toHtmlEscaped(), QLocale{}.formattedDataSize( static_cast<qint64>( it->m_nSize ) ), szStored.toHtmlEscaped() )
			.arg( it->m_nCrc, 8, 16, QLatin1Char( '0' ) ) );
	pLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pHeader->addWidget( pLabel, 1 );
	const QString szFile = m_szPreviewed;
	if ( m_pfnCanOpen && m_pfnCanOpen( szFile ) )
	{
		auto *pOpen = new QPushButton( tr( "Open in Editor" ) );
		pOpen->setToolTip( tr( "Opens a copy in its editor; saving asks where, since the archive isn't changed" ) );
		connect( pOpen, &QPushButton::clicked, this, [ this, szFile ] { emit OpenRequested( szFile ); } );
		pHeader->addWidget( pOpen );
	}
	auto *pExport = new QPushButton( tr( "Export…" ) );
	connect( pExport, &QPushButton::clicked, this, [ this, szFile ] {
		const QString szFolder = AskExportFolder();
		if ( !szFolder.isEmpty() )
		{
			ExportTo( { szFile }, szFolder, QFileInfo{ szFile }.path() );
		}
	} );
	pHeader->addWidget( pExport );
	auto *pLayout = new QVBoxLayout( pWrapper );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addLayout( pHeader );
	pLayout->addWidget( pView, 1 );
	SetPreviewWidget( pWrapper );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CVpkEditor::SetPreviewWidget( QWidget *pView )
{
	QLayout *pLayout = m_pPreviewHost->layout();
	while ( QLayoutItem *pItem = pLayout->takeAt( 0 ) )
	{
		// Later, not now: the old view may be what asked for this one and still be handling input.
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
QStringList CVpkEditor::GetSelectedFiles() const
{
	QStringList files;
	QSet<QString> seen;
	const auto add = [ & ]( const QString &szPath ) {
		if ( !seen.contains( szPath ) )
		{
			seen.insert( szPath );
			files.push_back( szPath );
		}
	};
	for ( const QModelIndex &proxy : m_pTree->selectionModel()->selectedRows( COLUMN_NAME ) )
	{
		const QString szPath = proxy.data( ROLE_PATH ).toString();
		if ( !proxy.data( ROLE_IS_FOLDER ).toBool() )
		{
			add( szPath );
			continue;
		}
		const std::string szPrefix = xv::vfs::NormalizePath( szPath.toStdString() ) + '/';
		for ( const auto &entry : m_pPackage->GetEntries() )
		{
			const QString szEntry = QString::fromStdString( entry.m_szPath );
			if ( xv::vfs::NormalizePath( entry.m_szPath ).starts_with( szPrefix ) && m_pFilter->Matches( szEntry ) )
			{
				add( szEntry );
			}
		}
	}
	return files;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CVpkEditor::AskExportFolder()
{
	QSettings settings;
	const QString szStart = settings.value( QLatin1String( EXPORT_FOLDER_KEY ) ).toString();
	const QString szFolder = AskFolder( this, tr( "Export To" ), szStart );
	if ( !szFolder.isEmpty() )
	{
		settings.setValue( QLatin1String( EXPORT_FOLDER_KEY ), szFolder );
	}
	return szFolder;
}

//-----------------------------------------------------------------------------
// Purpose: Picked rows keep their own names: a picked folder comes out as that folder, with its
//          files inside, like dragging it out of a file manager.
//-----------------------------------------------------------------------------
void CVpkEditor::ExportSelected()
{
	const QStringList files = GetSelectedFiles();
	if ( files.isEmpty() )
	{
		emit StatusMessage( tr( "Pick files or folders to export first" ) );
		return;
	}
	// The folder the picked rows share; each comes out below it.
	std::optional<QStringList> shared;
	for ( const QModelIndex &proxy : m_pTree->selectionModel()->selectedRows( COLUMN_NAME ) )
	{
		const QStringList parts = proxy.data( ROLE_PATH ).toString().split( u'/' ).mid( 0, proxy.data( ROLE_PATH ).toString().count( u'/' ) );
		if ( !shared )
		{
			shared = parts;
			continue;
		}
		qsizetype n = 0;
		while ( n < shared->size() && n < parts.size() && shared->at( n ).compare( parts.at( n ), Qt::CaseInsensitive ) == 0 )
		{
			n++;
		}
		shared = shared->mid( 0, n );
	}
	const QString szFolder = AskExportFolder();
	if ( !szFolder.isEmpty() )
	{
		ExportTo( files, szFolder, shared ? shared->join( u'/' ) : QString{} );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CVpkEditor::ExportAll()
{
	QStringList files;
	for ( const auto &entry : m_pPackage->GetEntries() )
	{
		const QString szPath = QString::fromStdString( entry.m_szPath );
		if ( m_pFilter->Matches( szPath ) )
		{
			files.push_back( szPath );
		}
	}
	if ( files.isEmpty() )
	{
		emit StatusMessage( tr( "No files match the search" ) );
		return;
	}
	const QString szFolder = AskExportFolder();
	if ( !szFolder.isEmpty() )
	{
		ExportTo( files, szFolder );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CVpkEditor::ExportTo( const QStringList &files, const QString &szFolder, const QString &szStripFolder, bool bQuiet )
{
	std::vector<std::string> paths;
	paths.reserve( static_cast<std::size_t>( files.size() ) );
	for ( const QString &szFile : files )
	{
		paths.push_back( szFile.toStdString() );
	}
	std::unique_ptr<QProgressDialog> pProgress;
	if ( !bQuiet )
	{
		pProgress = std::make_unique<QProgressDialog>( tr( "Exporting from %1…" ).arg( GetFileName() ), tr( "Cancel" ), 0, static_cast<int>( paths.size() ), this );
		pProgress->setWindowModality( Qt::WindowModal );
		pProgress->setMinimumDuration( 400 );
	}
	std::vector<std::string> failed;
	std::string szStrip = szStripFolder.toStdString();
	if ( !szStrip.empty() && !szStrip.ends_with( '/' ) )
	{
		szStrip += '/';
	}
	const auto pfnProgress = [ &pProgress ]( std::size_t nDone ) {
		if ( !pProgress )
		{
			return true;
		}
		pProgress->setValue( static_cast<int>( nDone ) );
		return !pProgress->wasCanceled();
	};
	const int nWritten = static_cast<int>( m_pPackage->ExtractAll( paths, ToPath( szFolder ), szStrip, pfnProgress, &failed ) );
	const bool bCancelled = pProgress && pProgress->wasCanceled();
	pProgress.reset();

	const QString szWhere = QDir::toNativeSeparators( szFolder );
	emit StatusMessage( tr( "Exported %n file(s) to %1", nullptr, nWritten ).arg( szWhere ) );
	if ( bQuiet )
	{
		return nWritten;
	}
	if ( !failed.empty() )
	{
		QStringList names;
		for ( std::size_t i = 0; i < failed.size() && i < 10; i++ )
		{
			names.push_back( QString::fromStdString( failed[ i ] ) );
		}
		if ( failed.size() > 10 )
		{
			names.push_back( tr( "…and %n more", nullptr, static_cast<int>( failed.size() - 10 ) ) );
		}
		QMessageBox::warning( this, tr( "Export" ), tr( "Exported %n file(s) to %1, but these couldn't be written:\n\n%2", nullptr, nWritten ).arg( szWhere, names.join( u'\n' ) ) );
		return nWritten;
	}
	QMessageBox box( QMessageBox::Information, tr( "Export" ),
		bCancelled ? tr( "Stopped after %n file(s), in %1.", nullptr, nWritten ).arg( szWhere ) : tr( "Exported %n file(s) to %1.", nullptr, nWritten ).arg( szWhere ), QMessageBox::Ok, this );
	QPushButton *pShow = box.addButton( tr( "Show Folder" ), QMessageBox::ActionRole );
	box.exec();
	if ( box.clickedButton() == pShow )
	{
		RevealInFileManager( szFolder );
	}
	return nWritten;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CVpkEditor::ShowContextMenu( const QPoint &at )
{
	const QModelIndex proxy = m_pTree->indexAt( at );
	QMenu menu( this );
	if ( proxy.isValid() )
	{
		const QString szPath = proxy.siblingAtColumn( COLUMN_NAME ).data( ROLE_PATH ).toString();
		menu.addAction( tr( "Export…" ), this, &CVpkEditor::ExportSelected );
		menu.addAction( tr( "Copy Path" ), this, [ szPath ] { QApplication::clipboard()->setText( szPath ); } );
		menu.addSeparator();
	}
	menu.addAction( GetSearch().isEmpty() ? tr( "Export All…" ) : tr( "Export All Matches…" ), this, &CVpkEditor::ExportAll );
	menu.addAction( tr( "Expand All" ), m_pTree, &QTreeView::expandAll );
	menu.addAction( tr( "Collapse All" ), m_pTree, &QTreeView::collapseAll );
	menu.exec( m_pTree->viewport()->mapToGlobal( at ) );
}
