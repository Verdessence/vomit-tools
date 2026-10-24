//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See TexturePropertiesDialog.h.
//
//===========================================================================//
#include "TexturePropertiesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Language.h"
#include "TextureOptionsDialog.h"
#include "texture/Sheet.h"

using namespace xv::texture;

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: A resource as the list shows it: its name, from lang_en.txt, or its tag.
//-----------------------------------------------------------------------------
QString ResourceName( const VtfExtraResource_t &resource )
{
	static constexpr char DIGITS[] = "0123456789abcdef";
	std::string szHex;
	for ( const std::uint8_t n : resource.m_Tag )
	{
		szHex += DIGITS[ n >> 4 ];
		szHex += DIGITS[ n & 15 ];
	}
	const xv::particles::PlainName_t plain = ParticleNames().Texture( "res:" + szHex );
	return plain.m_szName == szHex ? QObject::tr( "Unknown (%1)" ).arg( Qs( szHex ) ) : Qs( plain.m_szName );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTexturePropertiesDialog::CTexturePropertiesDialog( const CVtf &vtf, QWidget *pParent )
	: QDialog( pParent ),
	  m_Vtf( vtf ),
	  m_Initial( HeaderOf( vtf ) )
{
	setWindowTitle( tr( "Texture Properties" ) );
	const VtfInfo_t &info = vtf.m_Info;
	auto *pForm = new QFormLayout;
	pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );

	m_pVersion = new QComboBox;
	for ( int nMinor = info.m_nDepth > 1 ? 2 : 1; nMinor <= 5; nMinor++ )
	{
		m_pVersion->addItem( QStringLiteral( "7.%1" ).arg( nMinor ), nMinor );
	}
	m_pVersion->setCurrentIndex( std::max( 0, m_pVersion->findData( m_Initial.m_nVersionMinor ) ) );
	m_pVersion->setToolTip( tr( "7.2 opens in every Source game and tool. 7.3 and later can carry sprite sheets and other resources." ) );
	pForm->addRow( tr( "Version" ), m_pVersion );

	m_pFirstFrame = new QSpinBox;
	m_pFirstFrame->setRange( 0, 65535 );
	m_pFirstFrame->setValue( m_Initial.m_nFirstFrame );
	m_pFirstFrame->setToolTip( tr( "The frame an animated texture starts on" ) );
	pForm->addRow( tr( "First frame" ), m_pFirstFrame );

	m_pBumpScale = new QDoubleSpinBox;
	m_pBumpScale->setRange( -100.0, 100.0 );
	m_pBumpScale->setDecimals( 3 );
	m_pBumpScale->setSingleStep( 0.1 );
	m_pBumpScale->setValue( static_cast<double>( m_Initial.m_flBumpScale ) );
	pForm->addRow( tr( "Bump scale" ), m_pBumpScale );

	auto *pReflect = new QHBoxLayout;
	for ( int c = 0; c < 3; c++ )
	{
		m_pReflectivity[ c ] = new QDoubleSpinBox;
		m_pReflectivity[ c ]->setRange( 0.0, 100.0 );
		m_pReflectivity[ c ]->setDecimals( 4 );
		m_pReflectivity[ c ]->setSingleStep( 0.01 );
		m_pReflectivity[ c ]->setPrefix( QStringLiteral( "RGB" ).mid( c, 1 ) + u' ' );
		m_pReflectivity[ c ]->setValue( static_cast<double>( m_Initial.m_vecReflectivity[ static_cast<std::size_t>( c ) ] ) );
		pReflect->addWidget( m_pReflectivity[ c ] );
	}
	auto *pWorkOut = new QPushButton( tr( "Work Out" ) );
	pWorkOut->setToolTip( tr( "The average colour of the first frame in linear light, as Valve's tools set it" ) );
	connect( pWorkOut, &QPushButton::clicked, this, [ this ] {
		const auto reflectivity = Reflectivity( m_Vtf );
		for ( std::size_t c = 0; c < 3; c++ )
		{
			m_pReflectivity[ c ]->setValue( static_cast<double>( reflectivity[ c ] ) );
		}
	} );
	pReflect->addWidget( pWorkOut );
	pForm->addRow( tr( "Reflectivity" ), pReflect );

	m_pLowRes = new QComboBox;
	m_pLowRes->addItem( vtf.LowRes().empty() ? tr( "None" ) : tr( "Keep" ), static_cast<int>( LowResEdit_t::LOW_RES_EDIT_KEEP ) );
	if ( !vtf.LowRes().empty() )
	{
		m_pLowRes->addItem( tr( "Remove" ), static_cast<int>( LowResEdit_t::LOW_RES_EDIT_REMOVE ) );
	}
	m_pLowRes->addItem( tr( "Make again from the image" ), static_cast<int>( LowResEdit_t::LOW_RES_EDIT_MAKE ) );
	pForm->addRow( tr( "Thumbnail" ), m_pLowRes );

	m_pResources = new QListWidget;
	m_pResources->setObjectName( QStringLiteral( "textureResources" ) );
	m_pResources->setMaximumHeight( 120 );
	m_pResources->setToolTip( tr( "Double-click KeyValues data, LOD settings or extended flags to change them" ) );
	m_Resources = m_Initial.m_Resources;
	FillResources();
	auto *pAdd = new QPushButton( tr( "Add" ) );
	auto *pAddMenu = new QMenu( pAdd );
	pAddMenu->addAction( tr( "KeyValues Data…" ), this, [ this ] { EditResource( { 'K', 'V', 'D' } ); } );
	pAddMenu->addAction( tr( "LOD Control…" ), this, [ this ] { EditResource( { 'L', 'O', 'D' } ); } );
	pAddMenu->addAction( tr( "Extended Flags…" ), this, [ this ] { EditResource( { 'T', 'S', 'O' } ); } );
	pAddMenu->addAction( tr( "Sprite Sheet from a .sht File…" ), this, [ this ] {
		const QString szFile = QFileDialog::getOpenFileName( this, tr( "Sprite Sheet" ), {}, tr( "Sheets (*.sht);;All files (*)" ) );
		QFile file{ szFile };
		if ( szFile.isEmpty() || !file.open( QIODevice::ReadOnly ) )
		{
			return;
		}
		const QByteArray data = file.readAll();
		const std::span bytes{ reinterpret_cast<const std::byte *>( data.constData() ), static_cast<std::size_t>( data.size() ) };
		if ( !ReadSheet( bytes ) )
		{
			QMessageBox::warning( this, tr( "Sprite Sheet" ), tr( "%1 isn't a sprite sheet this can read." ).arg( szFile ) );
			return;
		}
		SetResource( { SHEET_RESOURCE, 0, 0, { bytes.begin(), bytes.end() } } );
	} );
	pAdd->setMenu( pAddMenu );
	auto *pRemove = new QPushButton( tr( "Remove" ) );
	connect( pRemove, &QPushButton::clicked, this, [ this ] {
		const int nRow = m_pResources->currentRow();
		if ( nRow >= 0 )
		{
			m_Resources.erase( m_Resources.begin() + nRow );
			FillResources();
		}
	} );
	connect( m_pResources, &QListWidget::itemDoubleClicked, this, [ this ]( QListWidgetItem *pItem ) { EditResource( m_Resources[ static_cast<std::size_t>( m_pResources->row( pItem ) ) ].m_Tag ); } );
	auto *pResourceButtons = new QVBoxLayout;
	pResourceButtons->addWidget( pAdd );
	pResourceButtons->addWidget( pRemove );
	pResourceButtons->addStretch();
	auto *pResourceRow = new QHBoxLayout;
	pResourceRow->addWidget( m_pResources, 1 );
	pResourceRow->addLayout( pResourceButtons );
	pForm->addRow( tr( "Resources" ), pResourceRow );

	auto *pNote = new QLabel( tr( "The pictures stay exactly as they are; only the header changes. Resources need 7.3 or later." ) );
	pNote->setWordWrap( true );
	pNote->setEnabled( false );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( pForm );
	pLayout->addWidget( pNote );
	pLayout->addWidget( pButtons );
	resize( 720, sizeHint().height() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
VtfHeaderEdit_t CTexturePropertiesDialog::GetEdit() const
{
	VtfHeaderEdit_t edit = m_Initial;
	edit.m_nVersionMinor = m_pVersion->currentData().toInt();
	edit.m_nFirstFrame = m_pFirstFrame->value();
	edit.m_flBumpScale = static_cast<float>( m_pBumpScale->value() );
	for ( std::size_t c = 0; c < 3; c++ )
	{
		edit.m_vecReflectivity[ c ] = static_cast<float>( m_pReflectivity[ c ]->value() );
	}
	edit.m_nLowRes = static_cast<LowResEdit_t>( m_pLowRes->currentData().toInt() );
	edit.m_Resources = m_Resources;
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTexturePropertiesDialog::FillResources()
{
	m_pResources->clear();
	for ( const VtfExtraResource_t &resource : m_Resources )
	{
		m_pResources->addItem( ResourceName( resource ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Puts a resource in, in place of one with the same tag.
//-----------------------------------------------------------------------------
void CTexturePropertiesDialog::SetResource( VtfExtraResource_t resource )
{
	const auto it = std::ranges::find( m_Resources, resource.m_Tag, &VtfExtraResource_t::m_Tag );
	if ( it != m_Resources.end() )
	{
		*it = std::move( resource );
	}
	else
	{
		m_Resources.push_back( std::move( resource ) );
	}
	FillResources();
	if ( m_pVersion->currentData().toInt() < 3 )
	{
		m_pVersion->setCurrentIndex( m_pVersion->findData( 4 ) ); // resources need 7.3 or later
	}
}

//-----------------------------------------------------------------------------
// Purpose: Asks for a KVD text, LOD clamps or extended flags, starting from what is there.
//-----------------------------------------------------------------------------
void CTexturePropertiesDialog::EditResource( std::array<std::uint8_t, 3> tag )
{
	const auto it = std::ranges::find( m_Resources, tag, &VtfExtraResource_t::m_Tag );
	const VtfExtraResource_t current = it != m_Resources.end() ? *it : VtfExtraResource_t{ tag, 0, 0, {} };
	if ( tag == std::array<std::uint8_t, 3>{ 'K', 'V', 'D' } )
	{
		bool bOk = false;
		const QString szText = QInputDialog::getMultiLineText( this, tr( "KeyValues Data" ), tr( "Text kept with the texture:" ),
			QString::fromUtf8( reinterpret_cast<const char *>( current.m_Data.data() ), static_cast<qsizetype>( current.m_Data.size() ) ), &bOk );
		if ( bOk )
		{
			const QByteArray utf8 = szText.toUtf8();
			const auto *p = reinterpret_cast<const std::byte *>( utf8.constData() );
			SetResource( { tag, 0, 0, { p, p + utf8.size() } } );
		}
	}
	else if ( tag == std::array<std::uint8_t, 3>{ 'L', 'O', 'D' } )
	{
		// Two bytes: how many mipmap levels the game may drop across and down at low quality.
		bool bOk = false;
		const int nU = QInputDialog::getInt( this, tr( "LOD Control" ), tr( "Mipmap levels the game may drop across:" ), static_cast<int>( current.m_nValue & 0xFF ), 0, 15, 1, &bOk );
		if ( !bOk )
		{
			return;
		}
		const int nV = QInputDialog::getInt( this, tr( "LOD Control" ), tr( "…and down:" ), static_cast<int>( ( current.m_nValue >> 8 ) & 0xFF ), 0, 15, 1, &bOk );
		if ( bOk )
		{
			SetResource( { tag, VtfResource_t::NO_DATA, static_cast<std::uint32_t>( nU | nV << 8 ), {} } );
		}
	}
	else if ( tag == std::array<std::uint8_t, 3>{ 'T', 'S', 'O' } )
	{
		bool bOk = false;
		const QString szValue = QInputDialog::getText( this, tr( "Extended Flags" ), tr( "Value (hex):" ), QLineEdit::Normal, QStringLiteral( "%1" ).arg( current.m_nValue, 8, 16, QLatin1Char( '0' ) ), &bOk );
		if ( bOk )
		{
			SetResource( { tag, VtfResource_t::NO_DATA, szValue.toUInt( nullptr, 16 ), {} } );
		}
	}
}
