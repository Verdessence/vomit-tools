//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See HammerSetupPage.h.
//
//===========================================================================//
#include "HammerSetupPage.h"

#include <fstream>
#include <iterator>

#include <QComboBox>
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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "ChoiceBoxes.h"
#include "FileDialogs.h"
#include "Preferences.h"
#include "map/Fgd.h"
#include "map/Scene.h"
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
	return QString::fromStdU16String( path.generic_u16string() );
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

//-----------------------------------------------------------------------------
// Purpose: Hammer++ reads its own FGD after the entry's, so the entry doesn't name it.
//-----------------------------------------------------------------------------
bool IsHammerPlusPlusFgd( const std::filesystem::path &fgd )
{
	return QString::fromStdU16String( fgd.filename().u16string() ).compare( QStringLiteral( "hammerplusplus_fgd.fgd" ), Qt::CaseInsensitive ) == 0;
}

//-----------------------------------------------------------------------------
// Purpose: The same files, by their paths as written out, whatever the case.
//-----------------------------------------------------------------------------
bool SameFiles( const std::vector<std::filesystem::path> &a, const std::vector<std::filesystem::path> &b )
{
	return std::equal( a.begin(), a.end(), b.begin(), b.end(), []( const std::filesystem::path &x, const std::filesystem::path &y ) {
		return ToQString( x.lexically_normal() ).compare( ToQString( y.lexically_normal() ), Qt::CaseInsensitive ) == 0;
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The first line of a class's description, for beside its name.
//-----------------------------------------------------------------------------
QString ShortDescription( const std::string &szDescription )
{
	QString sz = QString::fromStdString( szDescription ).simplified();
	if ( const qsizetype nStop = sz.indexOf( QStringLiteral( ". " ) ); nStop > 0 )
		sz.truncate( nStop + 1 );
	return sz;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CHammerSetupPage::CHammerSetupPage( QWidget *pParent )
	: QWidget( pParent )
{
	setObjectName( QStringLiteral( "hammerSetup" ) );
	auto *pLayout = new QVBoxLayout( this );
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "hammerSetupStatus" ) );
	m_pStatus->setWordWrap( true );
	m_pStatus->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pLayout->addWidget( m_pStatus );
	m_pFields = new QWidget;
	pLayout->addWidget( m_pFields );
	auto *pForm = new QFormLayout( m_pFields );
	pForm->setContentsMargins( 0, 0, 0, 0 );

	m_pKind = new QComboBox;
	m_pKind->setObjectName( QStringLiteral( "hammerSetupKind" ) );
	m_pKind->addItem( tr( "Hammer++" ), static_cast<int>( xv::map::HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS ) );
	m_pKind->addItem( tr( "Hammer" ), static_cast<int>( xv::map::HammerKind_t::HAMMER_KIND_HAMMER ) );
	pForm->addRow( tr( "Editor" ), m_pKind );

	m_pName = new QLineEdit;
	m_pName->setObjectName( QStringLiteral( "hammerSetupName" ) );
	pForm->addRow( tr( "Configuration name" ), m_pName );
	m_pFgds = new QPlainTextEdit;
	m_pFgds->setObjectName( QStringLiteral( "hammerSetupFgds" ) );
	m_pFgds->setToolTip( tr( "The game data files, one a line, in the order Hammer and the map viewer read them" ) );
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

	m_pPointEntity = MakeSearchBox( {} );
	m_pPointEntity->setObjectName( QStringLiteral( "hammerSetupPointEntity" ) );
	m_pPointEntity->setToolTip( tr( "The point entities in the game data files above" ) );
	pForm->addRow( tr( "Default point entity" ), m_pPointEntity );
	m_pSolidEntity = MakeSearchBox( {} );
	m_pSolidEntity->setObjectName( QStringLiteral( "hammerSetupSolidEntity" ) );
	m_pSolidEntity->setToolTip( tr( "The brush entities in the game data files above" ) );
	pForm->addRow( tr( "Default brush entity" ), m_pSolidEntity );
	m_pFgdsEdited = new QTimer( this );
	m_pFgdsEdited->setSingleShot( true );
	m_pFgdsEdited->setInterval( 400 );
	connect( m_pFgdsEdited, &QTimer::timeout, this, &CHammerSetupPage::FillEntityChoices );
	connect( m_pFgds, &QPlainTextEdit::textChanged, m_pFgdsEdited, qOverload<>( &QTimer::start ) );
	m_pTextureScale = new QDoubleSpinBox;
	m_pTextureScale->setRange( 0.01, 16.0 );
	m_pTextureScale->setDecimals( 3 );
	m_pTextureScale->setSingleStep( 0.05 );
	pForm->addRow( tr( "Default texture scale" ), m_pTextureScale );
	m_pLightmapScale = new QSpinBox;
	m_pLightmapScale->setRange( 1, 512 );
	pForm->addRow( tr( "Default lightmap scale" ), m_pLightmapScale );
	m_pCordonTexture = MakeSearchBox( {} );
	m_pCordonTexture->setObjectName( QStringLiteral( "hammerSetupCordonTexture" ) );
	pForm->addRow( tr( "Cordon texture" ), m_pCordonTexture );

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

	auto *pButtons = new QHBoxLayout;
	auto *pSuggest = new QPushButton( tr( "Fill from Game Folders" ) );
	pSuggest->setObjectName( QStringLiteral( "hammerSetupSuggest" ) );
	pSuggest->setToolTip( tr( "What the game's folders hold, instead of the configuration's entry" ) );
	auto *pSave = new QPushButton( tr( "Save to Hammer's Configuration" ) );
	pSave->setObjectName( QStringLiteral( "hammerSetupSave" ) );
	m_pSaved = new QLabel;
	m_pSaved->setObjectName( QStringLiteral( "hammerSetupSaved" ) );
	pButtons->addWidget( pSuggest );
	pButtons->addStretch();
	pButtons->addWidget( m_pSaved );
	pButtons->addWidget( pSave );
	pForm->addRow( pButtons );
	pLayout->addStretch();
	connect( pSuggest, &QPushButton::clicked, this, [ this ] { FillFields( xv::map::SuggestHammerSetup( m_GameDir, m_szGameName.toStdString() ) ); } );
	connect( pSave, &QPushButton::clicked, this, [ this ] {
		QString szError;
		if ( Save( &szError ) )
		{
			m_pSaved->setText( tr( "Saved; maps opened from now on read it." ) );
			Fill();
		}
		else
		{
			QMessageBox::warning( this, tr( "Hammer" ), szError );
		}
	} );
	connect( m_pKind, &QComboBox::currentIndexChanged, this, &CHammerSetupPage::Fill );
	SetGame( {}, {}, {} );
}

//-----------------------------------------------------------------------------
// Purpose: Hammer++ when it is installed, else Hammer when it is.
//-----------------------------------------------------------------------------
void CHammerSetupPage::SetGame( const std::filesystem::path &gameDir, const QString &szGameName, const std::function<std::vector<std::string>()> &pfnMaterials )
{
	if ( !gameDir.empty() && gameDir == m_GameDir )
		return;
	m_GameDir = gameDir;
	m_szGameName = szGameName;
	m_pSaved->clear();
	m_pFields->setEnabled( !gameDir.empty() );
	// Written as Hammer writes material names, with backslashes.
	std::vector<Choice_t> textures;
	for ( const std::string &szPath : pfnMaterials ? pfnMaterials() : std::vector<std::string>{} )
	{
		QString szName = QString::fromStdString( szPath );
		if ( szName.startsWith( QStringLiteral( "materials/" ), Qt::CaseInsensitive ) )
			szName.remove( 0, 10 );
		if ( szName.endsWith( QStringLiteral( ".vmt" ), Qt::CaseInsensitive ) )
			szName.chop( 4 );
		textures.push_back( { szName.replace( QLatin1Char( '/' ), QLatin1Char( '\\' ) ), {} } );
	}
	SetSearchChoices( m_pCordonTexture, textures );
	if ( gameDir.empty() )
	{
		m_pStatus->setText( tr( "Open a game first: the entry is written into that game's Hammer configuration." ) );
		return;
	}
	{
		const QSignalBlocker blocker( m_pKind );
		const bool bPlain = !xv::map::HammerProgram( m_GameDir, xv::map::HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS ) && xv::map::HammerProgram( m_GameDir, xv::map::HammerKind_t::HAMMER_KIND_HAMMER );
		m_pKind->setCurrentIndex( bPlain ? 1 : 0 );
	}
	Fill();
}

//-----------------------------------------------------------------------------
// Purpose: Puts an entry in the fields.
//-----------------------------------------------------------------------------
void CHammerSetupPage::FillFields( const xv::map::HammerSetup_t &setup )
{
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
	m_pPointEntity->setCurrentText( QString::fromStdString( setup.m_Defaults.m_szPointEntity ) );
	m_pSolidEntity->setCurrentText( QString::fromStdString( setup.m_Defaults.m_szSolidEntity ) );
	m_pTextureScale->setValue( setup.m_Defaults.m_flTextureScale );
	m_pLightmapScale->setValue( setup.m_Defaults.m_nLightmapScale );
	m_pCordonTexture->setCurrentText( QString::fromStdString( setup.m_szCordonTexture ) );
	m_pFgdsEdited->stop();
	FillEntityChoices();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::HammerKind_t CHammerSetupPage::GetKind() const
{
	return static_cast<xv::map::HammerKind_t>( m_pKind->currentData().toInt() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::filesystem::path CHammerSetupPage::GetConfigFile() const
{
	return xv::map::HammerConfigFile( m_GameDir, GetKind() );
}

//-----------------------------------------------------------------------------
// Purpose: The entry the configuration has for the game, else the folders' suggestion.
//-----------------------------------------------------------------------------
void CHammerSetupPage::Fill()
{
	if ( m_GameDir.empty() )
		return;
	const std::filesystem::path file = GetConfigFile();
	const std::optional<std::filesystem::path> program = xv::map::HammerProgram( m_GameDir, GetKind() );
	const std::optional<xv::map::HammerSetup_t> existing = xv::map::ReadHammerSetup( ReadText( file ), m_GameDir );
	QStringList status;
	status << ( program ? tr( "Installed: %1" ).arg( ToQString( *program ) ) : tr( "Not installed beside this game: the entry is written for when it is." ) );
	status << ( existing ? tr( "Editing this game's entry in %1" ).arg( ToQString( file ) ) : tr( "This game has no entry yet; saving adds one to %1" ).arg( ToQString( file ) ) );
	// What Preferences > Maps names in place of the entry is what the map viewer reads, so the
	// fields start from it; saving puts it in the entry.
	xv::map::HammerSetup_t setup = existing.value_or( xv::map::SuggestHammerSetup( m_GameDir, m_szGameName.toStdString() ) );
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	QStringList overridden;
	if ( !preferences.m_Fgds.isEmpty() )
	{
		std::vector<std::filesystem::path> fgds;
		for ( const QString &szFgd : preferences.m_Fgds )
			if ( !szFgd.trimmed().isEmpty() && !( GetKind() == xv::map::HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS && IsHammerPlusPlusFgd( ToPath( szFgd ) ) ) )
				fgds.push_back( ToPath( szFgd ) );
		if ( !fgds.empty() && !SameFiles( fgds, setup.m_Fgds ) )
		{
			setup.m_Fgds = std::move( fgds );
			overridden << tr( "game data" );
		}
	}
	if ( const std::string szPoint = preferences.m_szDefaultPointEntity.trimmed().toStdString(); !szPoint.empty() && szPoint != setup.m_Defaults.m_szPointEntity )
	{
		setup.m_Defaults.m_szPointEntity = szPoint;
		overridden << tr( "default point entity" );
	}
	if ( const std::string szBrush = preferences.m_szDefaultBrushEntity.trimmed().toStdString(); !szBrush.empty() && szBrush != setup.m_Defaults.m_szSolidEntity )
	{
		setup.m_Defaults.m_szSolidEntity = szBrush;
		overridden << tr( "default brush entity" );
	}
	if ( !overridden.isEmpty() )
		status << tr( "The %1 shown were set apart from this entry (what the map viewer reads now); saving writes them into it." ).arg( overridden.join( QStringLiteral( ", " ) ) );
	m_pStatus->setText( status.join( QLatin1Char( '\n' ) ) );
	FillFields( setup );
}

//-----------------------------------------------------------------------------
// Purpose: The entity boxes offer the classes the listed game data defines (Hammer++'s own
//          FGD too, which it always reads), each next to its description.
//-----------------------------------------------------------------------------
void CHammerSetupPage::FillEntityChoices()
{
	std::vector<std::filesystem::path> files;
	for ( const QString &szLine : m_pFgds->toPlainText().split( QLatin1Char( '\n' ), Qt::SkipEmptyParts ) )
	{
		std::error_code error;
		const std::filesystem::path found = xv::map::FindFileIgnoringCase( ToPath( szLine ) );
		if ( !szLine.trimmed().isEmpty() && std::filesystem::is_regular_file( found, error ) )
			files.push_back( found );
	}
	if ( GetKind() == xv::map::HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS )
		if ( const std::optional<std::filesystem::path> own = xv::map::HammerPlusPlusContent( m_GameDir ) )
			files.push_back( xv::map::FindFileIgnoringCase( *own / "hammerplusplus_fgd.fgd" ) );
	std::vector<Choice_t> points;
	std::vector<Choice_t> brushes;
	if ( !files.empty() )
	{
		const std::shared_ptr<const xv::map::CFgd> pFgd = xv::map::LoadFgds( files );
		for ( const auto &[ szKey, fgdClass ] : pFgd->GetClasses() )
		{
			const Choice_t choice{ QString::fromStdString( fgdClass.m_szName ), ShortDescription( fgdClass.m_szDescription ) };
			if ( fgdClass.m_nKind == xv::map::FgdClassKind_t::FGD_CLASS_KIND_SOLID )
			{
				if ( szKey != "worldspawn" )
					brushes.push_back( choice );
			}
			else if ( fgdClass.IsPointClass() && fgdClass.m_nKind != xv::map::FgdClassKind_t::FGD_CLASS_KIND_EXTEND )
			{
				points.push_back( choice );
			}
		}
	}
	SetSearchChoices( m_pPointEntity, points );
	SetSearchChoices( m_pSolidEntity, brushes );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::HammerSetup_t CHammerSetupPage::GetSetup() const
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
	setup.m_Defaults.m_szPointEntity = m_pPointEntity->currentText().trimmed().toStdString();
	setup.m_Defaults.m_szSolidEntity = m_pSolidEntity->currentText().trimmed().toStdString();
	setup.m_Defaults.m_flTextureScale = m_pTextureScale->value();
	setup.m_Defaults.m_nLightmapScale = m_pLightmapScale->value();
	setup.m_szCordonTexture = m_pCordonTexture->currentText().trimmed().toStdString();
	return setup;
}

//-----------------------------------------------------------------------------
// Purpose: The first save keeps Hammer's own file beside it, in case the entry was hand-made.
//-----------------------------------------------------------------------------
bool CHammerSetupPage::Save( QString *pError )
{
	if ( m_GameDir.empty() )
	{
		*pError = tr( "No game is open." );
		return false;
	}
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
	UpdatePreferences( setup );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The map viewer reads game data and default entities that older settings name apart
//          from the entry before the entry's own, so a saved entry would change nothing while
//          they are set. Where the viewer reads this entry, they are cleared; where it reads another
//          configuration (Hammer saved while Hammer++'s also names the game), they take the
//          saved values.
//-----------------------------------------------------------------------------
void CHammerSetupPage::UpdatePreferences( const xv::map::HammerSetup_t &setup ) const
{
	const xv::map::GameFgds_t read = xv::map::FindGameFgds( m_GameDir );
	std::vector<std::filesystem::path> readFgds;
	for ( const std::filesystem::path &fgd : read.m_Files )
		if ( !IsHammerPlusPlusFgd( fgd ) )
			readFgds.push_back( fgd );
	MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
	QStringList fgds;
	if ( !SameFiles( readFgds, setup.m_Fgds ) )
	{
		for ( const std::filesystem::path &fgd : setup.m_Fgds )
			fgds << ToQString( fgd );
		if ( GetKind() == xv::map::HammerKind_t::HAMMER_KIND_HAMMER_PLUS_PLUS )
			if ( const std::optional<std::filesystem::path> own = xv::map::HammerPlusPlusContent( m_GameDir ) )
				fgds << ToQString( xv::map::FindFileIgnoringCase( *own / "hammerplusplus_fgd.fgd" ) );
	}
	const QString szPoint = read.m_Defaults.m_szPointEntity == setup.m_Defaults.m_szPointEntity ? QString{} : QString::fromStdString( setup.m_Defaults.m_szPointEntity );
	const QString szBrush = read.m_Defaults.m_szSolidEntity == setup.m_Defaults.m_szSolidEntity ? QString{} : QString::fromStdString( setup.m_Defaults.m_szSolidEntity );
	if ( fgds == preferences.m_Fgds && szPoint == preferences.m_szDefaultPointEntity && szBrush == preferences.m_szDefaultBrushEntity )
		return;
	preferences.m_Fgds = fgds;
	preferences.m_szDefaultPointEntity = szPoint;
	preferences.m_szDefaultBrushEntity = szBrush;
	CPreferences::Instance().SetMapPreferences( preferences );
}
