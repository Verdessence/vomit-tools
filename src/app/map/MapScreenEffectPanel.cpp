//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapScreenEffectPanel.h.
//
//===========================================================================//
#include "MapScreenEffectPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "Graphics.h"
#include "map/MapDocument.h"
#include "map/MapEditor.h"
#include "map/MapScreenEffect.h"
#include "map/MaterialBrowser.h"

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
	m_pMaterial->setToolTip( tr( "A screenspace_general material: one the map names for the screen, or any typed or browsed" ) );
	m_pMaterial->lineEdit()->setPlaceholderText( tr( "effects/..." ) );
	m_pBrowse = new QToolButton;
	m_pBrowse->setObjectName( QStringLiteral( "mapScreenEffectBrowse" ) );
	m_pBrowse->setText( tr( "Browse..." ) );
	m_pBrowse->setToolTip( tr( "Pick the material in the texture browser" ) );
	pRow->addWidget( m_pMaterial, 1 );
	pRow->addWidget( m_pBrowse );
	pLayout->addLayout( pRow );
	m_pShown = new QCheckBox( tr( "Show over the 3D views" ) );
	m_pShown->setObjectName( QStringLiteral( "mapScreenEffectShown" ) );
	pLayout->addWidget( m_pShown );
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "mapScreenEffectStatus" ) );
	m_pStatus->setWordWrap( true );
	m_pStatus->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pLayout->addWidget( m_pStatus );
	// c0 to c3, as $c0_x... set them (the material's proxies' values while not changed here).
	auto *pGrid = new QGridLayout;
	for ( int c = 0; c < 4; c++ )
	{
		pGrid->addWidget( new QLabel( QStringLiteral( "c%1" ).arg( c ) ), c, 0 );
		for ( int i = 0; i < 4; i++ )
		{
			auto *pSpin = new QDoubleSpinBox;
			pSpin->setObjectName( QStringLiteral( "mapScreenEffectC%1%2" ).arg( c ).arg( QLatin1Char( "xyzw"[ i ] ) ) );
			pSpin->setRange( -100000.0, 100000.0 );
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
	pLayout->addLayout( pGrid );
	m_pReset = new QPushButton( tr( "Material's Values" ) );
	m_pReset->setObjectName( QStringLiteral( "mapScreenEffectReset" ) );
	m_pReset->setToolTip( tr( "c0 to c3 back to what the material and its proxies give" ) );
	pLayout->addWidget( m_pReset );
	pLayout->addStretch();

	connect( m_pMaterial, &QComboBox::textActivated, this, [ this ]( const QString &szMaterial ) { m_Editor.SetScreenEffect( szMaterial ); } );
	connect( m_pMaterial->lineEdit(), &QLineEdit::editingFinished, this, [ this ] {
		if ( m_pMaterial->currentText() != ( m_Editor.GetScreenEffect() ? m_Editor.GetScreenEffect()->GetMaterial() : QString{} ) )
			m_Editor.SetScreenEffect( m_pMaterial->currentText() );
	} );
	connect( m_pBrowse, &QToolButton::clicked, this, [ this ] {
		const auto &game = m_Editor.GetDocument().GetGame();
		if ( !game )
			return;
		if ( const std::optional<QString> picked = CMaterialBrowser::Pick( this, *game, m_Named, m_pMaterial->currentText() ) )
			m_Editor.SetScreenEffect( *picked );
	} );
	connect( m_pShown, &QCheckBox::toggled, this, [ this ]( bool bOn ) {
		if ( !m_bFilling )
			m_Editor.ShowScreenEffect( bOn );
	} );
	connect( m_pReset, &QPushButton::clicked, this, [ this ] { m_Editor.SetScreenEffectOverrides( {} ); } );
	connect( &m_Editor, &CMapEditor::ScreenEffectChanged, this, &CMapScreenEffectPanel::EffectChanged );
	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, &CMapScreenEffectPanel::Refresh );
	Refresh();
	EffectChanged();
}

//-----------------------------------------------------------------------------
// Purpose: env_screenoverlay's OverlayName1 to OverlayName10, and the material after any
//          r_screenoverlay in a key or output (point_clientcommand's, a logic_relay's).
//-----------------------------------------------------------------------------
void CMapScreenEffectPanel::Refresh()
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	QStringList named;
	const auto add = [ &named ]( QString szName ) {
		szName = szName.trimmed();
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
		for ( const xv::map::EntityKey_t &key : entity.m_Keys )
		{
			if ( key.m_bDeleted )
				continue;
			const QString szText = QString::fromUtf8( key.m_szValue.data(), static_cast<qsizetype>( key.m_szValue.size() ) );
			if ( bOverlay && QString::fromUtf8( key.m_szKey.data(), static_cast<qsizetype>( key.m_szKey.size() ) ).startsWith( QStringLiteral( "OverlayName" ), Qt::CaseInsensitive ) )
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
	const QSignalBlocker blocker( m_pMaterial );
	m_pMaterial->clear();
	m_pMaterial->addItems( named );
	m_pMaterial->setCurrentText( szCurrent );
}

//-----------------------------------------------------------------------------
// Purpose: The material, whether it shows, how it is drawn and its constants as they are now.
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
	{
		szStatus = tr( "%1 (%2)" ).arg( pEffect->GetShaderPath(), pEffect->GetProfile() );
#ifdef VOMIT_TOOLS_GPU
		if ( CGraphics::Instance().IsGpu() )
		{
			const bool bCard = pEffect->GetBaked().isValid();
			szStatus += bCard ? tr( ", drawn on the graphics card" ) : tr( ", drawn in software views only: %1" ).arg( pEffect->GetCardProblem() );
		}
		else
#endif
			szStatus += tr( ", drawn in software" );
		if ( pEffect->IsAnimated() )
			szStatus += tr( "; its proxies move it" );
	}
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
