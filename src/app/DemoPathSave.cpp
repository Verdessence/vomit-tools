//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoPathSave.h.
//
//===========================================================================//
#include "DemoPathSave.h"

#include <chrono>
#include <future>
#include <utility>

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include "FileDialogs.h"

namespace
{

constexpr char SETTINGS[] = "demoViewer/pathPicture/";

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Key( const char *pszName )
{
	return QString::fromLatin1( SETTINGS ) + QLatin1String( pszName );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The picture's contents above, the file's format and options below; formats this
//          build can't write aren't offered.
//-----------------------------------------------------------------------------
CDemoPathSaveDialog::CDemoPathSaveDialog( QWidget *pParent )
	: QDialog( pParent )
{
	setObjectName( QStringLiteral( "demoPathSave" ) );
	setWindowTitle( tr( "Save Image" ) );
	const QSettings settings;

	m_pScale = new QComboBox;
	m_pScale->setObjectName( QStringLiteral( "demoPathSaveScale" ) );
	for ( const double flScale : { 1.0, 2.0, 3.0, 4.0 } )
		m_pScale->addItem( tr( "%1× the view" ).arg( flScale ), flScale );
	m_pScale->setToolTip( tr( "How big the picture is: the paths are drawn again at that size, so their lines stay sharp; the map and heatmap are scaled" ) );
	m_pScale->setCurrentIndex( std::max( 0, m_pScale->findData( settings.value( Key( "scale" ), 2.0 ).toDouble() ) ) );
	m_pPlayed = new QComboBox;
	m_pPlayed->setObjectName( QStringLiteral( "demoPathSavePlayed" ) );
	m_pPlayed->addItem( tr( "Up to the playhead, as shown" ), false );
	m_pPlayed->addItem( tr( "The whole demo" ), true );
	m_pPlayed->setCurrentIndex( settings.value( Key( "wholeDemo" ), false ).toBool() ? 1 : 0 );
	m_pMap = new QCheckBox( tr( "The map underneath" ) );
	m_pMap->setObjectName( QStringLiteral( "demoPathSaveMap" ) );
	m_pMap->setChecked( settings.value( Key( "map" ), true ).toBool() );
	m_pTransparent = new QCheckBox( tr( "Transparent background" ) );
	m_pTransparent->setObjectName( QStringLiteral( "demoPathSaveTransparent" ) );
	m_pTransparent->setToolTip( tr( "Without the map, only the paths (and heatmap) on a clear background, for laying over something else" ) );
	m_pTransparent->setChecked( settings.value( Key( "transparent" ), false ).toBool() );
	m_pPlayers = new QCheckBox( tr( "The players where they are" ) );
	m_pPlayers->setObjectName( QStringLiteral( "demoPathSavePlayers" ) );
	m_pPlayers->setChecked( settings.value( Key( "players" ), true ).toBool() );

	m_pFormat = new QComboBox;
	m_pFormat->setObjectName( QStringLiteral( "demoPathSaveFormat" ) );
	for ( const ImageExportFormat_t nFormat : { ImageExportFormat_t::IMAGE_EXPORT_FORMAT_JXL, ImageExportFormat_t::IMAGE_EXPORT_FORMAT_AVIF, ImageExportFormat_t::IMAGE_EXPORT_FORMAT_PNG } )
	{
		if ( CanExportImage( nFormat ) )
			m_pFormat->addItem( ImageExportName( nFormat ), static_cast<int>( nFormat ) );
	}
	if ( !CanExportImage( ImageExportFormat_t::IMAGE_EXPORT_FORMAT_AVIF ) )
		m_pFormat->setToolTip( tr( "AVIF needs libavif with an encoder, or Qt's AVIF image plugin, neither of which this build has" ) );
	m_pFormat->setCurrentIndex( std::max( 0, m_pFormat->findData( settings.value( Key( "format" ), 0 ).toInt() ) ) );
	m_pLossless = new QCheckBox( tr( "Lossless" ) );
	m_pLossless->setObjectName( QStringLiteral( "demoPathSaveLossless" ) );
	m_pLossless->setToolTip( tr( "Every pixel as drawn; larger files" ) );
	m_pLossless->setChecked( settings.value( Key( "lossless" ), false ).toBool() );
	m_pQuality = new QSlider( Qt::Horizontal );
	m_pQuality->setObjectName( QStringLiteral( "demoPathSaveQuality" ) );
	m_pQuality->setRange( 0, 99 );
	m_pQualityValue = new QSpinBox;
	m_pQualityValue->setRange( 0, 99 );
	connect( m_pQuality, &QSlider::valueChanged, m_pQualityValue, &QSpinBox::setValue );
	connect( m_pQualityValue, &QSpinBox::valueChanged, m_pQuality, &QSlider::setValue );
	m_pQuality->setValue( settings.value( Key( "quality" ), 90 ).toInt() );
	m_pQuality->setToolTip( tr( "Higher keeps more detail in a larger file; 90 is hard to tell from lossless" ) );
	m_pEffortLabel = new QLabel;
	m_pEffort = new QSlider( Qt::Horizontal );
	m_pEffort->setObjectName( QStringLiteral( "demoPathSaveEffort" ) );
	m_pEffortValue = new QLabel;
	connect( m_pEffort, &QSlider::valueChanged, this, &CDemoPathSaveDialog::UpdateEnabled );
	auto *pQualityRow = new QHBoxLayout;
	pQualityRow->addWidget( m_pQuality, 1 );
	pQualityRow->addWidget( m_pQualityValue );
	auto *pEffortRow = new QHBoxLayout;
	pEffortRow->addWidget( m_pEffort, 1 );
	pEffortRow->addWidget( m_pEffortValue );

	auto *pPicture = new QFormLayout;
	pPicture->addRow( tr( "Size" ), m_pScale );
	pPicture->addRow( tr( "Paths" ), m_pPlayed );
	pPicture->addRow( QString{}, m_pMap );
	pPicture->addRow( QString{}, m_pTransparent );
	pPicture->addRow( QString{}, m_pPlayers );
	auto *pFile = new QFormLayout;
	pFile->addRow( tr( "Format" ), m_pFormat );
	pFile->addRow( QString{}, m_pLossless );
	pFile->addRow( tr( "Quality" ), pQualityRow );
	pFile->addRow( m_pEffortLabel, pEffortRow );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Save | QDialogButtonBox::Cancel );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( pPicture );
	pLayout->addSpacing( 8 );
	pLayout->addLayout( pFile );
	pLayout->addWidget( pButtons );

	// Effort is kept per format: their scales mean different things.
	const auto loadEffort = [ this ] {
		const auto nFormat = static_cast<ImageExportFormat_t>( m_pFormat->currentData().toInt() );
		m_pEffort->setRange( nFormat == ImageExportFormat_t::IMAGE_EXPORT_FORMAT_PNG ? 0 : 1, 9 );
		// AVIF's encoders are several times slower than JPEG XL's at the same effort.
		const int nDefault = nFormat == ImageExportFormat_t::IMAGE_EXPORT_FORMAT_PNG ? 6 : nFormat == ImageExportFormat_t::IMAGE_EXPORT_FORMAT_AVIF ? 4
																																					: 7;
		m_pEffort->setValue( QSettings{}.value( Key( "effort/" ) + ImageExportSuffix( nFormat ), nDefault ).toInt() );
		UpdateEnabled();
	};
	connect( m_pFormat, &QComboBox::currentIndexChanged, this, loadEffort );
	connect( m_pLossless, &QCheckBox::toggled, this, &CDemoPathSaveDialog::UpdateEnabled );
	connect( m_pMap, &QCheckBox::toggled, this, &CDemoPathSaveDialog::UpdateEnabled );
	loadEffort();
}

//-----------------------------------------------------------------------------
// Purpose: Quality only for a lossy format; the effort row says what it means for the format;
//          a clear background only without the map.
//-----------------------------------------------------------------------------
void CDemoPathSaveDialog::UpdateEnabled()
{
	const auto nFormat = static_cast<ImageExportFormat_t>( m_pFormat->currentData().toInt() );
	const bool bPng = nFormat == ImageExportFormat_t::IMAGE_EXPORT_FORMAT_PNG;
	m_pLossless->setEnabled( !bPng );
	m_pQuality->setEnabled( !bPng && !m_pLossless->isChecked() );
	m_pQualityValue->setEnabled( m_pQuality->isEnabled() );
	m_pEffortLabel->setText( bPng ? tr( "Compression" ) : tr( "Effort" ) );
	m_pEffort->setToolTip( bPng ? tr( "0 stores the pixels as they are; 9 makes the smallest file, slowest" ) : tr( "1 is quickest; 9 makes the smallest file at the same quality, slowest (AVIF: its encoder's speed, turned round)" ) );
	m_pEffortValue->setText( QString::number( m_pEffort->value() ) );
	m_pTransparent->setEnabled( !m_pMap->isChecked() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
DemoPathPicture_t CDemoPathSaveDialog::GetPicture() const
{
	DemoPathPicture_t picture;
	picture.m_flScale = m_pScale->currentData().toDouble();
	picture.m_bMap = m_pMap->isChecked();
	picture.m_bTransparent = !picture.m_bMap && m_pTransparent->isChecked();
	picture.m_bWholeDemo = m_pPlayed->currentData().toBool();
	picture.m_bPlayers = m_pPlayers->isChecked();
	return picture;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
ImageExportOptions_t CDemoPathSaveDialog::GetOptions() const
{
	ImageExportOptions_t options;
	options.m_nFormat = static_cast<ImageExportFormat_t>( m_pFormat->currentData().toInt() );
	options.m_bLossless = m_pLossless->isChecked();
	options.m_nQuality = m_pQuality->value();
	options.m_nEffort = m_pEffort->value();
	options.m_nPngLevel = m_pEffort->value();
	return options;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoPathSaveDialog::Remember() const
{
	QSettings settings;
	const DemoPathPicture_t picture = GetPicture();
	const ImageExportOptions_t options = GetOptions();
	settings.setValue( Key( "scale" ), picture.m_flScale );
	settings.setValue( Key( "wholeDemo" ), picture.m_bWholeDemo );
	settings.setValue( Key( "map" ), picture.m_bMap );
	settings.setValue( Key( "transparent" ), m_pTransparent->isChecked() );
	settings.setValue( Key( "players" ), picture.m_bPlayers );
	settings.setValue( Key( "format" ), static_cast<int>( options.m_nFormat ) );
	settings.setValue( Key( "lossless" ), options.m_bLossless );
	settings.setValue( Key( "quality" ), options.m_nQuality );
	settings.setValue( Key( "effort/" ) + ImageExportSuffix( options.m_nFormat ), m_pEffort->value() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool SaveDemoPathPicture( CDemoPathView &view, QWidget *pParent, const QString &szName )
{
	CDemoPathSaveDialog dialog( pParent );
	if ( dialog.exec() != QDialog::Accepted )
		return false;
	dialog.Remember();
	const ImageExportOptions_t options = dialog.GetOptions();
	const QString szSuffix = ImageExportSuffix( options.m_nFormat );
	QString szFile = AskSaveFile( pParent, QObject::tr( "Save Image" ), QFileInfo( szName ).completeBaseName() + QStringLiteral( "_paths." ) + szSuffix,
		QObject::tr( "%1 (*.%2)" ).arg( ImageExportName( options.m_nFormat ), szSuffix ) );
	if ( szFile.isEmpty() )
		return false;
	if ( QFileInfo( szFile ).suffix().isEmpty() )
		szFile += QLatin1Char( '.' ) + szSuffix;
	// Encoding takes seconds for a big picture (AVIF the longest): done on a worker, the window
	// kept drawing meanwhile.
	QProgressDialog progress( QObject::tr( "Saving %1…" ).arg( QFileInfo( szFile ).fileName() ), QString{}, 0, 0, pParent );
	progress.setWindowTitle( QObject::tr( "Save Image" ) );
	progress.setWindowModality( Qt::WindowModal );
	progress.setMinimumDuration( 0 );
	progress.show();
	QCoreApplication::processEvents();
	const QImage picture = view.RenderPicture( dialog.GetPicture() );
	auto job = std::async( std::launch::async, [ picture, szFile, options ] {
		QString szError;
		const bool bSaved = ExportImage( picture, szFile, options, &szError );
		return std::pair{ bSaved, szError };
	} );
	while ( job.wait_for( std::chrono::milliseconds( 30 ) ) != std::future_status::ready )
		QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
	const auto [ bSaved, szError ] = job.get();
	progress.close();
	if ( !bSaved )
	{
		QMessageBox::warning( pParent, QObject::tr( "Save Image" ), QObject::tr( "Could not save %1: %2" ).arg( QDir::toNativeSeparators( szFile ), szError ) );
		return false;
	}
	return true;
}
