//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MakeMaterialDialog.h.
//
//===========================================================================//
#include "MakeMaterialDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Language.h"

namespace
{

// Surface properties every Source game has, for when the game's own list isn't known.
const char *const COMMON_SURFACES[] = { "default", "concrete", "brick", "metal", "metal_box", "metalgrate", "wood", "wood_crate", "dirt", "grass", "gravel", "sand", "mud", "tile", "plaster", "glass", "rock", "carpet", "cardboard", "plastic", "rubber", "flesh", "water", "chainlink", "ice", "snow" };

// The shaders offered, the usual ones first.
const char *const SHADERS[] = { "LightmappedGeneric", "VertexLitGeneric", "UnlitGeneric", "WorldVertexTransition", "UnlitTwoTexture", "Sprite", "SpriteCard", "Refract" };

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Shaders lit by the world's light, which reflections, glow and normal maps work with.
//-----------------------------------------------------------------------------
bool IsLit( const QString &szShader )
{
	return szShader == u"LightmappedGeneric" || szShader == u"VertexLitGeneric" || szShader == u"WorldVertexTransition";
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: $basetexture for a texture's game path: without materials/ and the ending.
//-----------------------------------------------------------------------------
QString CMakeMaterialDialog::TexturePathFor( const QString &szGamePath )
{
	QString szPath = QString{ szGamePath }.replace( u'\\', u'/' );
	if ( szPath.startsWith( u"materials/", Qt::CaseInsensitive ) )
	{
		szPath.remove( 0, 10 );
	}
	if ( szPath.endsWith( u".vtf", Qt::CaseInsensitive ) )
	{
		szPath.chop( 4 );
	}
	return szPath;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMakeMaterialDialog::CMakeMaterialDialog( const QString &szGamePath, bool bAlpha, bool bNormal, const std::vector<Choice_t> &surfaceProps, QWidget *pParent )
	: QDialog( pParent ),
	  m_bNormal( bNormal )
{
	setWindowTitle( tr( "Create VMT" ) );
	const xv::particles::CNames &names = ParticleNames();
	m_pForm = new QFormLayout;
	m_pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );

	m_pShader = new QComboBox;
	m_pShader->setObjectName( QStringLiteral( "materialShader" ) );
	std::vector<Choice_t> shaders;
	for ( const char *pszShader : SHADERS )
	{
		shaders.push_back( { QString::fromLatin1( pszShader ), Qs( names.Shader( pszShader ).m_szMeaning ), QString::fromLatin1( pszShader ) } );
	}
	FillListBox( m_pShader, shaders );
	m_pForm->addRow( tr( "Shader" ), m_pShader );

	m_pTexture = new QLineEdit( TexturePathFor( szGamePath ) );
	m_pTexture->setToolTip( tr( "Where the texture is under materials/, without .vtf" ) );
	m_pForm->addRow( bNormal ? QStringLiteral( "$bumpmap" ) : QStringLiteral( "$basetexture" ), m_pTexture );
	m_pBump = new QLineEdit;
	m_pBump->setObjectName( QStringLiteral( "materialBump" ) );
	m_pBump->setPlaceholderText( tr( "none" ) );
	m_pBump->setToolTip( Qs( names.MaterialParameter( "$bumpmap" ).m_szMeaning ) );
	if ( !bNormal )
	{
		m_pForm->addRow( QStringLiteral( "$bumpmap" ), m_pBump );
	}

	// How alpha is used: none of these premultiply; each reads the base texture's alpha as is.
	m_pAlpha = new QComboBox;
	m_pAlpha->setObjectName( QStringLiteral( "materialAlpha" ) );
	FillListBox( m_pAlpha, {
							   { tr( "Solid" ), tr( "Alpha isn't used for see-through (other settings may use it as a mask)" ), QString{} },
							   { tr( "Blended" ), tr( "$translucent: smooth see-through by alpha, for glass, smoke and soft edges. Drawn after solid surfaces, so overlaps can sort wrongly" ), QStringLiteral( "$translucent" ) },
							   { tr( "Cut out" ), tr( "$alphatest: each texel is there or not, split at a threshold, for fences, leaves and grates. Sorts and casts shadows like a solid surface" ), QStringLiteral( "$alphatest" ) },
							   { tr( "Added" ), tr( "$additive: adds its colour to what's behind, for glows, fire and light; black vanishes, alpha isn't needed" ), QStringLiteral( "$additive" ) },
						   } );
	m_pAlpha->setCurrentIndex( bAlpha && !bNormal ? 1 : 0 );
	m_pAlpha->setEnabled( !bNormal );
	m_pThreshold = new QDoubleSpinBox;
	m_pThreshold->setRange( 0.0, 1.0 );
	m_pThreshold->setSingleStep( 0.05 );
	m_pThreshold->setValue( 0.5 );
	m_pThreshold->setPrefix( tr( "cut below " ) );
	m_pThreshold->setToolTip( Qs( names.MaterialParameter( "$alphatestreference" ).m_szMeaning ) );
	auto *pAlphaRow = new QHBoxLayout;
	pAlphaRow->addWidget( m_pAlpha, 1 );
	pAlphaRow->addWidget( m_pThreshold );
	m_pForm->addRow( tr( "Alpha" ), pAlphaRow );

	m_pTwoSided = new QCheckBox( tr( "Two-sided ($nocull)" ) );
	m_pTwoSided->setToolTip( Qs( names.MaterialParameter( "$nocull" ).m_szMeaning ) );
	m_pReflect = new QCheckBox( tr( "Reflections ($envmap env_cubemap)" ) );
	m_pReflect->setToolTip( tr( "Reflects the map's nearest env_cubemap" ) );
	m_pReflectMask = new QComboBox;
	FillListBox( m_pReflectMask, {
									 { tr( "everywhere" ), tr( "The whole surface reflects as much" ), QString{} },
									 { tr( "by the base's alpha" ), tr( "$basealphaenvmapmask: shiny where the base texture's alpha is bright" ), QStringLiteral( "$basealphaenvmapmask" ) },
									 { tr( "by the normal map's alpha" ), tr( "$normalmapalphaenvmapmask: shiny where the normal map's alpha is bright" ), QStringLiteral( "$normalmapalphaenvmapmask" ) },
								 } );
	auto *pReflectRow = new QHBoxLayout;
	pReflectRow->addWidget( m_pReflect );
	pReflectRow->addWidget( m_pReflectMask, 1 );
	m_pGlow = new QCheckBox( tr( "Glows by the base's alpha ($selfillum)" ) );
	m_pGlow->setToolTip( Qs( names.MaterialParameter( "$selfillum" ).m_szMeaning ) );
	m_pPhong = new QCheckBox( tr( "Shine ($phong)" ) );
	m_pPhong->setToolTip( Qs( names.MaterialParameter( "$phong" ).m_szMeaning ) );
	m_pForm->addRow( QString{}, m_pTwoSided );
	m_pForm->addRow( QString{}, pReflectRow );
	m_pForm->addRow( QString{}, m_pGlow );
	m_pForm->addRow( QString{}, m_pPhong );

	std::vector<Choice_t> surfaces = surfaceProps;
	if ( surfaces.empty() )
	{
		for ( const char *pszSurface : COMMON_SURFACES )
		{
			surfaces.push_back( { QString::fromLatin1( pszSurface ), {} } );
		}
	}
	m_pSurface = MakeSearchBox( surfaces );
	m_pSurface->setObjectName( QStringLiteral( "materialSurface" ) );
	m_pSurface->setCurrentIndex( -1 );
	m_pSurface->lineEdit()->setPlaceholderText( tr( "leave out" ) );
	m_pSurface->setToolTip( Qs( names.MaterialParameter( "$surfaceprop" ).m_szMeaning ) );
	m_pForm->addRow( QStringLiteral( "$surfaceprop" ), m_pSurface );
	m_pKeywords = new QLineEdit;
	m_pKeywords->setPlaceholderText( tr( "none" ) );
	m_pKeywords->setToolTip( Qs( names.MaterialParameter( "%keywords" ).m_szMeaning ) );
	m_pForm->addRow( QStringLiteral( "%keywords" ), m_pKeywords );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( m_pForm );
	if ( bNormal )
	{
		auto *pNormalNote = new QLabel( tr( "This texture is a normal map, so it goes in $bumpmap; give the material its own $basetexture too." ) );
		pNormalNote->setWordWrap( true );
		pLayout->addWidget( pNormalNote );
	}
	auto *pNote = new QLabel( tr( "Anything else can be added in the material editor afterwards." ) );
	pNote->setEnabled( false );
	pLayout->addWidget( pNote );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pButtons->button( QDialogButtonBox::Ok )->setText( tr( "Create" ) );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	pLayout->addWidget( pButtons );

	connect( m_pShader, &QComboBox::currentIndexChanged, this, &CMakeMaterialDialog::Update );
	connect( m_pAlpha, &QComboBox::currentIndexChanged, this, &CMakeMaterialDialog::Update );
	connect( m_pReflect, &QCheckBox::toggled, this, &CMakeMaterialDialog::Update );
	Update();
	resize( 560, sizeHint().height() );
}

//-----------------------------------------------------------------------------
// Purpose: Offers what the shader and the alpha choice work with.
//-----------------------------------------------------------------------------
void CMakeMaterialDialog::Update()
{
	const QString szShader = m_pShader->currentData().toString();
	const bool bLit = IsLit( szShader );
	m_pThreshold->setVisible( m_pAlpha->currentData().toString() == u"$alphatest" );
	m_pBump->setEnabled( bLit );
	m_pReflect->setEnabled( bLit );
	m_pReflectMask->setEnabled( bLit && m_pReflect->isChecked() );
	m_pGlow->setEnabled( bLit );
	m_pPhong->setEnabled( szShader == u"VertexLitGeneric" );
}

//-----------------------------------------------------------------------------
// Purpose: The material's text.
//-----------------------------------------------------------------------------
QString CMakeMaterialDialog::GetText() const
{
	const QString szShader = m_pShader->currentData().toString();
	QString szText = QStringLiteral( "\"%1\"\n{\n" ).arg( szShader );
	const auto line = [ &szText ]( const QString &szKey, const QString &szValue ) { szText += QStringLiteral( "\t\"%1\" \"%2\"\n" ).arg( szKey, szValue ); };
	line( m_bNormal ? QStringLiteral( "$bumpmap" ) : QStringLiteral( "$basetexture" ), m_pTexture->text().trimmed() );
	if ( m_pBump->isEnabled() && !m_bNormal && !m_pBump->text().trimmed().isEmpty() )
	{
		line( QStringLiteral( "$bumpmap" ), m_pBump->text().trimmed() );
	}
	if ( const QString szAlpha = m_pAlpha->isEnabled() ? m_pAlpha->currentData().toString() : QString{}; !szAlpha.isEmpty() )
	{
		line( szAlpha, QStringLiteral( "1" ) );
		if ( szAlpha == u"$alphatest" )
		{
			line( QStringLiteral( "$alphatestreference" ), QString::number( m_pThreshold->value() ) );
		}
	}
	if ( m_pTwoSided->isChecked() )
	{
		line( QStringLiteral( "$nocull" ), QStringLiteral( "1" ) );
	}
	if ( m_pReflect->isEnabled() && m_pReflect->isChecked() )
	{
		line( QStringLiteral( "$envmap" ), QStringLiteral( "env_cubemap" ) );
		if ( const QString szMask = m_pReflectMask->currentData().toString(); !szMask.isEmpty() )
		{
			line( szMask, QStringLiteral( "1" ) );
		}
	}
	if ( m_pGlow->isEnabled() && m_pGlow->isChecked() )
	{
		line( QStringLiteral( "$selfillum" ), QStringLiteral( "1" ) );
	}
	if ( m_pPhong->isEnabled() && m_pPhong->isChecked() )
	{
		line( QStringLiteral( "$phong" ), QStringLiteral( "1" ) );
		line( QStringLiteral( "$phongexponent" ), QStringLiteral( "20" ) );
		line( QStringLiteral( "$phongboost" ), QStringLiteral( "1" ) );
		line( QStringLiteral( "$phongfresnelranges" ), QStringLiteral( "[0.5 0.75 1]" ) );
	}
	if ( const QString szSurface = m_pSurface->currentText().trimmed(); !szSurface.isEmpty() )
	{
		line( QStringLiteral( "$surfaceprop" ), szSurface );
	}
	if ( const QString szKeywords = m_pKeywords->text().trimmed(); !szKeywords.isEmpty() )
	{
		line( QStringLiteral( "%keywords" ), szKeywords );
	}
	szText += QStringLiteral( "}\n" );
	return szText;
}
