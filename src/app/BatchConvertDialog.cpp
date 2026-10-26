//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See BatchConvertDialog.h.
//
//===========================================================================//
#include "BatchConvertDialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

#include "FileDialogs.h"
#include "TextureEditor.h"
#include "TextureOptionsDialog.h"

using namespace xv::texture;

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The files a job works on, sorted.
//-----------------------------------------------------------------------------
QStringList FilesOf( const BatchJob_t &job )
{
	QStringList files;
	QDirIterator it( job.m_szInput, job.m_bToVtf ? PicturePatterns() : QStringList{ QStringLiteral( "*.vtf" ) }, QDir::Files,
		job.m_bRecurse ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags );
	while ( it.hasNext() )
	{
		files << it.next();
	}
	files.sort( Qt::CaseInsensitive );
	return files;
}

//-----------------------------------------------------------------------------
// Purpose: A file's place in the output folder: the same sub-folders, its own name.
//-----------------------------------------------------------------------------
QString OutputFolderFor( const BatchJob_t &job, const QString &szFile )
{
	const QString szRelative = QDir{ job.m_szInput }.relativeFilePath( QFileInfo{ szFile }.absolutePath() );
	return QDir::cleanPath( QDir{ job.m_szOutput }.filePath( szRelative ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Runs a job; pfnProgress returns false to stop.
//-----------------------------------------------------------------------------
BatchResult_t RunBatch( const BatchJob_t &job, const std::function<bool( int, int )> &pfnProgress )
{
	BatchResult_t result;
	const QStringList files = FilesOf( job );
	for ( int i = 0; i < files.size(); i++ )
	{
		if ( pfnProgress && !pfnProgress( i, static_cast<int>( files.size() ) ) )
		{
			break;
		}
		const QString &szFile = files[ i ];
		const QFileInfo info{ szFile };
		const QString szFolder = OutputFolderFor( job, szFile );
		if ( !job.m_bToVtf )
		{
			const auto data = [ &szFile ] {
				QFile file{ szFile };
				return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray{};
			}();
			const auto vtf = ReadVtf( std::span{ reinterpret_cast<const std::byte *>( data.constData() ), static_cast<std::size_t>( data.size() ) } );
			if ( vtf && ExportPictures( *vtf, szFolder, info.completeBaseName(), job.m_szExtension ) > 0 )
			{
				result.m_nDone++;
			}
			else
			{
				result.m_Failures << QObject::tr( "%1: couldn't write its pictures" ).arg( info.fileName() );
			}
			continue;
		}
		QString szError;
		const std::vector<SourceImage_t> sources = ReadSources( szFile, &szError );
		if ( sources.empty() )
		{
			result.m_Failures << QStringLiteral( "%1: %2" ).arg( info.fileName(), szError );
			continue;
		}
		// Pictures with see-through parts get a format that keeps them.
		BuildOptions_t options = job.m_Options;
		if ( job.m_bAlphaFormat && options.m_nAlphaFormat == VtfFormat_t::VTF_FORMAT_NONE )
		{
			options.m_nAlphaFormat = job.m_nAlphaFormat;
		}
		std::string szBuildError;
		const auto bytes = BuildVtf( sources, options, &szBuildError );
		QDir{}.mkpath( szFolder );
		QSaveFile out{ QDir{ szFolder }.filePath( info.completeBaseName() + QStringLiteral( ".vtf" ) ) };
		if ( bytes.empty() || !out.open( QIODevice::WriteOnly ) || out.write( reinterpret_cast<const char *>( bytes.data() ), static_cast<qint64>( bytes.size() ) ) != static_cast<qint64>( bytes.size() ) || !out.commit() )
		{
			result.m_Failures << QStringLiteral( "%1: %2" ).arg( info.fileName(), bytes.empty() ? QString::fromStdString( szBuildError ) : out.errorString() );
			continue;
		}
		result.m_nDone++;
	}
	if ( pfnProgress )
	{
		pfnProgress( static_cast<int>( files.size() ), static_cast<int>( files.size() ) );
	}
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: szStart is the folder offered first.
//-----------------------------------------------------------------------------
CBatchConvertDialog::CBatchConvertDialog( const QString &szStart, QWidget *pParent )
	: QDialog( pParent ),
	  m_Options( CTextureOptionsDialog::DefaultsFor( {} ) )
{
	setWindowTitle( tr( "Convert a Folder" ) );
	auto *pForm = new QFormLayout;
	pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );

	m_pDirection = new QComboBox;
	m_pDirection->addItem( tr( "Pictures into textures (VTF)" ), true );
	m_pDirection->addItem( tr( "Textures (VTF) into pictures" ), false );
	pForm->addRow( tr( "Make" ), m_pDirection );

	const auto folderRow = [ this ]( QLineEdit *pEdit, const QString &szTitle ) {
		auto *pRow = new QHBoxLayout;
		auto *pBrowse = new QPushButton( tr( "Browse…" ) );
		connect( pBrowse, &QPushButton::clicked, this, [ this, pEdit, szTitle ] {
			const QString szFolder = AskFolder( this, szTitle, pEdit->text() );
			if ( !szFolder.isEmpty() )
			{
				pEdit->setText( QDir::toNativeSeparators( szFolder ) );
			}
		} );
		pRow->addWidget( pEdit, 1 );
		pRow->addWidget( pBrowse );
		return pRow;
	};
	m_pInput = new QLineEdit( QDir::toNativeSeparators( szStart ) );
	m_pOutput = new QLineEdit( QDir::toNativeSeparators( szStart ) );
	m_pOutput->setToolTip( tr( "Sub-folders are kept; files of the same name are replaced" ) );
	pForm->addRow( tr( "From folder" ), folderRow( m_pInput, tr( "Convert From" ) ) );
	pForm->addRow( tr( "Into folder" ), folderRow( m_pOutput, tr( "Convert Into" ) ) );
	m_pRecurse = new QCheckBox( tr( "Sub-folders too" ) );
	m_pRecurse->setChecked( true );
	pForm->addRow( QString{}, m_pRecurse );

	m_pExtension = new QComboBox;
	m_pExtension->addItems( PictureTypesForSaving() );
	m_pExtension->setToolTip( tr( "hdr keeps HDR textures' light values" ) );
	pForm->addRow( tr( "Pictures as" ), m_pExtension );

	auto *pSettingsRow = new QHBoxLayout;
	m_pSettings = new QLabel;
	m_pAlphaFormat = new QCheckBox( tr( "Pictures with see-through parts are made DXT5" ) );
	m_pAlphaFormat->setChecked( true );
	m_pAlphaFormat->setToolTip( tr( "When the format above has no alpha" ) );
	auto *pSettings = new QPushButton( tr( "Settings…" ) );
	connect( pSettings, &QPushButton::clicked, this, [ this ] {
		CTextureOptionsDialog dialog( CTextureOptionsDialog::MODE_BATCH, {}, m_Options, this );
		if ( dialog.exec() == QDialog::Accepted )
		{
			m_Options = dialog.GetOptions();
			Update();
		}
	} );
	pSettingsRow->addWidget( m_pSettings, 1 );
	pSettingsRow->addWidget( pSettings );
	pForm->addRow( tr( "Textures" ), pSettingsRow );
	pForm->addRow( QString{}, m_pAlphaFormat );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pButtons->button( QDialogButtonBox::Ok )->setText( tr( "Convert" ) );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( pForm );
	pLayout->addWidget( pButtons );
	connect( m_pDirection, &QComboBox::currentIndexChanged, this, &CBatchConvertDialog::Update );
	resize( 620, sizeHint().height() );
	Update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBatchConvertDialog::Update()
{
	const bool bToVtf = m_pDirection->currentData().toBool();
	m_pExtension->setEnabled( !bToVtf );
	m_pSettings->setEnabled( bToVtf );
	m_pAlphaFormat->setEnabled( bToVtf );
	m_pSettings->setText( tr( "%1, version 7.%2%3" )
			.arg( QString::fromUtf8( FormatName( m_Options.m_Write.m_nFormat ).data() ) )
			.arg( m_Options.m_Write.m_nVersionMinor )
			.arg( m_Options.m_Write.m_bMipmaps ? tr( ", mipmaps" ) : QString{} ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
BatchJob_t CBatchConvertDialog::GetJob() const
{
	BatchJob_t job;
	job.m_szInput = QDir::fromNativeSeparators( m_pInput->text().trimmed() );
	job.m_szOutput = QDir::fromNativeSeparators( m_pOutput->text().trimmed() );
	job.m_bRecurse = m_pRecurse->isChecked();
	job.m_bToVtf = m_pDirection->currentData().toBool();
	job.m_szExtension = m_pExtension->currentText();
	job.m_Options = m_Options;
	job.m_bAlphaFormat = m_pAlphaFormat->isChecked();
	return job;
}
