//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MakeMaterialDialog.h.
//
//===========================================================================//
#include "MakeMaterialDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace
{

// Surface properties every Source game has, for when the game's own list isn't known.
const char *const COMMON_SURFACES[] = { "default", "concrete", "brick", "metal", "metal_box", "metalgrate", "wood", "wood_crate", "dirt", "grass", "gravel", "sand", "mud", "tile", "plaster", "glass", "rock", "carpet", "cardboard", "plastic", "rubber", "flesh", "water", "chainlink", "ice", "snow" };

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
CMakeMaterialDialog::CMakeMaterialDialog( const QString &szGamePath, bool bAlpha, bool bNormal, const QStringList &surfaceProps, QWidget *pParent )
	: QDialog( pParent ),
	  m_bNormal( bNormal )
{
	setWindowTitle( tr( "Make Material" ) );
	auto *pForm = new QFormLayout;
	pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );

	m_pShader = new QComboBox;
	m_pShader->setObjectName( QStringLiteral( "materialShader" ) );
	m_pShader->addItem( tr( "LightmappedGeneric · brushes (walls, floors)" ), QStringLiteral( "LightmappedGeneric" ) );
	m_pShader->addItem( tr( "VertexLitGeneric · models" ), QStringLiteral( "VertexLitGeneric" ) );
	m_pShader->addItem( tr( "UnlitGeneric · no lighting (sprites, glowing signs, UI)" ), QStringLiteral( "UnlitGeneric" ) );
	m_pShader->addItem( tr( "WorldVertexTransition · blended displacements" ), QStringLiteral( "WorldVertexTransition" ) );
	pForm->addRow( tr( "Shader" ), m_pShader );

	m_pTexture = new QLineEdit( TexturePathFor( szGamePath ) );
	m_pTexture->setToolTip( tr( "Where the texture is under materials/, without .vtf" ) );
	pForm->addRow( bNormal ? QStringLiteral( "$bumpmap" ) : QStringLiteral( "$basetexture" ), m_pTexture );

	m_pAlpha = new QComboBox;
	m_pAlpha->addItem( tr( "Solid" ), QString{} );
	m_pAlpha->addItem( tr( "See-through ($translucent)" ), QStringLiteral( "$translucent" ) );
	m_pAlpha->addItem( tr( "Cut out ($alphatest): fences, leaves" ), QStringLiteral( "$alphatest" ) );
	m_pAlpha->setCurrentIndex( bAlpha && !bNormal ? 1 : 0 );
	m_pAlpha->setEnabled( !bNormal );
	pForm->addRow( tr( "Alpha" ), m_pAlpha );

	m_pSurface = new QComboBox;
	m_pSurface->setEditable( true );
	QStringList surfaces = surfaceProps;
	if ( surfaces.isEmpty() )
	{
		for ( const char *pszSurface : COMMON_SURFACES )
		{
			surfaces << QString::fromLatin1( pszSurface );
		}
	}
	m_pSurface->addItem( tr( "(leave out)" ), QString{} );
	for ( const QString &szSurface : surfaces )
	{
		m_pSurface->addItem( szSurface, szSurface );
	}
	m_pSurface->setToolTip( tr( "How it sounds and reacts when walked on or shot ($surfaceprop)" ) );
	pForm->addRow( QStringLiteral( "$surfaceprop" ), m_pSurface );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( pForm );
	if ( bNormal )
	{
		auto *pNote = new QLabel( tr( "This texture is a normal map, so it goes in $bumpmap; give the material its own $basetexture too." ) );
		pNote->setWordWrap( true );
		pLayout->addWidget( pNote );
	}
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	pLayout->addWidget( pButtons );
	resize( 520, sizeHint().height() );
}

//-----------------------------------------------------------------------------
// Purpose: The material's text.
//-----------------------------------------------------------------------------
QString CMakeMaterialDialog::GetText() const
{
	QString szText = QStringLiteral( "\"%1\"\n{\n" ).arg( m_pShader->currentData().toString() );
	const auto line = [ &szText ]( const QString &szKey, const QString &szValue ) { szText += QStringLiteral( "\t\"%1\" \"%2\"\n" ).arg( szKey, szValue ); };
	line( m_bNormal ? QStringLiteral( "$bumpmap" ) : QStringLiteral( "$basetexture" ), m_pTexture->text().trimmed() );
	if ( const QString szAlpha = m_pAlpha->currentData().toString(); !szAlpha.isEmpty() )
	{
		line( szAlpha, QStringLiteral( "1" ) );
	}
	const int nSurface = m_pSurface->findText( m_pSurface->currentText() );
	const QString szSurface = nSurface >= 0 ? m_pSurface->itemData( nSurface ).toString() : m_pSurface->currentText().trimmed();
	if ( !szSurface.isEmpty() )
	{
		line( QStringLiteral( "$surfaceprop" ), szSurface );
	}
	szText += QStringLiteral( "}\n" );
	return szText;
}
