//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SheetDialog.h.
//
//===========================================================================//
#include "SheetDialog.h"

#include <cstring>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "FileDialogs.h"
#include "ImageFiles.h"
#include "TextureEditor.h"
#include "TextureOptionsDialog.h"
#include "TexturePresets.h"

using namespace xv::texture;

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( const std::string &szText )
{
	return QString::fromStdString( szText );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: vtf, when given, is the texture being edited.
//-----------------------------------------------------------------------------
CSheetDialog::CSheetDialog( const CVtf *pVtf, QWidget *pParent )
	: QDialog( pParent ),
	  m_pVtf( pVtf )
{
	setWindowTitle( tr( "Sprite Sheet" ) );
	// Starts with the particle preset: DXT5, clamped, version 7.4 (sheets need 7.3 or later).
	for ( const TexturePreset_t &preset : TexturePresets() )
	{
		if ( preset.m_bBuiltin && preset.m_Options.m_Write.m_nVersionMinor >= 3 )
		{
			m_Options = preset.m_Options;
			break;
		}
	}
	m_Options.m_nResize = ResizeMode_t::RESIZE_KEEP; // the packing makes powers of two itself

	m_pTree = new QTreeWidget;
	m_pTree->setObjectName( QStringLiteral( "sheetTree" ) );
	m_pTree->setHeaderLabels( { tr( "Sequence / frame" ), tr( "Lasts" ) } );
	m_pTree->setSelectionMode( QAbstractItemView::SingleSelection );

	auto *pAddSequence = new QPushButton( tr( "New Sequence" ) );
	auto *pAddFrames = new QPushButton( tr( "Add Frames…" ) );
	pAddFrames->setToolTip( tr( "Pictures to add to the picked sequence, one frame each" ) );
	auto *pRemove = new QPushButton( tr( "Remove" ) );
	auto *pUp = new QPushButton( tr( "Up" ) );
	auto *pDown = new QPushButton( tr( "Down" ) );
	auto *pOpen = new QPushButton( tr( "Open .mks…" ) );
	auto *pSave = new QPushButton( tr( "Save .mks…" ) );
	auto *pCut = new QPushButton( tr( "Start from This Texture" ) );
	pCut->setToolTip( tr( "Cut the texture's own frames out by its sheet, to change and pack again" ) );
	pCut->setEnabled( m_pVtf && !m_pVtf->Resource( SHEET_RESOURCE ).empty() );
	auto *pButtons = new QVBoxLayout;
	for ( QPushButton *pButton : { pAddSequence, pAddFrames, pRemove, pUp, pDown } )
	{
		pButtons->addWidget( pButton );
	}
	pButtons->addSpacing( 12 );
	for ( QPushButton *pButton : { pOpen, pSave, pCut } )
	{
		pButtons->addWidget( pButton );
	}
	pButtons->addStretch();

	m_pNumber = new QSpinBox;
	m_pNumber->setRange( 0, 4095 );
	m_pNumber->setToolTip( tr( "The number particles pick the sequence by (Sequence Number)" ) );
	m_pLoop = new QCheckBox( tr( "Loops" ) );
	m_pLoop->setToolTip( tr( "Off: holds the last frame" ) );
	m_pDuration = new QDoubleSpinBox;
	m_pDuration->setRange( 0.01, 100.0 );
	m_pDuration->setSingleStep( 0.25 );
	m_pDuration->setToolTip( tr( "How long the frame shows, relative to the others (1 is normal; the particle sets the speed)" ) );
	auto *pPicked = new QFormLayout;
	pPicked->addRow( tr( "Sequence number" ), m_pNumber );
	pPicked->addRow( QString{}, m_pLoop );
	pPicked->addRow( tr( "Frame lasts" ), m_pDuration );

	auto *pLeft = new QWidget;
	auto *pLeftLayout = new QHBoxLayout( pLeft );
	auto *pTreeColumn = new QVBoxLayout;
	pTreeColumn->addWidget( m_pTree, 1 );
	pTreeColumn->addLayout( pPicked );
	pLeftLayout->addLayout( pTreeColumn, 1 );
	pLeftLayout->addLayout( pButtons );

	m_pPreview = new QLabel;
	m_pPreview->setObjectName( QStringLiteral( "sheetPreview" ) );
	m_pPreview->setAlignment( Qt::AlignCenter );
	m_pPreview->setMinimumSize( 256, 256 );
	m_pPadding = new QSpinBox;
	m_pPadding->setRange( 0, 8 );
	m_pPadding->setValue( 1 );
	m_pPadding->setPrefix( tr( "border " ) );
	m_pPadding->setSuffix( tr( " texels" ) );
	m_pPadding->setToolTip( tr( "Each picture's edge repeated around it, so filtering doesn't bleed from its neighbours" ) );
	m_pSettings = new QLabel;
	auto *pSettingsButton = new QPushButton( tr( "Texture Settings…" ) );
	auto *pSettingsRow = new QHBoxLayout;
	pSettingsRow->addWidget( m_pPadding );
	pSettingsRow->addWidget( m_pSettings, 1 );
	pSettingsRow->addWidget( pSettingsButton );
	auto *pRight = new QWidget;
	auto *pRightLayout = new QVBoxLayout( pRight );
	pRightLayout->addWidget( m_pPreview, 1 );
	pRightLayout->addLayout( pSettingsRow );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( pLeft );
	pSplitter->addWidget( pRight );
	pSplitter->setSizes( { 460, 420 } );

	m_pNotes = new QLabel;
	m_pNotes->setObjectName( QStringLiteral( "sheetNotes" ) );
	m_pNotes->setWordWrap( true );
	auto *pBox = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pBox->button( QDialogButtonBox::Ok )->setText( tr( "Make Sheet" ) );
	connect( pBox, &QDialogButtonBox::accepted, this, [ this ] {
		std::string szError;
		if ( BuildTexture( &szError ).empty() )
		{
			QMessageBox::warning( this, tr( "Sprite Sheet" ), tr( "Couldn't make the sheet: %1." ).arg( Qs( szError ) ) );
			return;
		}
		accept();
	} );
	connect( pBox, &QDialogButtonBox::rejected, this, &QDialog::reject );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( pSplitter, 1 );
	pLayout->addWidget( m_pNotes );
	pLayout->addWidget( pBox );
	resize( 960, 620 );

	connect( pAddSequence, &QPushButton::clicked, this, [ this ] {
		int nNumber = 0;
		for ( const MksSequence_t &sequence : m_Mks.m_Sequences )
		{
			nNumber = std::max( nNumber, sequence.m_nNumber + 1 );
		}
		m_Mks.m_Sequences.push_back( { nNumber, true, {} } );
		FillTree();
		m_pTree->setCurrentItem( m_pTree->topLevelItem( m_pTree->topLevelItemCount() - 1 ) );
	} );
	connect( pAddFrames, &QPushButton::clicked, this, [ this ] {
		const QStringList files = AskOpenFiles( this, tr( "Frames" ), {}, PictureFilter( false ) );
		if ( !files.isEmpty() )
		{
			AddFrames( files );
		}
	} );
	connect( pRemove, &QPushButton::clicked, this, [ this ] {
		const auto [ nSequence, nFrame ] = Picked();
		if ( nSequence < 0 )
		{
			return;
		}
		auto &sequence = m_Mks.m_Sequences[ static_cast<std::size_t>( nSequence ) ];
		if ( nFrame < 0 )
		{
			m_Mks.m_Sequences.erase( m_Mks.m_Sequences.begin() + nSequence );
		}
		else
		{
			sequence.m_Frames.erase( sequence.m_Frames.begin() + nFrame );
		}
		FillTree();
	} );
	const auto move = [ this ]( int nBy ) {
		const auto [ nSequence, nFrame ] = Picked();
		if ( nSequence < 0 )
		{
			return;
		}
		if ( nFrame < 0 )
		{
			const int nTo = nSequence + nBy;
			if ( nTo >= 0 && nTo < static_cast<int>( m_Mks.m_Sequences.size() ) )
			{
				std::swap( m_Mks.m_Sequences[ static_cast<std::size_t>( nSequence ) ], m_Mks.m_Sequences[ static_cast<std::size_t>( nTo ) ] );
				FillTree();
				m_pTree->setCurrentItem( m_pTree->topLevelItem( nTo ) );
			}
			return;
		}
		auto &frames = m_Mks.m_Sequences[ static_cast<std::size_t>( nSequence ) ].m_Frames;
		const int nTo = nFrame + nBy;
		if ( nTo >= 0 && nTo < static_cast<int>( frames.size() ) )
		{
			std::swap( frames[ static_cast<std::size_t>( nFrame ) ], frames[ static_cast<std::size_t>( nTo ) ] );
			FillTree();
			m_pTree->setCurrentItem( m_pTree->topLevelItem( nSequence )->child( nTo ) );
		}
	};
	connect( pUp, &QPushButton::clicked, this, [ move ] { move( -1 ); } );
	connect( pDown, &QPushButton::clicked, this, [ move ] { move( 1 ); } );
	connect( pOpen, &QPushButton::clicked, this, [ this ] {
		const QString szFile = AskOpenFile( this, tr( "Open .mks" ), {}, tr( "mksheet files (*.mks);;All files (*)" ) );
		if ( !szFile.isEmpty() )
		{
			LoadMks( szFile );
		}
	} );
	connect( pSave, &QPushButton::clicked, this, [ this ] {
		const QString szFile = AskSaveFile( this, tr( "Save .mks" ), {}, tr( "mksheet files (*.mks)" ) );
		if ( !szFile.isEmpty() && !SaveMks( szFile ) )
		{
			QMessageBox::warning( this, tr( "Sprite Sheet" ), tr( "Could not write %1." ).arg( szFile ) );
		}
	} );
	connect( pCut, &QPushButton::clicked, this, &CSheetDialog::StartFromTexture );
	connect( pSettingsButton, &QPushButton::clicked, this, [ this ] {
		CTextureOptionsDialog dialog( CTextureOptionsDialog::MODE_BATCH, {}, m_Options, this );
		if ( dialog.exec() == QDialog::Accepted )
		{
			m_Options = dialog.GetOptions();
			m_Options.m_nResize = ResizeMode_t::RESIZE_KEEP;
			UpdatePreview();
		}
	} );
	connect( m_pTree, &QTreeWidget::currentItemChanged, this, [ this ] {
		const auto [ nSequence, nFrame ] = Picked();
		m_bFilling = true;
		m_pNumber->setEnabled( nSequence >= 0 );
		m_pLoop->setEnabled( nSequence >= 0 );
		m_pDuration->setEnabled( nFrame >= 0 );
		if ( nSequence >= 0 )
		{
			const MksSequence_t &sequence = m_Mks.m_Sequences[ static_cast<std::size_t>( nSequence ) ];
			m_pNumber->setValue( sequence.m_nNumber );
			m_pLoop->setChecked( sequence.m_bLoop );
			if ( nFrame >= 0 )
			{
				m_pDuration->setValue( static_cast<double>( sequence.m_Frames[ static_cast<std::size_t>( nFrame ) ].m_flDuration ) );
			}
		}
		m_bFilling = false;
	} );
	connect( m_pNumber, &QSpinBox::valueChanged, this, &CSheetDialog::EditPicked );
	connect( m_pLoop, &QCheckBox::toggled, this, &CSheetDialog::EditPicked );
	connect( m_pDuration, &QDoubleSpinBox::valueChanged, this, &CSheetDialog::EditPicked );
	connect( m_pPadding, &QSpinBox::valueChanged, this, &CSheetDialog::UpdatePreview );
	FillTree();
}

//-----------------------------------------------------------------------------
// Purpose: The sequence (and frame, or -1) the tree has picked.
//-----------------------------------------------------------------------------
std::pair<int, int> CSheetDialog::Picked() const
{
	QTreeWidgetItem *pItem = m_pTree->currentItem();
	if ( !pItem )
	{
		return { -1, -1 };
	}
	if ( QTreeWidgetItem *pParent = pItem->parent() )
	{
		return { m_pTree->indexOfTopLevelItem( pParent ), pParent->indexOfChild( pItem ) };
	}
	return { m_pTree->indexOfTopLevelItem( pItem ), -1 };
}

//-----------------------------------------------------------------------------
// Purpose: The number, loop and duration boxes write to what is picked.
//-----------------------------------------------------------------------------
void CSheetDialog::EditPicked()
{
	const auto [ nSequence, nFrame ] = Picked();
	if ( m_bFilling || nSequence < 0 )
	{
		return;
	}
	MksSequence_t &sequence = m_Mks.m_Sequences[ static_cast<std::size_t>( nSequence ) ];
	sequence.m_nNumber = m_pNumber->value();
	sequence.m_bLoop = m_pLoop->isChecked();
	if ( nFrame >= 0 )
	{
		sequence.m_Frames[ static_cast<std::size_t>( nFrame ) ].m_flDuration = static_cast<float>( m_pDuration->value() );
	}
	FillTree();
	m_pTree->setCurrentItem( nFrame >= 0 ? m_pTree->topLevelItem( nSequence )->child( nFrame ) : m_pTree->topLevelItem( nSequence ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSheetDialog::FillTree()
{
	const QSignalBlocker blocker{ m_pTree };
	m_pTree->clear();
	for ( const MksSequence_t &sequence : m_Mks.m_Sequences )
	{
		auto *pSequence = new QTreeWidgetItem( m_pTree, { tr( "Sequence %1%2" ).arg( sequence.m_nNumber ).arg( sequence.m_bLoop ? tr( " · loops" ) : QString{} ), tr( "%n frame(s)", nullptr, static_cast<int>( sequence.m_Frames.size() ) ) } );
		for ( const MksFrame_t &frame : sequence.m_Frames )
		{
			QStringList files;
			for ( const std::string &szFile : frame.m_Files )
			{
				files << Qs( szFile );
			}
			auto *pFrame = new QTreeWidgetItem( pSequence, { files.join( QStringLiteral( " + " ) ), QString::number( static_cast<double>( frame.m_flDuration ) ) } );
			const auto it = m_Pictures.find( frame.m_Files.front() );
			if ( it != m_Pictures.end() )
			{
				pFrame->setIcon( 0, QIcon( QPixmap::fromImage( ToQImage( it->second ).scaled( 24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation ) ) ) );
			}
			else
			{
				pFrame->setToolTip( 0, tr( "No picture of this name" ) );
			}
		}
		pSequence->setExpanded( true );
	}
	m_pTree->resizeColumnToContents( 0 );
	UpdatePreview();
}

//-----------------------------------------------------------------------------
// Purpose: The packed sheet as it would be made, and what's wrong if it can't be.
//-----------------------------------------------------------------------------
void CSheetDialog::UpdatePreview()
{
	std::string szError;
	const auto packed = m_Mks.m_Sequences.empty() ? std::nullopt : PackSheet( m_Mks, m_Pictures, &szError, m_pPadding->value() );
	QStringList notes;
	for ( const std::string &szProblem : m_Mks.m_Problems )
	{
		notes << Qs( szProblem );
	}
	if ( packed )
	{
		m_pPreview->setPixmap( QPixmap::fromImage( ToQImage( packed->m_Atlas ).scaled( m_pPreview->size().boundedTo( QSize( 512, 512 ) ), Qt::KeepAspectRatio, Qt::SmoothTransformation ) ) );
		notes.prepend( tr( "Packs into %1×%2, sheet version %3." ).arg( packed->m_Atlas.m_nWidth ).arg( packed->m_Atlas.m_nHeight ).arg( packed->m_Sheet.m_nVersion ) );
	}
	else
	{
		m_pPreview->setText( m_Mks.m_Sequences.empty() ? tr( "Add a sequence, then frames, or open an .mks file." ) : tr( "Can't pack: %1" ).arg( Qs( szError ) ) );
	}
	m_pNotes->setText( notes.join( u'\n' ) );
	m_pSettings->setText( tr( "%1, version 7.%2" ).arg( QString::fromUtf8( FormatName( m_Options.m_Write.m_nFormat ).data() ) ).arg( std::max( 3, m_Options.m_Write.m_nVersionMinor ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QImage CSheetDialog::GetPreview() const
{
	const auto packed = PackSheet( m_Mks, m_Pictures, nullptr, m_pPadding->value() );
	return packed ? ToQImage( packed->m_Atlas ) : QImage{};
}

//-----------------------------------------------------------------------------
// Purpose: Pictures added to the picked sequence (a new one if none is), one frame each.
//-----------------------------------------------------------------------------
void CSheetDialog::AddFrames( const QStringList &files )
{
	int nSequence = Picked().first;
	if ( nSequence < 0 )
	{
		if ( m_Mks.m_Sequences.empty() )
		{
			m_Mks.m_Sequences.push_back( { 0, true, {} } );
		}
		nSequence = static_cast<int>( m_Mks.m_Sequences.size() ) - 1;
	}
	QStringList failures;
	for ( const QString &szFile : files )
	{
		QString szError;
		std::vector<SourceImage_t> read = ReadSources( szFile, &szError );
		if ( read.empty() )
		{
			failures << QStringLiteral( "%1: %2" ).arg( QFileInfo{ szFile }.fileName(), szError );
			continue;
		}
		// A GIF's frames become frames of their own.
		for ( std::size_t i = 0; i < read.size(); i++ )
		{
			const std::string szName = ( read.size() == 1 ? QFileInfo{ szFile }.fileName() : QStringLiteral( "%1#%2" ).arg( QFileInfo{ szFile }.fileName() ).arg( i ) ).toStdString();
			m_Pictures[ szName ] = read[ i ].m_Light.Empty() ? read[ i ].m_Image : ToEightBit( read[ i ].m_Light );
			m_Mks.m_Sequences[ static_cast<std::size_t>( nSequence ) ].m_Frames.push_back( { { szName }, 1.0f } );
		}
	}
	if ( !failures.isEmpty() )
	{
		QMessageBox::warning( this, tr( "Sprite Sheet" ), failures.join( u'\n' ) );
	}
	FillTree();
}

//-----------------------------------------------------------------------------
// Purpose: An .mks file, with its pictures from beside it.
//-----------------------------------------------------------------------------
bool CSheetDialog::LoadMks( const QString &szPath )
{
	QFile file{ szPath };
	if ( !file.open( QIODevice::ReadOnly ) )
	{
		return false;
	}
	m_Mks = ParseMks( file.readAll().toStdString() );
	m_Pictures.clear();
	const QDir folder = QFileInfo{ szPath }.absoluteDir();
	for ( const MksSequence_t &sequence : m_Mks.m_Sequences )
	{
		for ( const MksFrame_t &frame : sequence.m_Frames )
		{
			for ( const std::string &szName : frame.m_Files )
			{
				if ( m_Pictures.contains( szName ) )
				{
					continue;
				}
				QString szError;
				const auto read = ReadSources( folder.filePath( Qs( szName ) ), &szError );
				if ( read.empty() )
				{
					m_Mks.m_Problems.push_back( szName + ": " + szError.toStdString() );
					continue;
				}
				m_Pictures[ szName ] = read.front().m_Light.Empty() ? read.front().m_Image : ToEightBit( read.front().m_Light );
			}
		}
	}
	FillTree();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSheetDialog::SaveMks( const QString &szPath ) const
{
	QSaveFile file{ szPath };
	const QByteArray text = QByteArray::fromStdString( WriteMks( m_Mks ) );
	return file.open( QIODevice::WriteOnly ) && file.write( text ) == text.size() && file.commit();
}

//-----------------------------------------------------------------------------
// Purpose: The texture's own frames cut out by its sheet, as pictures to change and pack again.
//-----------------------------------------------------------------------------
void CSheetDialog::StartFromTexture()
{
	const auto sheet = m_pVtf ? ReadSheet( m_pVtf->Resource( SHEET_RESOURCE ) ) : std::nullopt;
	const auto atlas = m_pVtf ? m_pVtf->Decode( 0 ) : std::nullopt;
	if ( !sheet || !atlas )
	{
		return;
	}
	m_Mks = {};
	m_Pictures.clear();
	std::map<std::array<int, 4>, std::string> cut; // the same rect is the same picture
	const auto cutOut = [ & ]( const std::array<float, 4> &rect ) {
		const std::array<int, 4> texels{ static_cast<int>( std::lround( rect[ 0 ] * static_cast<float>( atlas->m_nWidth ) ) ), static_cast<int>( std::lround( rect[ 1 ] * static_cast<float>( atlas->m_nHeight ) ) ),
			static_cast<int>( std::lround( rect[ 2 ] * static_cast<float>( atlas->m_nWidth ) ) ), static_cast<int>( std::lround( rect[ 3 ] * static_cast<float>( atlas->m_nHeight ) ) ) };
		if ( const auto it = cut.find( texels ); it != cut.end() )
		{
			return it->second;
		}
		const std::string szName = "frame" + std::to_string( cut.size() ) + ".tga";
		const int nWidth = std::clamp( texels[ 2 ] - texels[ 0 ], 1, atlas->m_nWidth );
		const int nHeight = std::clamp( texels[ 3 ] - texels[ 1 ], 1, atlas->m_nHeight );
		CImage picture{ nWidth, nHeight };
		for ( int y = 0; y < nHeight; y++ )
		{
			for ( int x = 0; x < nWidth; x++ )
			{
				std::memcpy( picture.Pixel( x, y ), atlas->Pixel( std::clamp( texels[ 0 ] + x, 0, atlas->m_nWidth - 1 ), std::clamp( texels[ 1 ] + y, 0, atlas->m_nHeight - 1 ) ), 4 );
			}
		}
		m_Pictures[ szName ] = std::move( picture );
		cut[ texels ] = szName;
		return szName;
	};
	for ( const CSheet::Sequence_t &sequence : sheet->m_Sequences )
	{
		MksSequence_t out{ sequence.m_nNumber, !sequence.m_bClamp, {} };
		for ( const CSheet::Frame_t &frame : sequence.m_Frames )
		{
			MksFrame_t cutFrame{ { cutOut( frame.m_Rect ) }, frame.m_flDuration };
			if ( sheet->m_nVersion >= 1 && frame.m_Others[ 0 ] != frame.m_Rect )
			{
				cutFrame.m_Files.push_back( cutOut( frame.m_Others[ 0 ] ) );
			}
			out.m_Frames.push_back( std::move( cutFrame ) );
		}
		m_Mks.m_Sequences.push_back( std::move( out ) );
	}
	// Keep the texture's own settings.
	m_Options = OptionsOf( *m_pVtf );
	m_Options.m_nResize = ResizeMode_t::RESIZE_KEEP;
	m_Options.m_Write.m_Resources.clear();
	FillTree();
}

//-----------------------------------------------------------------------------
// Purpose: The packed pictures with the sheet resource.
//-----------------------------------------------------------------------------
std::vector<std::byte> CSheetDialog::BuildTexture( std::string *pError ) const
{
	const auto packed = PackSheet( m_Mks, m_Pictures, pError, m_pPadding->value() );
	if ( !packed )
	{
		return {};
	}
	BuildOptions_t options = m_Options;
	options.m_nFaces = 1;
	options.m_Write.m_nDepth = 1;
	options.m_Write.m_nVersionMinor = std::max( 3, options.m_Write.m_nVersionMinor );
	std::erase_if( options.m_Write.m_Resources, []( const VtfExtraResource_t &resource ) { return resource.m_Tag == SHEET_RESOURCE; } );
	options.m_Write.m_Resources.push_back( { SHEET_RESOURCE, 0, 0, WriteSheet( packed->m_Sheet ) } );
	const std::vector<SourceImage_t> sources{ { packed->m_Atlas, {} } };
	return BuildVtf( sources, options, pError );
}
