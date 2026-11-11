//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapScreenEffectPanel.h.
//
//===========================================================================//
#include "MapScreenEffectPanel.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "map/MapEntityFields.h"
#include "map/MapScreenEffect.h"
#include "map/MaterialBrowser.h"
#include "shader/Bytecode.h"

namespace
{

constexpr int ROLE_MATERIAL = Qt::UserRole + 1;

//-----------------------------------------------------------------------------
// Purpose: A number as short as it can be written exactly enough.
//-----------------------------------------------------------------------------
QString Number( double fl )
{
	return QString::number( fl, 'g', 6 );
}

//-----------------------------------------------------------------------------
// Purpose: Verd's screenspace_font_line's constants unpacked, as its TECHNICAL_REFERENCE
//          (section 4) packs them: c0 time, the box's origin and scale; c1 the rows and fade,
//          the reveal cursor, the tint (each channel's complement, a byte each) and the first two
//          parameter widths; c2 the next two, then 21 runtime glyphs, three 8-bit cells a float.
//-----------------------------------------------------------------------------
QStringList FontLineConstants( const std::array<xv::shader::Vec4_t, 4> &c )
{
	const auto floorOf = []( float fl ) { return static_cast<long long>( std::floor( fl ) ); };
	QStringList lines;
	lines << QObject::tr( "Box at %1, %2 (canvas px), scale %3" ).arg( Number( c[ 0 ][ 1 ] ), Number( c[ 0 ][ 2 ] ), Number( c[ 0 ][ 3 ] ) );
	const long long nRowsPacked = floorOf( c[ 1 ][ 0 ] );
	const long long nAlpha = nRowsPacked / 16384;
	const long long nRows = nRowsPacked - nAlpha * 16384;
	lines << QObject::tr( "Rows from %1, %2 of them; fade alpha %3/63" ).arg( nRows / 8 ).arg( nRows % 8 + 1 ).arg( nAlpha );
	lines << ( c[ 1 ][ 1 ] < 0.0f ? QObject::tr( "Reveal: hidden" ) : QObject::tr( "Reveal: up to column %1" ).arg( Number( c[ 1 ][ 1 ] ) ) );
	const long long nTint = floorOf( c[ 1 ][ 2 ] );
	lines << QObject::tr( "Tint %1 %2 %3" ).arg( 255 - nTint % 256 ).arg( 255 - ( nTint / 256 ) % 256 ).arg( 255 - ( nTint / 65536 ) % 256 );
	const long long nE01 = floorOf( c[ 1 ][ 3 ] );
	const long long nE23 = floorOf( c[ 2 ][ 0 ] );
	lines << QObject::tr( "Parameter widths E0..E3: %1, %2, %3, %4" ).arg( nE01 % 4096 ).arg( nE01 / 4096 ).arg( nE23 % 4096 ).arg( nE23 / 4096 );
	if ( nE01 == 0 && nE23 == 0 )
	{
		lines << QObject::tr( "Runtime glyphs: unused (no parameters)" );
		return lines;
	}
	QStringList cells;
	const std::array<float, 7> glyphs{ c[ 2 ][ 1 ], c[ 2 ][ 2 ], c[ 2 ][ 3 ], c[ 3 ][ 0 ], c[ 3 ][ 1 ], c[ 3 ][ 2 ], c[ 3 ][ 3 ] };
	for ( const float fl : glyphs )
	{
		const long long n = floorOf( fl );
		for ( const long long nCell : { n % 256, ( n / 256 ) % 256, ( n / 65536 ) % 256 } )
			if ( nCell != 255 )
				cells << QString::number( nCell );
	}
	lines << ( cells.isEmpty() ? QObject::tr( "Runtime glyphs: none" ) : QObject::tr( "Runtime glyph cells: %1" ).arg( cells.join( u' ' ) ) );
	return lines;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapScreenEffectPanel::CMapScreenEffectPanel( CMapEditor &editor, QWidget *pParent )
	: QWidget( pParent ),
	  m_Editor( editor )
{
	auto *pLayout = new QVBoxLayout( this );
	auto *pRow = new QHBoxLayout;
	m_pMaterial = new QComboBox;
	m_pMaterial->setObjectName( QStringLiteral( "mapScreenEffectMaterial" ) );
	m_pMaterial->setEditable( true );
	m_pMaterial->setInsertPolicy( QComboBox::NoInsert );
	m_pMaterial->setToolTip( tr( "A screenspace_general material: one the map names for the screen, or any typed or picked below" ) );
	m_pMaterial->lineEdit()->setPlaceholderText( tr( "effects/..." ) );
	m_pBrowse = new QToolButton;
	m_pBrowse->setObjectName( QStringLiteral( "mapScreenEffectBrowse" ) );
	m_pBrowse->setText( tr( "Any Material..." ) );
	m_pBrowse->setToolTip( tr( "Pick any material from the game's list" ) );
	pRow->addWidget( m_pMaterial, 1 );
	pRow->addWidget( m_pBrowse );
	pLayout->addLayout( pRow );
	// Under the material: the shader it runs, or why it can't.
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "mapScreenEffectStatus" ) );
	m_pStatus->setWordWrap( true );
	m_pStatus->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pLayout->addWidget( m_pStatus );
	m_pShown = new QCheckBox( tr( "Enabled" ) );
	m_pShown->setObjectName( QStringLiteral( "mapScreenEffectShown" ) );
	m_pShown->setToolTip( tr( "Draw the material over the 3D views" ) );
	pLayout->addWidget( m_pShown );

	// Picking by shader: the game's screenspace_general materials, or its .vcs files.
	auto *pPick = new QWidget;
	auto *pPickLayout = new QVBoxLayout( pPick );
	pPickLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pFilter = new QLineEdit;
	m_pFilter->setObjectName( QStringLiteral( "mapScreenEffectFilter" ) );
	m_pFilter->setPlaceholderText( tr( "Filter by material or shader..." ) );
	m_pFilter->setClearButtonEnabled( true );
	pPickLayout->addWidget( m_pFilter );
	m_pTabs = new QTabWidget;
	m_pTabs->setDocumentMode( true );
	m_pMaterials = new QTreeWidget;
	m_pMaterials->setObjectName( QStringLiteral( "mapScreenEffectMaterials" ) );
	m_pMaterials->setHeaderLabels( { tr( "Material" ), tr( "Pixel Shader" ) } );
	m_pMaterials->setRootIsDecorated( true );
	m_pMaterials->setUniformRowHeights( true );
	m_pMaterials->header()->setStretchLastSection( true );
	m_pMaterials->setColumnWidth( 0, 200 );
	m_pMaterials->setMinimumHeight( 160 );
	m_pShaders = new QTreeWidget;
	m_pShaders->setObjectName( QStringLiteral( "mapScreenEffectShaders" ) );
	m_pShaders->setHeaderLabels( { tr( "Shader" ), tr( "Materials" ) } );
	m_pShaders->setUniformRowHeights( true );
	m_pShaders->setColumnWidth( 0, 220 );
	m_pShaders->setMinimumHeight( 160 );
	m_pTabs->addTab( m_pMaterials, tr( "Materials" ) );
	m_pTabs->addTab( m_pShaders, tr( "Shaders" ) );
	pPickLayout->addWidget( m_pTabs );
	pLayout->addWidget( MakeFoldSection( tr( "Pick by Shader" ), pPick, QStringLiteral( "mapScreenEffectPick" ), true ) );

	// c0 to c3, as $c0_x... set them (the material's proxies' values while not changed here).
	auto *pConstants = new QWidget;
	auto *pConstantsLayout = new QVBoxLayout( pConstants );
	pConstantsLayout->setContentsMargins( 0, 0, 0, 0 );
	auto *pGrid = new QGridLayout;
	for ( int c = 0; c < 4; c++ )
	{
		pGrid->addWidget( new QLabel( QStringLiteral( "c%1" ).arg( c ) ), c, 0 );
		for ( int i = 0; i < 4; i++ )
		{
			auto *pSpin = new QDoubleSpinBox;
			pSpin->setObjectName( QStringLiteral( "mapScreenEffectC%1%2" ).arg( c ).arg( QLatin1Char( "xyzw"[ i ] ) ) );
			pSpin->setRange( -100000000.0, 100000000.0 );
			pSpin->setDecimals( 3 );
			pSpin->setSingleStep( 0.05 );
			pSpin->setButtonSymbols( QAbstractSpinBox::NoButtons );
			pSpin->setToolTip( QStringLiteral( "$c%1_%2" ).arg( c ).arg( QLatin1Char( "xyzw"[ i ] ) ) );
			// Narrow: four to a row in the side (their range would ask for a wide box each).
			pSpin->setMinimumWidth( 32 );
			pSpin->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
			pGrid->addWidget( pSpin, c, 1 + i );
			m_Constants[ static_cast<std::size_t>( c ) ][ static_cast<std::size_t>( i ) ] = pSpin;
			connect( pSpin, &QDoubleSpinBox::valueChanged, this, &CMapScreenEffectPanel::ConstantsEdited );
		}
	}
	pConstantsLayout->addLayout( pGrid );
	m_pReset = new QPushButton( tr( "Material's Values" ) );
	m_pReset->setObjectName( QStringLiteral( "mapScreenEffectReset" ) );
	m_pReset->setToolTip( tr( "c0 to c3 back to what the material and its proxies give" ) );
	pConstantsLayout->addWidget( m_pReset );
	pLayout->addWidget( MakeFoldSection( tr( "Constants" ), pConstants, QStringLiteral( "mapScreenEffectConstants" ), true ) );

	// What the shader sees and costs, kept up to date while open.
	m_pStatistics = new QLabel;
	m_pStatistics->setObjectName( QStringLiteral( "mapScreenEffectStatistics" ) );
	m_pStatistics->setWordWrap( true );
	m_pStatistics->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pStatistics->setTextFormat( Qt::RichText );
	pLayout->addWidget( MakeFoldSection( tr( "Statistics" ), m_pStatistics, QStringLiteral( "mapScreenEffectStatisticsSection" ), true ) );
	pLayout->addStretch();
	m_pStatisticsTimer = new QTimer( this );
	m_pStatisticsTimer->setInterval( 250 );
	connect( m_pStatisticsTimer, &QTimer::timeout, this, [ this ] {
		// Only while someone can see them, and only an effect its proxies move changes.
		if ( m_pStatistics->isVisible() && m_Editor.GetScreenEffect() && m_Editor.GetScreenEffect()->IsAnimated() )
			UpdateStatistics();
	} );
	m_pStatisticsTimer->start();

	connect( m_pMaterial, &QComboBox::textActivated, this, [ this ]( const QString &szMaterial ) { m_Editor.SetScreenEffect( szMaterial ); } );
	connect( m_pMaterial->lineEdit(), &QLineEdit::editingFinished, this, [ this ] {
		if ( m_pMaterial->currentText() != ( m_Editor.GetScreenEffect() ? m_Editor.GetScreenEffect()->GetMaterial() : QString{} ) )
			m_Editor.SetScreenEffect( m_pMaterial->currentText() );
	} );
	connect( m_pBrowse, &QToolButton::clicked, this, [ this ] {
		const auto &game = m_Editor.GetDocument().GetGame();
		if ( !game )
			return;
		if ( const std::optional<QString> picked = CMaterialBrowser::PickFromList( this, *game, m_Named, m_pMaterial->currentText() ) )
			m_Editor.SetScreenEffect( *picked );
	} );
	connect( m_pShown, &QCheckBox::toggled, this, [ this ]( bool bOn ) {
		if ( !m_bFilling )
			m_Editor.ShowScreenEffect( bOn );
	} );
	connect( m_pFilter, &QLineEdit::textChanged, this, &CMapScreenEffectPanel::FillLists );
	connect( m_pMaterials, &QTreeWidget::itemActivated, this, &CMapScreenEffectPanel::ItemPicked );
	connect( m_pShaders, &QTreeWidget::itemActivated, this, &CMapScreenEffectPanel::ItemPicked );
	connect( m_pReset, &QPushButton::clicked, this, [ this ] { m_Editor.SetScreenEffectOverrides( {} ); } );
	connect( &m_Editor, &CMapEditor::ScreenEffectChanged, this, &CMapScreenEffectPanel::EffectChanged );
	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, &CMapScreenEffectPanel::Refresh );
	connect( &m_Editor.GetDocument(), &CMapDocument::Loaded, this, &CMapScreenEffectPanel::RequestCatalog );
	Refresh();
	EffectChanged();
	RequestCatalog();
}

//-----------------------------------------------------------------------------
// Purpose: The game's catalog asked for once its game is known (read on a thread).
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::RequestCatalog()
{
	const auto &game = m_Editor.GetDocument().GetGame();
	if ( m_bCatalogAsked || !game )
	{
		FillLists();
		return;
	}
	m_bCatalogAsked = true;
	FillLists();
	RequestScreenCatalog( *game, this, [ this ]( std::shared_ptr<const ScreenCatalog_t> pCatalog ) { SetCatalog( std::move( pCatalog ) ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::SetCatalog( std::shared_ptr<const ScreenCatalog_t> pCatalog )
{
	m_pCatalog = std::move( pCatalog );
	FillLists();
}

//-----------------------------------------------------------------------------
// Purpose: The map's own first, then the game's, each with the shader it names; those whose
//          .vcs the game hasn't greyed and last. The Shaders tab: every .vcs with the materials
//          using it under it.
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::FillLists()
{
	const QString szFilter = m_pFilter->text().trimmed();
	const auto passes = [ & ]( const QString &szMaterial, const QString &szShader ) {
		if ( szFilter.isEmpty() )
			return true;
		for ( const QString &szWord : szFilter.split( u' ', Qt::SkipEmptyParts ) )
			if ( !szMaterial.contains( szWord, Qt::CaseInsensitive ) && !szShader.contains( szWord, Qt::CaseInsensitive ) )
				return false;
		return true;
	};
	const QColor missing = palette().color( QPalette::Disabled, QPalette::Text );
	const auto entryOf = [ this ]( const QString &szMaterial ) -> const ScreenMaterial_t * {
		if ( !m_pCatalog )
			return nullptr;
		const auto it = std::ranges::find_if( m_pCatalog->m_Materials, [ & ]( const ScreenMaterial_t &entry ) { return entry.m_szMaterial.compare( szMaterial, Qt::CaseInsensitive ) == 0; } );
		return it != m_pCatalog->m_Materials.end() ? &*it : nullptr;
	};
	const auto addMaterial = [ & ]( QTreeWidgetItem *pParent, const QString &szMaterial, const ScreenMaterial_t *pEntry ) {
		auto *pItem = new QTreeWidgetItem( pParent, { szMaterial, pEntry ? pEntry->m_szPixelShader : QString{} } );
		pItem->setData( 0, ROLE_MATERIAL, szMaterial );
		pItem->setToolTip( 0, szMaterial );
		if ( pEntry )
		{
			pItem->setToolTip( 1, pEntry->m_szShaderPath.isEmpty() ? tr( "%1: not in the game's files" ).arg( pEntry->m_szPixelShader ) : pEntry->m_szShaderPath );
			if ( pEntry->m_szShaderPath.isEmpty() )
			{
				pItem->setForeground( 0, missing );
				pItem->setForeground( 1, missing );
			}
		}
		return pItem;
	};

	m_pMaterials->clear();
	auto *pMap = new QTreeWidgetItem( m_pMaterials, { tr( "This map's" ) } );
	pMap->setFlags( Qt::ItemIsEnabled );
	for ( const QString &szName : m_Named )
	{
		const ScreenMaterial_t *pEntry = entryOf( szName );
		if ( passes( szName, pEntry ? pEntry->m_szPixelShader : QString{} ) )
			addMaterial( pMap, szName, pEntry );
	}
	pMap->setText( 0, tr( "This map's (%1)" ).arg( pMap->childCount() ) );
	auto *pGame = new QTreeWidgetItem( m_pMaterials, { m_pCatalog ? QString{} : tr( "The game's: reading its materials..." ) } );
	pGame->setFlags( Qt::ItemIsEnabled );
	m_pShaders->clear();
	if ( m_pCatalog )
	{
		std::vector<const ScreenMaterial_t *> found, absent;
		for ( const ScreenMaterial_t &entry : m_pCatalog->m_Materials )
			if ( passes( entry.m_szMaterial, entry.m_szPixelShader ) )
				( entry.m_szShaderPath.isEmpty() ? absent : found ).push_back( &entry );
		for ( const auto *pList : { &found, &absent } )
			for ( const ScreenMaterial_t *pEntry : *pList )
				addMaterial( pGame, pEntry->m_szMaterial, pEntry );
		pGame->setText( 0, tr( "The game's (%1)" ).arg( pGame->childCount() ) );

		for ( const QString &szShader : m_pCatalog->m_Shaders )
		{
			std::vector<const ScreenMaterial_t *> users;
			for ( const ScreenMaterial_t &entry : m_pCatalog->m_Materials )
				if ( entry.m_szShaderPath.compare( szShader, Qt::CaseInsensitive ) == 0 )
					users.push_back( &entry );
			const QString szName = szShader.mid( szShader.lastIndexOf( u'/' ) + 1 );
			const bool bShaderPasses = passes( {}, szName );
			if ( !bShaderPasses && std::ranges::none_of( users, [ & ]( const ScreenMaterial_t *pEntry ) { return passes( pEntry->m_szMaterial, {} ); } ) )
				continue;
			auto *pShader = new QTreeWidgetItem( m_pShaders, { szName, users.empty() ? tr( "no material uses it" ) : QString::number( users.size() ) } );
			pShader->setToolTip( 0, szShader );
			pShader->setFlags( Qt::ItemIsEnabled );
			if ( users.empty() )
				pShader->setForeground( 1, missing );
			for ( const ScreenMaterial_t *pEntry : users )
				if ( bShaderPasses || passes( pEntry->m_szMaterial, {} ) )
					addMaterial( pShader, pEntry->m_szMaterial, pEntry );
			if ( !szFilter.isEmpty() )
				pShader->setExpanded( true );
		}
	}
	pMap->setExpanded( true );
	pGame->setExpanded( true );
}

//-----------------------------------------------------------------------------
// Purpose: A material row picked (double-click, Enter): its effect shown.
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::ItemPicked( QTreeWidgetItem *pItem )
{
	const QString szMaterial = pItem ? pItem->data( 0, ROLE_MATERIAL ).toString() : QString{};
	if ( szMaterial.isEmpty() )
		return;
	{
		const QSignalBlocker blocker( m_pMaterial );
		m_pMaterial->setCurrentText( szMaterial );
	}
	m_Editor.SetScreenEffect( szMaterial );
	if ( m_Editor.GetScreenEffect() )
		m_Editor.ShowScreenEffect( true );
}

//-----------------------------------------------------------------------------
// Purpose: env_screenoverlay's OverlayName1 to OverlayName10, material_modify_control's
//          materialName, and the material after any r_screenoverlay in a key or output
//          (point_clientcommand's, a logic_relay's).
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::Refresh()
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	QStringList named;
	const auto add = [ &named ]( QString szName ) {
		szName = szName.trimmed();
		szName.replace( u'\\', u'/' );
		if ( szName.startsWith( QStringLiteral( "materials/" ), Qt::CaseInsensitive ) )
			szName.remove( 0, 10 );
		if ( szName.endsWith( QStringLiteral( ".vmt" ), Qt::CaseInsensitive ) )
			szName.chop( 4 );
		if ( !szName.isEmpty() && szName != QStringLiteral( "off" ) && !named.contains( szName, Qt::CaseInsensitive ) )
			named << szName;
	};
	static const QRegularExpression COMMAND( QStringLiteral( "r_screenoverlay\\s+\"?([^\\s\";:,]+)" ), QRegularExpression::CaseInsensitiveOption );
	for ( std::size_t e = 0; pScene && e < pScene->m_Map.m_Entities.size(); e++ )
	{
		const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ e ];
		if ( entity.m_bDeleted )
			continue;
		const bool bOverlay = entity.GetClassname() == "env_screenoverlay";
		const bool bModify = entity.GetClassname() == "material_modify_control";
		for ( const xv::map::EntityKey_t &key : entity.m_Keys )
		{
			if ( key.m_bDeleted )
				continue;
			const QString szText = QString::fromUtf8( key.m_szValue.data(), static_cast<qsizetype>( key.m_szValue.size() ) );
			const QString szKey = QString::fromUtf8( key.m_szKey.data(), static_cast<qsizetype>( key.m_szKey.size() ) );
			if ( ( bOverlay && szKey.startsWith( QStringLiteral( "OverlayName" ), Qt::CaseInsensitive ) ) || ( bModify && szKey.compare( QStringLiteral( "materialName" ), Qt::CaseInsensitive ) == 0 ) )
				add( szText );
			for ( auto it = COMMAND.globalMatch( szText ); it.hasNext(); )
				add( it.next().captured( 1 ) );
		}
		// Outputs that send the command (a point_clientcommand's Command input).
		for ( const xv::map::Connection_t &connection : entity.m_Connections )
		{
			const QString szParameter = QString::fromUtf8( connection.m_szParameter.data(), static_cast<qsizetype>( connection.m_szParameter.size() ) );
			for ( auto it = COMMAND.globalMatch( szParameter ); it.hasNext(); )
				add( it.next().captured( 1 ) );
		}
	}
	if ( named == m_Named )
		return;
	m_Named = named;
	const QString szCurrent = m_pMaterial->currentText();
	{
		const QSignalBlocker blocker( m_pMaterial );
		m_pMaterial->clear();
		m_pMaterial->addItems( named );
		m_pMaterial->setCurrentText( szCurrent );
	}
	FillLists();
}

//-----------------------------------------------------------------------------
// Purpose: The material, whether it shows, the shader it runs and its constants as they are now.
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::EffectChanged()
{
	m_bFilling = true;
	const auto &pEffect = m_Editor.GetScreenEffect();
	{
		const QSignalBlocker blocker( m_pMaterial );
		if ( pEffect )
			m_pMaterial->setCurrentText( pEffect->GetMaterial() );
	}
	m_pShown->setChecked( m_Editor.IsScreenEffectShown() );
	m_pShown->setEnabled( pEffect != nullptr );
	QString szStatus;
	if ( !m_Editor.GetScreenEffectProblem().isEmpty() )
		szStatus = m_Editor.GetScreenEffectProblem();
	else if ( !pEffect )
		szStatus = m_Named.isEmpty() ? tr( "Pick a screenspace_general material to see it over the 3D views." ) : tr( "The map names %n for the screen: pick one.", nullptr, static_cast<int>( m_Named.size() ) );
	else if ( !pEffect->GetProblem().isEmpty() )
		szStatus = pEffect->GetProblem();
	else
		szStatus = tr( "%1 (%2)" ).arg( pEffect->GetShaderPath(), pEffect->GetProfile() );
	m_pStatus->setText( szStatus );
	const xv::shader::ScreenEffect_t now = pEffect ? pEffect->At( m_Editor.GetScreenEffectTime() ) : xv::shader::ScreenEffect_t{};
	for ( std::size_t c = 0; c < 4; c++ )
	{
		for ( std::size_t i = 0; i < 4; i++ )
		{
			const QSignalBlocker blocker( m_Constants[ c ][ i ] );
			m_Constants[ c ][ i ]->setValue( now.m_Constants[ c ][ i ] );
			m_Constants[ c ][ i ]->setEnabled( pEffect != nullptr );
		}
	}
	m_pReset->setEnabled( pEffect && std::ranges::any_of( pEffect->GetOverrides(), []( const auto &override ) { return override.has_value(); } ) );
	m_bFilling = false;
	UpdateStatistics();
}

//-----------------------------------------------------------------------------
// Purpose: A constant typed: its register held at the values shown, the others left to the
//          material.
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::ConstantsEdited()
{
	const auto &pEffect = m_Editor.GetScreenEffect();
	if ( m_bFilling || !pEffect )
		return;
	std::array<std::optional<xv::shader::Vec4_t>, 4> overrides = pEffect->GetOverrides();
	for ( std::size_t c = 0; c < 4; c++ )
	{
		if ( std::ranges::find( m_Constants[ c ], qobject_cast<QDoubleSpinBox *>( sender() ) ) == m_Constants[ c ].end() )
			continue;
		overrides[ c ] = xv::shader::Vec4_t{ static_cast<float>( m_Constants[ c ][ 0 ]->value() ), static_cast<float>( m_Constants[ c ][ 1 ]->value() ), static_cast<float>( m_Constants[ c ][ 2 ]->value() ), static_cast<float>( m_Constants[ c ][ 3 ]->value() ) };
	}
	m_Editor.SetScreenEffectOverrides( overrides );
}

//-----------------------------------------------------------------------------
// Purpose: The shader run over a small picture once: its cost per 1920 by 1080 frame,
//          as the views pay it when they draw in software.
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::MeasureSoftware()
{
	const auto &pEffect = m_Editor.GetScreenEffect();
	m_szMeasured = pEffect ? pEffect->GetMaterial() + u'|' + pEffect->GetShaderPath() : QString{};
	m_flSoftwareMs = -1.0;
	if ( !pEffect || !pEffect->CanRun() )
		return;
	// Small and once: a heavy shader (Verd's font line) costs seconds a full frame in software.
	constexpr int WIDTH = 64, HEIGHT = 36, RUNS = 1;
	xv::texture::CImage frame( WIDTH, HEIGHT );
	for ( std::size_t i = 0; i < frame.m_Pixels.size(); i++ )
		frame.m_Pixels[ i ] = static_cast<std::uint8_t>( ( i * 37 ) & 0xFF );
	const auto start = std::chrono::steady_clock::now();
	for ( int n = 0; n < RUNS; n++ )
	{
		xv::texture::CImage copy = frame;
		pEffect->Apply( copy, m_Editor.GetScreenEffectTime() );
	}
	const double flMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() / RUNS;
	m_flSoftwareMs = flMs * ( 1920.0 * 1080.0 ) / ( WIDTH * HEIGHT );
}

//-----------------------------------------------------------------------------
// Purpose: c0 to c3 as the shader sees them now; its size; the constants and samplers it reads
//          (by the constant table's names, and what each sampler is bound to); its cost in
//          software; and for screenspace_font_line, its constants unpacked.
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::UpdateStatistics()
{
	const auto &pEffect = m_Editor.GetScreenEffect();
	if ( !pEffect || !pEffect->GetShader() )
	{
		m_pStatistics->setText( tr( "No shader running." ) );
		return;
	}
	if ( m_szMeasured != pEffect->GetMaterial() + u'|' + pEffect->GetShaderPath() )
		MeasureSoftware();
	const xv::shader::ScreenEffect_t now = pEffect->At( m_Editor.GetScreenEffectTime() );
	const xv::shader::Program_t &program = pEffect->GetShader()->GetProgram();
	const xv::shader::ProgramUse_t use = xv::shader::DescribeUse( program );
	QStringList lines;
	lines << tr( "<b>Now</b> (t = %1 s)" ).arg( m_Editor.GetScreenEffectTime(), 0, 'f', 2 );
	for ( std::size_t c = 0; c < 4; c++ )
	{
		const bool bHeld = pEffect->GetOverrides()[ c ].has_value();
		lines << QStringLiteral( "c%1 = %2, %3, %4, %5%6" ).arg( c ).arg( Number( now.m_Constants[ c ][ 0 ] ), Number( now.m_Constants[ c ][ 1 ] ), Number( now.m_Constants[ c ][ 2 ] ), Number( now.m_Constants[ c ][ 3 ] ), bHeld ? tr( " (held here)" ) : QString{} );
	}
	lines << QString{} << tr( "<b>Shader</b> %1: %2 instructions, %3 of them texture reads; %4 temporaries" ).arg( QString::fromStdString( program.Profile() ) ).arg( use.m_nInstructions ).arg( use.m_nTextureReads ).arg( use.m_nTemps );
	for ( const int nRegister : use.m_Constants )
	{
		const xv::shader::Constant_t *pConstant = program.FindConstant( 2, nRegister );
		const QString szName = pConstant ? QString::fromStdString( pConstant->m_szName ).toHtmlEscaped() : QString{};
		QString szWhat;
		if ( nRegister >= 0 && nRegister < 4 )
			szWhat = tr( "the material's $c%1" ).arg( nRegister );
		else if ( nRegister >= 4 && nRegister < 8 )
		{
			const int nSampler = nRegister - 4;
			const xv::texture::CImage &texture = pEffect->GetTexture( nSampler );
			szWhat = pEffect->IsFrameSampler( nSampler ) ? tr( "1/size of s%1, the frame" ).arg( nSampler ) : texture.Empty() ? tr( "1/size of s%1 (none)" ).arg( nSampler )
																															  : tr( "1/size of s%1: %2 by %3" ).arg( nSampler ).arg( texture.m_nWidth ).arg( texture.m_nHeight );
		}
		else
			szWhat = tr( "not set by screenspace_general (0)" );
		lines << QStringLiteral( "c%1 %2: %3" ).arg( nRegister ).arg( szName, szWhat );
	}
	constexpr std::array<const char *, 4> PARAMETERS{ "$basetexture", "$texture1", "$texture2", "$texture3" };
	for ( const int nSampler : use.m_Samplers )
	{
		const xv::shader::Constant_t *pSampler = program.FindConstant( 3, nSampler );
		const QString szName = pSampler ? QString::fromStdString( pSampler->m_szName ).toHtmlEscaped() : QString{};
		QString szWhat;
		if ( nSampler >= 4 )
			szWhat = tr( "not bound (black)" );
		else if ( pEffect->IsFrameSampler( nSampler ) )
			szWhat = tr( "%1: the frame" ).arg( QLatin1String( PARAMETERS[ static_cast<std::size_t>( nSampler ) ] ) );
		else if ( const xv::texture::CImage &texture = pEffect->GetTexture( nSampler ); texture.Empty() )
			szWhat = tr( "%1: none (black)" ).arg( QLatin1String( PARAMETERS[ static_cast<std::size_t>( nSampler ) ] ) );
		else
			szWhat = tr( "%1 %2: %3 by %4" ).arg( QLatin1String( PARAMETERS[ static_cast<std::size_t>( nSampler ) ] ), QString::fromStdString( now.m_Textures[ static_cast<std::size_t>( nSampler ) ] ).toHtmlEscaped() ).arg( texture.m_nWidth ).arg( texture.m_nHeight );
		lines << QStringLiteral( "s%1 %2: %3" ).arg( nSampler ).arg( szName, szWhat );
	}
	lines << QString{};
	if ( m_flSoftwareMs >= 0.0 )
		lines << tr( "<b>Cost</b> in software: about %1 ms a 1920 by 1080 frame" ).arg( m_flSoftwareMs, 0, 'f', 0 );
	if ( pEffect->GetShaderPath().contains( QStringLiteral( "screenspace_font_line" ), Qt::CaseInsensitive ) )
	{
		lines << QString{} << tr( "<b>screenspace_font_line</b>" );
		for ( const QString &szLine : FontLineConstants( now.m_Constants ) )
			lines << szLine.toHtmlEscaped();
	}
	m_pStatistics->setText( lines.join( QStringLiteral( "<br>" ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapScreenEffectPanel::GetStatistics() const
{
	return m_pStatistics->text();
}
