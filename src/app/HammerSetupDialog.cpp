//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See HammerSetupDialog.h.
//
//===========================================================================//
#include "HammerSetupDialog.h"

#include <fstream>
#include <iterator>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QVBoxLayout>

#include "FileDialogs.h"
#include "vfs/Vfs.h"

namespace
{
// The paths the dialog edits: the config's key, its label, and whether it is a folder.
struct PathField_t
{
	const char *m_pszKey;
	const char *m_pszLabel;
	bool m_bFolder;
};
constexpr PathField_t PATH_FIELDS[] = {
	{ "GameExe", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "Game executable" ), false },
	{ "BSP", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "BSP executable" ), false },
	{ "Vis", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "VIS executable" ), false },
	{ "Light", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "RAD executable" ), false },
	{ "MDL", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "Model compiler" ), false },
	{ "GameExeDir", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "Game executable folder" ), true },
	{ "MapDir", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "Map source folder" ), true },
	{ "BSPDir", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "Compiled maps folder" ), true },
	{ "PrefabDir", QT_TRANSLATE_NOOP( "CHammerSetupDialog", "Prefabs folder" ), true },
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString ToQString( const std::filesystem::path &path )
{
	return QString::fromStdString( xv::vfs::ToUtf8( path ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::filesystem::path ToPath( const QString &sz )
{
	return xv::vfs::FromUtf8( sz.trimmed().toStdString() );
}

//-----------------------------------------------------------------------------
// Purpose: The configuration's text, or nothing when there is no file yet.
//-----------------------------------------------------------------------------
std::string ReadText( const std::filesystem::path &file )
{
	std::ifstream stream{ file, std::ios::binary };
	return stream ? std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} } : std::string{};
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CHammerSetupDialog::CHammerSetupDialog( const std::filesystem::path &gameDir, const QString &szGameName, QWidget *pParent )
	: QDialog( pParent ),
	  m_GameDir( gameDir ),
	  m_szGameName( szGameName )
{
	setObjectName( QStringLiteral( "hammerSetup" ) );
	setWindowTitle( tr( "Set Up Hammer" ) );
	auto *pLayout = new QVBoxLayout( this );
	auto *pForm = new QFormLayout;
	pLayout->addLayout( pForm );

	m_pKind = new QComboBox;
	m_pKind->setObjectName( QStringLiteral( "hammerSetupKind" ) );
	m_pKind->addItem( tr( "Hammer++" ), static_cast<int>( xv::map::HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS ) );
	m_pKind->addItem( tr( "Hammer" ), static_cast<int>( xv::map::HammerKind_t::HAMMER_KIND_HAMMER ) );
	pForm->addRow( tr( "Editor" ), m_pKind );
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "hammerSetupStatus" ) );
	m_pStatus->setWordWrap( true );
	m_pStatus->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pForm->addRow( m_pStatus );

	m_pName = new QLineEdit;
	m_pName->setObjectName( QStringLiteral( "hammerSetupName" ) );
	pForm->addRow( tr( "Configuration name" ), m_pName );
	m_pFgds = new QPlainTextEdit;
	m_pFgds->setObjectName( QStringLiteral( "hammerSetupFgds" ) );
	m_pFgds->setToolTip( tr( "The game data files, one a line, in the order Hammer reads them" ) );
	m_pFgds->setFixedHeight( fontMetrics().lineSpacing() * 4 + 12 );
	auto *pFgdRow = new QHBoxLayout;
	auto *pAddFgd = new QPushButton( tr( "Add…" ) );
	pFgdRow->addWidget( m_pFgds, 1 );
	pFgdRow->addWidget( pAddFgd, 0, Qt::AlignTop );
	pForm->addRow( tr( "Game data files" ), pFgdRow );
	connect( pAddFgd, &QPushButton::clicked, this, [ this ] {
		for ( const QString &szFile : AskOpenFiles( this, tr( "Game Data Files" ), ToQString( m_GameDir.parent_path() / "bin" ), tr( "Game data (*.fgd)" ) ) )
			m_pFgds->appendPlainText( szFile );
	} );

	for ( const PathField_t &field : PATH_FIELDS )
	{
		auto *pEdit = new QLineEdit;
		pEdit->setObjectName( QStringLiteral( "hammerSetup%1" ).arg( QLatin1String( field.m_pszKey ) ) );
		auto *pBrowse = new QPushButton( tr( "Browse…" ) );
		auto *pRow = new QHBoxLayout;
		pRow->addWidget( pEdit, 1 );
		pRow->addWidget( pBrowse );
		pForm->addRow( tr( field.m_pszLabel ), pRow );
		m_Paths[ field.m_pszKey ] = pEdit;
		const bool bFolder = field.m_bFolder;
		const QString szLabel = tr( field.m_pszLabel );
		connect( pBrowse, &QPushButton::clicked, this, [ this, pEdit, bFolder, szLabel ] {
			const QString szStart = pEdit->text().isEmpty() ? ToQString( m_GameDir.parent_path() ) : pEdit->text();
			const QString szPicked = bFolder ? AskFolder( this, szLabel, szStart ) : AskOpenFile( this, szLabel, szStart );
			if ( !szPicked.isEmpty() )
				pEdit->setText( szPicked );
		} );
	}

	m_pPointEntity = new QLineEdit;
	m_pPointEntity->setObjectName( QStringLiteral( "hammerSetupPointEntity" ) );
	pForm->addRow( tr( "Default point entity" ), m_pPointEntity );
	m_pSolidEntity = new QLineEdit;
	m_pSolidEntity->setObjectName( QStringLiteral( "hammerSetupSolidEntity" ) );
	pForm->addRow( tr( "Default brush entity" ), m_pSolidEntity );
	m_pTextureScale = new QDoubleSpinBox;
	m_pTextureScale->setRange( 0.01, 16.0 );
	m_pTextureScale->setDecimals( 3 );
	m_pTextureScale->setSingleStep( 0.05 );
	pForm->addRow( tr( "Default texture scale" ), m_pTextureScale );
	m_pLightmapScale = new QSpinBox;
	m_pLightmapScale->setRange( 1, 512 );
	pForm->addRow( tr( "Default lightmap scale" ), m_pLightmapScale );
	m_pCordonTexture = new QLineEdit;
	pForm->addRow( tr( "Cordon texture" ), m_pCordonTexture );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Save | QDialogButtonBox::Cancel );
	pButtons->setObjectName( QStringLiteral( "hammerSetupButtons" ) );
	auto *pSuggest = pButtons->addButton( tr( "Fill from Game Folders" ), QDialogButtonBox::ResetRole );
	pSuggest->setToolTip( tr( "What the game's folders hold, instead of the configuration's entry" ) );
	pLayout->addWidget( pButtons );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	connect( pButtons, &QDialogButtonBox::accepted, this, [ this ] {
		QString szError;
		if ( Save( &szError ) )
			accept();
		else
			QMessageBox::warning( this, windowTitle(), szError );
	} );
	auto fill = [ this ]( const xv::map::HammerSetup_t &setup ) {
		m_pName->setText( QString::fromStdString( setup.m_szName ) );
		QStringList fgds;
		for ( const auto &fgd : setup.m_Fgds )
			fgds << ToQString( fgd );
		m_pFgds->setPlainText( fgds.join( QLatin1Char( '\n' ) ) );
		const std::map<std::string, std::filesystem::path> paths = { { "GameExe", setup.m_GameExe }, { "BSP", setup.m_Bsp }, { "Vis", setup.m_Vis },
			{ "Light", setup.m_Light }, { "MDL", setup.m_Mdl }, { "GameExeDir", setup.m_GameExeDir }, { "MapDir", setup.m_MapDir }, { "BSPDir", setup.m_BspDir },
			{ "PrefabDir", setup.m_PrefabDir } };
		for ( const auto &[ szKey, path ] : paths )
			m_Paths[ szKey ]->setText( ToQString( path ) );
		m_pPointEntity->setText( QString::fromStdString( setup.m_Defaults.m_szPointEntity ) );
		m_pSolidEntity->setText( QString::fromStdString( setup.m_Defaults.m_szSolidEntity ) );
		m_pTextureScale->setValue( setup.m_Defaults.m_flTextureScale );
		m_pLightmapScale->setValue( setup.m_Defaults.m_nLightmapScale );
		m_pCordonTexture->setText( QString::fromStdString( setup.m_szCordonTexture ) );
	};
	connect( pSuggest, &QPushButton::clicked, this, [ this, fill ] { fill( xv::map::SuggestHammerSetup( m_GameDir, m_szGameName.toStdString() ) ); } );
	connect( m_pKind, &QComboBox::currentIndexChanged, this, &CHammerSetupDialog::Fill );
	// Hammer++ when it is installed, else Hammer when it is.
	if ( !xv::map::HammerProgram( m_GameDir, xv::map::HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS ) && xv::map::HammerProgram( m_GameDir, xv::map::HammerKind_t::HAMMER_KIND_HAMMER ) )
		m_pKind->setCurrentIndex( 1 );
	m_pfnFill = fill;
	Fill();
	resize( 720, sizeHint().height() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::HammerKind_t CHammerSetupDialog::GetKind() const
{
	return static_cast<xv::map::HammerKind_t>( m_pKind->currentData().toInt() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::filesystem::path CHammerSetupDialog::GetConfigFile() const
{
	return xv::map::HammerConfigFile( m_GameDir, GetKind() );
}

//-----------------------------------------------------------------------------
// Purpose: The entry the configuration has for the game, else the folders' suggestion.
//-----------------------------------------------------------------------------
void CHammerSetupDialog::Fill()
{
	const std::filesystem::path file = GetConfigFile();
	const std::optional<std::filesystem::path> program = xv::map::HammerProgram( m_GameDir, GetKind() );
	const std::optional<xv::map::HammerSetup_t> existing = xv::map::ReadHammerSetup( ReadText( file ), m_GameDir );
	QStringList status;
	status << ( program ? tr( "Installed: %1" ).arg( ToQString( *program ) ) : tr( "Not installed beside this game: the entry is written for when it is." ) );
	status << ( existing ? tr( "Editing this game's entry in %1" ).arg( ToQString( file ) ) : tr( "This game has no entry yet; saving adds one to %1" ).arg( ToQString( file ) ) );
	m_pStatus->setText( status.join( QLatin1Char( '\n' ) ) );
	m_pfnFill( existing.value_or( xv::map::SuggestHammerSetup( m_GameDir, m_szGameName.toStdString() ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::HammerSetup_t CHammerSetupDialog::GetSetup() const
{
	xv::map::HammerSetup_t setup;
	setup.m_szName = m_pName->text().trimmed().toStdString();
	setup.m_GameDir = m_GameDir;
	for ( const QString &szLine : m_pFgds->toPlainText().split( QLatin1Char( '\n' ), Qt::SkipEmptyParts ) )
		if ( !szLine.trimmed().isEmpty() )
			setup.m_Fgds.push_back( ToPath( szLine ) );
	auto path = [ & ]( const char *pszKey ) { return ToPath( m_Paths.at( pszKey )->text() ); };
	setup.m_GameExe = path( "GameExe" );
	setup.m_Bsp = path( "BSP" );
	setup.m_Vis = path( "Vis" );
	setup.m_Light = path( "Light" );
	setup.m_Mdl = path( "MDL" );
	setup.m_GameExeDir = path( "GameExeDir" );
	setup.m_MapDir = path( "MapDir" );
	setup.m_BspDir = path( "BSPDir" );
	setup.m_PrefabDir = path( "PrefabDir" );
	setup.m_Defaults.m_szPointEntity = m_pPointEntity->text().trimmed().toStdString();
	setup.m_Defaults.m_szSolidEntity = m_pSolidEntity->text().trimmed().toStdString();
	setup.m_Defaults.m_flTextureScale = m_pTextureScale->value();
	setup.m_Defaults.m_nLightmapScale = m_pLightmapScale->value();
	setup.m_szCordonTexture = m_pCordonTexture->text().trimmed().toStdString();
	return setup;
}

//-----------------------------------------------------------------------------
// Purpose: The first save keeps Hammer's own file beside it, in case the entry was hand-made.
//-----------------------------------------------------------------------------
bool CHammerSetupDialog::Save( QString *pError )
{
	const xv::map::HammerSetup_t setup = GetSetup();
	if ( setup.m_szName.empty() )
	{
		*pError = tr( "The configuration needs a name." );
		return false;
	}
	if ( setup.m_Fgds.empty() )
	{
		*pError = tr( "Hammer needs at least one game data file." );
		return false;
	}
	const std::filesystem::path file = GetConfigFile();
	std::error_code error;
	std::filesystem::create_directories( file.parent_path(), error );
	const std::string szExisting = ReadText( file );
	const QString szFile = ToQString( file );
	if ( !szExisting.empty() && !QFile::exists( szFile + QStringLiteral( ".bak" ) ) )
		QFile::copy( szFile, szFile + QStringLiteral( ".bak" ) );
	const std::string szText = xv::map::WriteHammerSetup( szExisting, setup );
	QSaveFile out( szFile );
	if ( !out.open( QIODevice::WriteOnly ) || out.write( szText.data(), static_cast<qint64>( szText.size() ) ) != static_cast<qint64>( szText.size() ) || !out.commit() )
	{
		*pError = tr( "Couldn't write %1: %2" ).arg( szFile, out.errorString() );
		return false;
	}
	return true;
}
