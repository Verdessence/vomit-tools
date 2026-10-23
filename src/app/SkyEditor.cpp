//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SkyEditor.h.
//
//===========================================================================//
#include "SkyEditor.h"

#include <cmath>
#include <functional>
#include <set>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QUndoStack>
#include <QUrl>
#include <QTimer>
#include <QVBoxLayout>

#include "ImageFiles.h"
#include "QtPaths.h"
#include "SkyDocument.h"
#include "SkyTestPattern.h"
#include "SkyView.h"

using xv::sky::Face_t;
using xv::texture::COrientation;
using Severity_t = CSkyEditor::Issue_t::Severity_t;

namespace
{

constexpr auto IMAGE_FOLDER_KEY = "skyImageFolder";
constexpr float SEAM_LIMIT = 12.0f; // stock skies stay under 2 on their sides

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FaceName( Face_t nFace )
{
	return QString::fromUtf8( xv::sky::Suffix( nFace ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Direction( Face_t nFace )
{
	switch ( nFace )
	{
		case Face_t::FACE_RT: return QObject::tr( "east · yaw 0" );
		case Face_t::FACE_BK: return QObject::tr( "north · yaw 90" );
		case Face_t::FACE_LF: return QObject::tr( "west · yaw 180" );
		case Face_t::FACE_FT: return QObject::tr( "south · yaw 270" );
		case Face_t::FACE_UP: return QObject::tr( "up" );
		case Face_t::FACE_DN: return QObject::tr( "down" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString DescribeFix( const xv::sky::OrientationFix_t &fix )
{
	QString szTurn;
	switch ( fix.m_nQuarterTurns )
	{
		case 1: szTurn = QObject::tr( "turning it 90° clockwise" ); break;
		case 2: szTurn = QObject::tr( "turning it 180°" ); break;
		case 3: szTurn = QObject::tr( "turning it 90° counter-clockwise" ); break;
		default: break;
	}
	if ( fix.m_bMirror )
	{
		szTurn = szTurn.isEmpty() ? QObject::tr( "mirroring it" ) : szTurn + QObject::tr( " and mirroring it" );
	}
	return szTurn;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString TestPatternSource()
{
	return QObject::tr( "test pattern" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QStringList LocalImageFiles( const QMimeData *pMime )
{
	QStringList paths;
	for ( const QUrl &url : pMime->urls() )
	{
		if ( url.isLocalFile() && IsImageFile( url.toLocalFile() ) )
		{
			paths << url.toLocalFile();
		}
	}
	return paths;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FormatBytes( double flBytes )
{
	if ( flBytes >= 1024.0 * 1024.0 )
	{
		return QStringLiteral( "%1 MB" ).arg( flBytes / ( 1024.0 * 1024.0 ), 0, 'f', 1 );
	}
	return QStringLiteral( "%1 KB" ).arg( std::lround( flBytes / 1024.0 ) );
}

// One face of the cross: its image, or a place to drop one.
class CFaceSlot : public QWidget
{
public:
	std::function<void( Face_t )> m_pfnShowMaterial;

	CFaceSlot( CSkyDocument *pDocument, Face_t nFace, std::function<void( Face_t )> pfnBrowse, std::function<void( Face_t, QStringList )> pfnDropped )
		: m_pDocument( pDocument ),
		  m_nFace( nFace ),
		  m_pfnBrowse( std::move( pfnBrowse ) ),
		  m_pfnDropped( std::move( pfnDropped ) )
	{
		setAcceptDrops( true );
		setMinimumSize( 90, 90 );
		setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
		setObjectName( QStringLiteral( "face_" ) + FaceName( nFace ) );
		setToolTip( tr( "%1 (%2): drop an image here, double-click to choose one, right-click to turn or mirror it" ).arg( FaceName( nFace ), Direction( nFace ) ) );
	}

	void Refresh()
	{
		m_Thumbnail = {};
		update();
	}

	[[nodiscard]] QSize sizeHint() const override { return { 150, 170 }; }

protected:
	void paintEvent( QPaintEvent * ) override
	{
		QPainter painter( this );
		painter.setRenderHint( QPainter::Antialiasing );
		QFont bold = font();
		bold.setBold( true );
		const int nCaption = QFontMetrics{ bold }.height() + 4; // above the image, so its edge labels stay visible
		const int nSide = std::max( 8, std::min( width(), height() - nCaption ) );
		const QRectF square( ( width() - nSide ) / 2.0 + 1, ( height() - nSide - nCaption ) / 2.0 + nCaption + 1, nSide - 2, nSide - 2 );
		const auto &image = m_pDocument->GetCube().Face( m_nFace );
		if ( image.Empty() )
		{
			painter.setPen( QPen( m_bHover ? palette().color( QPalette::Highlight ) : palette().color( QPalette::Mid ), m_bHover ? 2 : 1, Qt::DashLine ) );
			painter.drawRoundedRect( square, 6, 6 );
			painter.setPen( palette().color( QPalette::PlaceholderText ) );
			painter.drawText( square, Qt::AlignCenter | Qt::TextWordWrap, tr( "%1\n%2\n\nDrop an image\nor double-click" ).arg( FaceName( m_nFace ), Direction( m_nFace ) ) );
			return;
		}
		if ( m_Thumbnail.isNull() || m_Thumbnail.width() != static_cast<int>( square.width() ) )
		{
			m_Thumbnail = ToQImage( image ).scaled( square.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
		}
		painter.drawImage( square.topLeft(), m_Thumbnail );
		painter.setPen( QPen( m_bHover ? palette().color( QPalette::Highlight ) : QColor( 0, 0, 0, 120 ), m_bHover ? 3 : 1 ) );
		painter.setBrush( Qt::NoBrush );
		painter.drawRect( square );

		const auto &slot = m_pDocument->Slot( m_nFace );
		QString szText = QStringLiteral( "%1 · %2×%3" ).arg( FaceName( m_nFace ) ).arg( image.m_nWidth ).arg( image.m_nHeight );
		if ( slot.m_Orientation.m_nQuarterTurns || slot.m_Orientation.m_bMirror )
		{
			szText += QStringLiteral( " · %1°%2" ).arg( slot.m_Orientation.m_nQuarterTurns * 90 ).arg( slot.m_Orientation.m_bMirror ? tr( " mirrored" ) : QString{} );
		}
		painter.setFont( bold );
		painter.setPen( palette().color( QPalette::WindowText ) );
		const QRectF band( square.left(), square.top() - nCaption, square.width(), nCaption );
		painter.drawText( band, Qt::AlignVCenter | Qt::AlignLeft, painter.fontMetrics().elidedText( szText, Qt::ElideRight, static_cast<int>( band.width() ) ) );
	}

	void mouseDoubleClickEvent( QMouseEvent * ) override { m_pfnBrowse( m_nFace ); }

	void contextMenuEvent( QContextMenuEvent *pEvent ) override
	{
		QMenu menu( this );
		menu.addAction( tr( "Choose Image…" ), this, [ this ] { m_pfnBrowse( m_nFace ); } );
		const auto &slot = m_pDocument->Slot( m_nFace );
		const bool bHas = slot.m_pImage != nullptr;
		const auto orient = [ this ]( COrientation change, const QString &szText ) {
			return [ this, change, szText ] { m_pDocument->Orient( m_nFace, change, szText.arg( FaceName( m_nFace ) ) ); };
		};
		menu.addSeparator();
		menu.addAction( tr( "Turn Clockwise" ), this, orient( { 1, false }, tr( "Turn %1" ) ) )->setEnabled( bHas );
		menu.addAction( tr( "Turn Counter-clockwise" ), this, orient( { 3, false }, tr( "Turn %1" ) ) )->setEnabled( bHas );
		menu.addAction( tr( "Turn 180°" ), this, orient( { 2, false }, tr( "Turn %1" ) ) )->setEnabled( bHas );
		menu.addAction( tr( "Mirror" ), this, orient( { 0, true }, tr( "Mirror %1" ) ) )->setEnabled( bHas );
		menu.addSeparator();
		menu.addAction( tr( "Show Material" ), this, [ this ] { m_pfnShowMaterial( m_nFace ); } )->setEnabled( bHas && !slot.m_szMaterial.isEmpty() && m_pfnShowMaterial );
		menu.addAction( tr( "Copy Source Path" ), this, [ szSource = slot.m_szSource ] { QApplication::clipboard()->setText( szSource ); } )->setEnabled( bHas && !slot.m_szSource.isEmpty() );
		menu.addAction( tr( "Clear" ), this, [ this ] { m_pDocument->ClearFace( m_nFace ); } )->setEnabled( bHas );
		menu.exec( pEvent->globalPos() );
	}

	void dragEnterEvent( QDragEnterEvent *pEvent ) override
	{
		if ( !LocalImageFiles( pEvent->mimeData() ).isEmpty() )
		{
			pEvent->acceptProposedAction();
			m_bHover = true;
			update();
		}
	}
	void dragLeaveEvent( QDragLeaveEvent * ) override
	{
		m_bHover = false;
		update();
	}
	void dropEvent( QDropEvent *pEvent ) override
	{
		m_bHover = false;
		update();
		m_pfnDropped( m_nFace, LocalImageFiles( pEvent->mimeData() ) );
		pEvent->acceptProposedAction();
	}

private:
	CSkyDocument *m_pDocument;
	Face_t m_nFace;
	std::function<void( Face_t )> m_pfnBrowse;
	std::function<void( Face_t, QStringList )> m_pfnDropped;
	QImage m_Thumbnail;
	bool m_bHover = false;
};

// Keeps the preview square however the panel is shaped, with its controls right under it.
class CSquarePanel : public QWidget
{
public:
	CSquarePanel( QWidget *pView, QWidget *pBar )
		: m_pView( pView ),
		  m_pBar( pBar )
	{
		m_pView->setParent( this );
		m_pBar->setParent( this );
		setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
	}

	[[nodiscard]] QSize sizeHint() const override { return { 480, 480 + m_pBar->sizeHint().height() }; }
	[[nodiscard]] QSize minimumSizeHint() const override
	{
		return { std::max( 240, m_pBar->minimumSizeHint().width() ), 240 + m_pBar->sizeHint().height() };
	}

protected:
	void resizeEvent( QResizeEvent * ) override
	{
		constexpr int nGAP = 4;
		const int nBarHeight = m_pBar->sizeHint().height();
		const int nSide = std::max( 0, std::min( width(), height() - nBarHeight - nGAP ) );
		const int nTop = ( height() - nSide - nGAP - nBarHeight ) / 2;
		m_pView->setGeometry( ( width() - nSide ) / 2, nTop, nSide, nSide );
		const int nBarWidth = std::min( width(), std::max( nSide, m_pBar->sizeHint().width() ) );
		m_pBar->setGeometry( ( width() - nBarWidth ) / 2, nTop + nSide + nGAP, nBarWidth, nBarHeight );
	}

private:
	QWidget *m_pView;
	QWidget *m_pBar;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Takes ownership of document.
//-----------------------------------------------------------------------------
CSkyEditor::CSkyEditor( CSkyDocument *pDocument, QWidget *pParent )
	: QWidget( pParent ),
	  m_pDocument( pDocument )
{
	m_pDocument->setParent( this );
	setAcceptDrops( true );

	auto *pToolbar = new QToolBar;
	pToolbar->setToolButtonStyle( Qt::ToolButtonTextOnly );
	pToolbar->addWidget( new QLabel( tr( "Name " ) ) );
	m_pName = new QLineEdit( m_pDocument->GetName() );
	m_pName->setObjectName( QStringLiteral( "skyName" ) );
	m_pName->setPlaceholderText( QStringLiteral( "sky_mymap" ) );
	m_pName->setMaximumWidth( 220 );
	m_pName->setValidator( new QRegularExpressionValidator( QRegularExpression( QStringLiteral( "[a-z0-9_.-]{0,64}" ) ), m_pName ) );
	m_pName->setToolTip( tr( "The sky's name: maps use it as their skyname, and its files are materials/skybox/<name>rt.vtf and so on" ) );
	connect( m_pName, &QLineEdit::textEdited, this, [ this ]( const QString &szText ) { m_pDocument->SetName( szText ); } );
	connect( m_pDocument, &CSkyDocument::NameChanged, this, [ this ] {
		if ( m_pName->text() != m_pDocument->GetName() )
		{
			m_pName->setText( m_pDocument->GetName() );
		}
		Relint();
	} );
	pToolbar->addWidget( m_pName );
	pToolbar->addSeparator();

	m_pFormat = new QComboBox;
	m_pFormat->addItem( tr( "Uncompressed (BGR888)" ), static_cast<int>( xv::texture::VtfFormat_t::BGR888 ) );
	m_pFormat->addItem( tr( "Compressed (DXT1)" ), static_cast<int>( xv::texture::VtfFormat_t::DXT1 ) );
	m_pFormat->setToolTip( tr( "Uncompressed keeps smooth gradients smooth; DXT1 is a sixth of the size but can band" ) );
	m_pFormat->setCurrentIndex( m_pFormat->findData( static_cast<int>( m_pDocument->GetFormat() ) ) );
	connect( m_pFormat, &QComboBox::currentIndexChanged, this, [ this ] {
		m_pDocument->SetFormat( static_cast<xv::texture::VtfFormat_t>( m_pFormat->currentData().toInt() ) );
	} );
	pToolbar->addWidget( m_pFormat );

	m_pSize = new QComboBox;
	m_pSize->addItem( tr( "Keep size" ), 0 );
	for ( int nSize : { 256, 512, 1024, 2048, 4096 } )
	{
		m_pSize->addItem( QStringLiteral( "%1 × %1" ).arg( nSize ), nSize );
	}
	m_pSize->setToolTip( tr( "Face size to write; the game needs square faces with power-of-two sides" ) );
	m_pSize->setCurrentIndex( std::max( 0, m_pSize->findData( m_pDocument->size() ) ) );
	connect( m_pSize, &QComboBox::currentIndexChanged, this, [ this ] { m_pDocument->SetSize( m_pSize->currentData().toInt() ); } );
	pToolbar->addWidget( m_pSize );

	m_pMipmaps = new QCheckBox( tr( "Mipmaps" ) );
	m_pMipmaps->setChecked( m_pDocument->GetMipmaps() );
	m_pMipmaps->setToolTip( tr( "Smaller copies for distant viewing. Valve's newer skies go without (NOMIP), as a sky is never far away" ) );
	connect( m_pMipmaps, &QCheckBox::toggled, this, [ this ]( bool bOn ) { m_pDocument->SetMipmaps( bOn ); } );
	connect( m_pDocument, &CSkyDocument::OptionsChanged, this, [ this ] {
		const QSignalBlocker formatBlocker{ m_pFormat };
		const QSignalBlocker sizeBlocker{ m_pSize };
		const QSignalBlocker mipmapsBlocker{ m_pMipmaps };
		m_pFormat->setCurrentIndex( m_pFormat->findData( static_cast<int>( m_pDocument->GetFormat() ) ) );
		m_pSize->setCurrentIndex( std::max( 0, m_pSize->findData( m_pDocument->size() ) ) );
		m_pMipmaps->setChecked( m_pDocument->GetMipmaps() );
		Relint();
		UpdateSizeEstimate();
	} );
	pToolbar->addWidget( m_pMipmaps );
	m_pEstimate = new QLabel;
	m_pEstimate->setContentsMargins( 8, 0, 8, 0 );
	m_pEstimate->setEnabled( false );
	pToolbar->addWidget( m_pEstimate );
	pToolbar->addSeparator();
	pToolbar->addAction( tr( "Load Faces…" ), this, &CSkyEditor::BrowseFiles )->setToolTip( tr( "Choose up to six images named …rt, …bk, …lf, …ft, …up and …dn" ) );
	pToolbar->addAction( tr( "Test Pattern" ), this, [ this ] { UseTestPattern(); } )->setToolTip( tr( "Labelled faces for checking a sky's orientation in game" ) );
	pToolbar->addSeparator();
	QAction *pExportAction = pToolbar->addAction( tr( "Export" ), this, &CSkyEditor::ExportRequested );
	pExportAction->setToolTip( tr( "Write the VTFs and VMTs to the game's write folder (Ctrl+S)" ) );
	pToolbar->addAction( tr( "Export To…" ), this, &CSkyEditor::ExportToRequested )->setToolTip( tr( "Write them under a folder of your choice (a mod or custom/ folder)" ) );

	m_pExported = new QLabel;
	m_pExported->setObjectName( QStringLiteral( "skyExported" ) );
	m_pExported->setWordWrap( true );
	m_pExported->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pExported->setContentsMargins( 8, 2, 8, 2 );
	m_pExported->hide();

	// The cross: as the faces join, with rt in the middle (up's bottom edge and dn's top edge
	// meet rt; going right from rt: ft, lf, bk).
	auto *pCross = new QWidget;
	auto *pGrid = new QGridLayout( pCross );
	pGrid->setSpacing( 2 );
	const std::array<std::tuple<Face_t, int, int>, 6> places{ { { Face_t::FACE_UP, 0, 1 }, { Face_t::FACE_BK, 1, 0 }, { Face_t::FACE_RT, 1, 1 }, { Face_t::FACE_FT, 1, 2 }, { Face_t::FACE_LF, 1, 3 }, { Face_t::FACE_DN, 2, 1 } } };
	for ( const auto &[ face, row, column ] : places )
	{
		auto *pSlot = new CFaceSlot( m_pDocument, face, [ this ]( Face_t f ) { BrowseFace( f ); }, [ this ]( Face_t f, const QStringList &paths ) {
			if (paths.size() == 1) {
				LoadFace(f, paths.front());
			} else {
				LoadFiles(paths);
			} } );
		pSlot->m_pfnShowMaterial = [ this ]( Face_t f ) { emit MaterialRequested( m_pDocument->Slot( f ).m_szMaterial ); };
		m_Slots[ xv::sky::IndexOf( face ) ] = pSlot;
		pGrid->addWidget( pSlot, row, column );
	}
	for ( int i = 0; i < 4; i++ )
	{
		pGrid->setColumnStretch( i, 1 );
	}

	m_pIssueList = new QListWidget;
	m_pIssueList->setObjectName( QStringLiteral( "skyIssues" ) );
	m_pIssueList->setWordWrap( true );
	m_pIssueList->setMaximumHeight( 150 );
	connect( m_pIssueList, &QListWidget::itemActivated, this, [ this ]( QListWidgetItem *pItem ) {
		const int nRow = m_pIssueList->row( pItem );
		if ( nRow >= 0 && nRow < static_cast<int>( m_Issues.size() ) && m_Issues[ static_cast<std::size_t>( nRow ) ].m_Fix )
		{
			// After the double-click: the fix rebuilds this list.
			QTimer::singleShot( 0, this, [ this, fix = *m_Issues[ static_cast<std::size_t>( nRow ) ].m_Fix ] { ApplyFix( fix ); } );
		}
	} );

	auto *pLeft = new QWidget;
	auto *pLeftLayout = new QVBoxLayout( pLeft );
	pLeftLayout->setContentsMargins( 6, 6, 6, 6 );
	pLeftLayout->addWidget( pCross, 1 );
	pLeftLayout->addWidget( m_pIssueList );

	m_pView = new CSkyView;
	m_pView->SetCube( &m_pDocument->GetCube() );
	auto *pLabels = new QCheckBox( tr( "Face names" ) );
	pLabels->setChecked( true );
	connect( pLabels, &QCheckBox::toggled, m_pView, &CSkyView::SetShowLabels );
	auto *pEdges = new QCheckBox( tr( "Face edges" ) );
	connect( pEdges, &QCheckBox::toggled, m_pView, &CSkyView::SetShowEdges );
	auto *pCopy = new QPushButton( tr( "Copy setang" ) );
	pCopy->setToolTip( tr( "Copies the console command (needs sv_cheats 1) that looks the same way in game" ) );
	connect( pCopy, &QPushButton::clicked, this, [ this ] { QApplication::clipboard()->setText( m_pView->GetSetangCommand() ); } );
	auto *pViewBar = new QWidget;
	auto *pViewBarLayout = new QHBoxLayout( pViewBar );
	pViewBarLayout->setContentsMargins( 0, 0, 0, 0 );
	pViewBarLayout->addWidget( pLabels );
	pViewBarLayout->addWidget( pEdges );
	pViewBarLayout->addStretch();
	pViewBarLayout->addWidget( pCopy );
	auto *pRight = new QWidget;
	auto *pRightLayout = new QVBoxLayout( pRight );
	pRightLayout->setContentsMargins( 0, 6, 6, 6 );
	pRightLayout->addWidget( new CSquarePanel( m_pView, pViewBar ), 1 );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( pLeft );
	pSplitter->addWidget( pRight );
	pSplitter->setStretchFactor( 0, 4 );
	pSplitter->setStretchFactor( 1, 5 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 0 );
	pLayout->addWidget( pToolbar );
	pLayout->addWidget( m_pExported );
	pLayout->addWidget( pSplitter, 1 );

	connect( m_pDocument, &CSkyDocument::FacesChanged, this, &CSkyEditor::FacesChanged );
	FacesChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyEditor::FacesChanged()
{
	m_pView->SetCube( &m_pDocument->GetCube() );
	for ( QWidget *pSlot : m_Slots )
	{
		static_cast<CFaceSlot *>( pSlot )->Refresh();
	}
	Relint();
	UpdateSizeEstimate();
}

//-----------------------------------------------------------------------------
// Purpose: Puts each file on the face its name ends with (…rt.png, …UP.tga), as one undo step. A
//          single
//          file dropped into an empty sky brings the other faces beside it along. Returns the
//          files that
//          couldn't be placed or read, with the reason.
//-----------------------------------------------------------------------------
QStringList CSkyEditor::LoadFiles( const QStringList &given )
{
	QStringList paths = given;
	// One face dropped into an empty sky brings the rest of its sky along.
	const bool bEmpty = std::ranges::none_of( xv::sky::FACES, [ this ]( Face_t nFace ) { return m_pDocument->Slot( nFace ).m_pImage != nullptr; } );
	if ( paths.size() == 1 && bEmpty )
	{
		if ( const auto files = xv::sky::FindSkyFiles( ToPath( paths.front() ) ); files && files->Count() > 1 )
		{
			paths.clear();
			for ( const auto &file : files->m_Faces )
			{
				if ( !file.empty() && IsImageFile( ToQString( file ) ) )
				{
					paths << ToQString( file );
				}
			}
			if ( m_pDocument->GetName().isEmpty() && xv::sky::IsValidName( files->m_szName ) )
			{
				m_pDocument->SetName( QString::fromStdString( files->m_szName ) );
			}
		}
	}
	QStringList problems;
	std::array<std::optional<CSkyDocument::Slot_t>, 6> changes;
	for ( const QString &szPath : paths )
	{
		const QString szName = QFileInfo{ szPath }.fileName();
		const auto face = xv::sky::FaceFromFileName( szName.toStdString() );
		if ( !face )
		{
			problems << tr( "%1: the name doesn't end in rt, bk, lf, ft, up or dn, so drop it onto its face instead" ).arg( szName );
			continue;
		}
		QString szError;
		auto image = ReadImageFile( szPath, &szError );
		if ( !image )
		{
			problems << tr( "%1: %2" ).arg( szName, szError );
			continue;
		}
		changes[ xv::sky::IndexOf( *face ) ] = CSkyDocument::Slot_t{ std::make_shared<const xv::texture::CImage>( std::move( *image ) ), szPath, {}, {}, {} };
	}
	if ( std::ranges::any_of( changes, []( const auto &change ) { return change.has_value(); } ) )
	{
		m_pDocument->SetSlots( changes, tr( "Load Faces" ) );
	}
	if ( !problems.isEmpty() )
	{
		QMessageBox::warning( this, tr( "Load Faces" ), problems.join( u'\n' ) );
	}
	return problems;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSkyEditor::LoadFace( Face_t nFace, const QString &szPath )
{
	QString szError;
	auto image = ReadImageFile( szPath, &szError );
	if ( !image )
	{
		QMessageBox::warning( this, tr( "Load Image" ), tr( "Could not read %1:\n%2" ).arg( QDir::toNativeSeparators( szPath ), szError ) );
		return false;
	}
	m_pDocument->SetSlot( nFace, { std::make_shared<const xv::texture::CImage>( std::move( *image ) ), szPath, {}, {}, {} }, tr( "Set %1" ).arg( FaceName( nFace ) ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Fills every face with a labelled pattern for checking orientation in game.
//-----------------------------------------------------------------------------
void CSkyEditor::UseTestPattern( int nSize )
{
	std::array<std::optional<CSkyDocument::Slot_t>, 6> changes;
	for ( Face_t nFace : xv::sky::FACES )
	{
		changes[ xv::sky::IndexOf( nFace ) ] = CSkyDocument::Slot_t{ std::make_shared<const xv::texture::CImage>( SkyTestPattern( nFace, nSize ) ), TestPatternSource(), {}, {}, {} };
	}
	m_pDocument->SetSlots( changes, tr( "Test Pattern" ) );
	if ( m_pDocument->GetName().isEmpty() )
	{
		m_pDocument->SetName( QStringLiteral( "vomit_test" ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyEditor::ApplyFix( const xv::sky::OrientationFix_t &fix )
{
	m_pDocument->Orient( fix.m_nFace, { fix.m_nQuarterTurns, fix.m_bMirror }, tr( "Fix %1" ).arg( FaceName( fix.m_nFace ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: No errors: every face present, a valid name, sizes the game can use.
//-----------------------------------------------------------------------------
bool CSkyEditor::CanExport() const
{
	return std::ranges::none_of( m_Issues, []( const Issue_t &issue ) { return issue.m_nSeverity == Severity_t::SEVERITY_ERROR; } );
}

//-----------------------------------------------------------------------------
// Purpose: After an export: where the files went and how to see the sky in game.
//-----------------------------------------------------------------------------
void CSkyEditor::ShowExported( const QString &szFolder )
{
	m_pExported->setText( tr( "Exported to %1. In game, with a map loaded, open the console and enter: sv_skyname %2" )
			.arg( QDir::toNativeSeparators( szFolder ), m_pDocument->GetName() ) );
	m_pExported->show();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyEditor::dragEnterEvent( QDragEnterEvent *pEvent )
{
	if ( !LocalImageFiles( pEvent->mimeData() ).isEmpty() )
	{
		pEvent->acceptProposedAction();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyEditor::dropEvent( QDropEvent *pEvent )
{
	LoadFiles( LocalImageFiles( pEvent->mimeData() ) );
	pEvent->acceptProposedAction();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyEditor::BrowseFace( Face_t nFace )
{
	QSettings settings;
	const QString szPath = QFileDialog::getOpenFileName( this, tr( "Image for %1 (%2)" ).arg( FaceName( nFace ), Direction( nFace ) ), settings.value( IMAGE_FOLDER_KEY ).toString(),
		tr( "Images (*.png *.jpg *.jpeg *.tga *.bmp *.psd *.hdr *.vtf);;All files (*)" ) );
	if ( !szPath.isEmpty() )
	{
		settings.setValue( IMAGE_FOLDER_KEY, QFileInfo{ szPath }.absolutePath() );
		LoadFace( nFace, szPath );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyEditor::BrowseFiles()
{
	QSettings settings;
	const QStringList paths = QFileDialog::getOpenFileNames( this, tr( "Load Faces (named …rt, …bk, …lf, …ft, …up, …dn)" ), settings.value( IMAGE_FOLDER_KEY ).toString(),
		tr( "Images (*.png *.jpg *.jpeg *.tga *.bmp *.psd *.hdr *.vtf);;All files (*)" ) );
	if ( !paths.isEmpty() )
	{
		settings.setValue( IMAGE_FOLDER_KEY, QFileInfo{ paths.front() }.absolutePath() );
		LoadFiles( paths );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyEditor::Relint()
{
	m_Issues.clear();
	const QString szName = m_pDocument->GetName();
	if ( szName.isEmpty() )
	{
		m_Issues.push_back( { Severity_t::SEVERITY_ERROR, tr( "Name the sky. Maps use the name as their skyname, and the files become materials/skybox/<name>rt.vtf and so on." ), {} } );
	}
	else if ( !xv::sky::IsValidName( szName.toStdString() ) )
	{
		m_Issues.push_back( { Severity_t::SEVERITY_ERROR, tr( "Sky names may only use lowercase letters, digits, '_', '-' and '.'." ), {} } );
	}

	// Faces may differ in size (Valve's own skies pair 4x4 bottoms with 512 sides), but each must
	// be a square with power-of-two sides. Half-height stock faces are made square on export.
	const xv::sky::CCube &cube = m_pDocument->GetCube();
	QStringList missing;
	for ( Face_t nFace : xv::sky::FACES )
	{
		const auto &image = cube.Face( nFace );
		if ( image.Empty() )
		{
			missing << FaceName( nFace );
			continue;
		}
		const bool bUsable = image.m_nWidth == image.m_nHeight && xv::texture::IsPowerOfTwo( image.m_nWidth );
		if ( m_pDocument->size() == 0 && !bUsable && cube.m_Transforms[ xv::sky::IndexOf( nFace ) ].IsIdentity() )
		{
			m_Issues.push_back( { Severity_t::SEVERITY_ERROR, tr( "%1 is %2×%3, but the game needs square faces with power-of-two sides. Pick a face size to resize it." ).arg( FaceName( nFace ) ).arg( image.m_nWidth ).arg( image.m_nHeight ), {} } );
		}
	}
	if ( !missing.isEmpty() )
	{
		m_Issues.push_back( { Severity_t::SEVERITY_ERROR, tr( "No image yet for: %1." ).arg( missing.join( QStringLiteral( ", " ) ) ), {} } );
	}

	// How the faces meet. Turned up/down faces can be fixed; other mismatches only reported. The
	// test pattern's faces aren't meant to meet, so it isn't checked.
	const bool bTestPattern = std::ranges::all_of( xv::sky::FACES, [ this ]( Face_t nFace ) { return m_pDocument->Slot( nFace ).m_szSource == TestPatternSource(); } );
	std::set<Face_t> fixable;
	for ( const auto &fix : bTestPattern ? std::vector<xv::sky::OrientationFix_t>{} : xv::sky::SuggestOrientation( cube ) )
	{
		fixable.insert( fix.m_nFace );
		m_Issues.push_back( { Severity_t::SEVERITY_WARNING, tr( "%1 looks turned: %2 makes its edges meet the sides (difference %3 → %4). Double-click to fix." ).arg( FaceName( fix.m_nFace ), DescribeFix( fix ) ).arg( std::lround( fix.m_flBefore ) ).arg( std::lround( fix.m_flAfter ) ), fix } );
	}
	float flWorstBottom = 0.0f;
	for ( const auto &seam : bTestPattern ? std::vector<xv::sky::Seam_t>{} : xv::sky::Seams( cube ) )
	{
		if ( fixable.contains( seam.m_nA ) || fixable.contains( seam.m_nB ) || seam.m_flDifference < SEAM_LIMIT )
		{
			continue;
		}
		if ( seam.m_nA == Face_t::FACE_DN || seam.m_nB == Face_t::FACE_DN )
		{
			flWorstBottom = std::max( flWorstBottom, seam.m_flDifference );
			continue;
		}
		m_Issues.push_back( { Severity_t::SEVERITY_WARNING, tr( "%1 and %2 don't meet (difference %3). Check that each image is on the right face." ).arg( FaceName( seam.m_nA ), FaceName( seam.m_nB ) ).arg( std::lround( seam.m_flDifference ) ), {} } );
	}
	if ( flWorstBottom > 0.0f )
	{
		m_Issues.push_back( { Severity_t::SEVERITY_INFO, tr( "dn doesn't continue the sides (difference up to %1). That's common, and fine: the bottom of a sky is rarely seen." ).arg( std::lround( flWorstBottom ) ), {} } );
	}

	m_pIssueList->clear();
	for ( const Issue_t &issue : m_Issues )
	{
		const QStyle::StandardPixmap nIcon = issue.m_nSeverity == Severity_t::SEVERITY_ERROR ? QStyle::SP_MessageBoxCritical
			: issue.m_nSeverity == Severity_t::SEVERITY_WARNING                              ? QStyle::SP_MessageBoxWarning
																							 : QStyle::SP_MessageBoxInformation;
		auto *pItem = new QListWidgetItem( style()->standardIcon( nIcon ), issue.m_szMessage );
		if ( issue.m_Fix )
		{
			pItem->setToolTip( tr( "Double-click to apply" ) );
		}
		m_pIssueList->addItem( pItem );
	}
	if ( m_Issues.empty() )
	{
		m_pIssueList->addItem( new QListWidgetItem( style()->standardIcon( QStyle::SP_DialogApplyButton ), tr( "Ready to export." ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyEditor::UpdateSizeEstimate()
{
	int nSide = m_pDocument->size();
	if ( nSide == 0 )
	{
		for ( const auto &image : m_pDocument->GetCube().m_Faces )
		{
			nSide = std::max( nSide, std::max( image.m_nWidth, image.m_nHeight ) );
		}
	}
	if ( nSide == 0 )
	{
		m_pEstimate->clear();
		return;
	}
	const double flPixels = static_cast<double>( nSide ) * nSide * ( m_pDocument->GetMipmaps() ? 4.0 / 3.0 : 1.0 );
	const double flPerFace = flPixels * ( m_pDocument->GetFormat() == xv::texture::VtfFormat_t::DXT1 ? 0.5 : 3.0 );
	m_pEstimate->setText( tr( "≈ %1" ).arg( FormatBytes( flPerFace * 6 ) ) );
	m_pEstimate->setToolTip( tr( "6 faces of %1 each" ).arg( FormatBytes( flPerFace ) ) );
}
