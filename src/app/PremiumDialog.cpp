//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See PremiumDialog.h.
//
//===========================================================================//
#include "PremiumDialog.h"

#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMovie>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "kv1/Kv1.h"

namespace
{

constexpr auto SHOWN_KEY = "premiumShown";

// The window's text, from data/premium.txt.
struct PremiumText_t
{
	xv::kv1::CParseResult m_Parsed;

	[[nodiscard]] QString Get( std::string_view szKey, const QString &szFallback = {} ) const
	{
		const xv::kv1::CNode *pRoot = m_Parsed.m_Document.Find( "premium" );
		const xv::kv1::CNode *pNode = pRoot ? pRoot->Find( szKey ) : nullptr;
		return pNode && pNode->IsPair() ? QString::fromStdString( pNode->GetValue() ).replace( QStringLiteral( "\\n" ), QStringLiteral( "\n" ) ) : szFallback;
	}
	[[nodiscard]] const xv::kv1::CNode *Features() const
	{
		const xv::kv1::CNode *pRoot = m_Parsed.m_Document.Find( "premium" );
		return pRoot ? pRoot->Find( "features" ) : nullptr;
	}
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PremiumText_t ReadText()
{
	QFile file{ QStringLiteral( ":/data/premium.txt" ) };
	return { xv::kv1::Parse( file.open( QIODevice::ReadOnly ) ? file.readAll().toStdString() : std::string{} ) };
}

//-----------------------------------------------------------------------------
// Purpose: A tick or a cross for a column of the table.
//-----------------------------------------------------------------------------
QLabel *Mark( bool bHas )
{
	auto *pMark = new QLabel( bHas ? QStringLiteral( "✔" ) : QStringLiteral( "✘" ) );
	pMark->setAlignment( Qt::AlignCenter );
	pMark->setStyleSheet( bHas ? QStringLiteral( "color: #3a9a3a; font-weight: bold;" ) : QStringLiteral( "color: #b04040;" ) );
	return pMark;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPremiumDialog::CPremiumDialog( QWidget *pParent )
	: QDialog( pParent )
{
	setObjectName( QStringLiteral( "premium" ) );
	const PremiumText_t text = ReadText();
	setWindowTitle( text.Get( "title", QStringLiteral( "vomit-tools Premium" ) ) );

	// The character, playing over and over (a GIF; a still picture works too).
	auto *pPicture = new QLabel;
	pPicture->setObjectName( QStringLiteral( "premiumPicture" ) );
	pPicture->setAlignment( Qt::AlignTop | Qt::AlignHCenter );
	auto *pMovie = new QMovie( QStringLiteral( ":/assets/premium_character.gif" ), {}, this );
	pMovie->setCacheMode( QMovie::CacheAll );
	if ( pMovie->isValid() )
	{
		pMovie->jumpToFrame( 0 );
		pMovie->setScaledSize( pMovie->currentImage().size().scaled( 280, 300, Qt::KeepAspectRatio ) );
		pPicture->setMovie( pMovie );
		pMovie->start();
	}

	auto *pGreeting = new QLabel( text.Get( "greeting" ) );
	pGreeting->setObjectName( QStringLiteral( "premiumGreeting" ) );
	pGreeting->setWordWrap( true );
	pGreeting->setAlignment( Qt::AlignCenter );
	QFont big = pGreeting->font();
	big.setPointSizeF( big.pointSizeF() * 1.3 );
	pGreeting->setFont( big );

	// What each has, a row a feature, then the price.
	auto *pTable = new QGridLayout;
	pTable->setHorizontalSpacing( 18 );
	QFont bold = font();
	bold.setBold( true );
	const auto heading = [ &bold ]( const QString &szText ) {
		auto *pLabel = new QLabel( szText );
		pLabel->setFont( bold );
		pLabel->setAlignment( Qt::AlignCenter );
		return pLabel;
	};
	pTable->addWidget( heading( text.Get( "free", tr( "Free" ) ) ), 0, 1 );
	pTable->addWidget( heading( text.Get( "premium", tr( "Premium" ) ) ), 0, 2 );
	int nRow = 1;
	if ( const xv::kv1::CNode *pFeatures = text.Features() )
	{
		for ( const xv::kv1::CNode &feature : pFeatures->GetChildren() )
		{
			if ( !feature.IsPair() )
			{
				continue;
			}
			const QStringList has = QString::fromStdString( feature.GetValue() ).split( u' ', Qt::SkipEmptyParts );
			auto *pName = new QLabel( QString::fromStdString( feature.GetKey() ) );
			pName->setWordWrap( true );
			pTable->addWidget( pName, nRow, 0 );
			pTable->addWidget( Mark( has.value( 0 ).compare( u"yes", Qt::CaseInsensitive ) == 0 ), nRow, 1 );
			pTable->addWidget( Mark( has.value( 1 ).compare( u"yes", Qt::CaseInsensitive ) == 0 ), nRow, 2 );
			nRow++;
		}
	}
	auto *pPriceFree = heading( text.Get( "free_price" ) );
	auto *pPricePremium = heading( text.Get( "premium_price" ) );
	pTable->addWidget( pPriceFree, nRow, 1 );
	pTable->addWidget( pPricePremium, nRow, 2 );
	pTable->setColumnStretch( 0, 1 );

	// Anything goes in; nothing is kept.
	m_pCard = new QLineEdit;
	m_pCard->setObjectName( QStringLiteral( "premiumCard" ) );
	m_pExpiry = new QLineEdit;
	m_pCode = new QLineEdit;
	m_pCode->setEchoMode( QLineEdit::Password );
	auto *pForm = new QFormLayout;
	pForm->addRow( text.Get( "card", tr( "Card number:" ) ), m_pCard );
	pForm->addRow( text.Get( "expiry", tr( "Expiry date:" ) ), m_pExpiry );
	pForm->addRow( text.Get( "code", tr( "Security code:" ) ), m_pCode );
	auto *pNote = new QLabel( text.Get( "note" ) );
	pNote->setEnabled( false );
	pNote->setWordWrap( true );
	pNote->setAlignment( Qt::AlignCenter );

	auto *pAccept = new QPushButton( text.Get( "accept", tr( "OK" ) ) );
	pAccept->setObjectName( QStringLiteral( "premiumAccept" ) );
	pAccept->setDefault( true );
	auto *pLater = new QPushButton( text.Get( "later", tr( "Later" ) ) );
	connect( pAccept, &QPushButton::clicked, this, [ this, szThanks = text.Get( "thanks" ) ] {
		m_pCard->clear();
		m_pExpiry->clear();
		m_pCode->clear();
		if ( !szThanks.isEmpty() )
		{
			QMessageBox::information( this, windowTitle(), szThanks );
		}
		accept();
	} );
	connect( pLater, &QPushButton::clicked, this, &QDialog::reject );
	auto *pButtons = new QHBoxLayout;
	pButtons->addStretch();
	pButtons->addWidget( pLater );
	pButtons->addWidget( pAccept );
	pButtons->addStretch();

	auto *pRight = new QVBoxLayout;
	pRight->addWidget( pGreeting );
	pRight->addSpacing( 6 );
	pRight->addLayout( pTable );
	pRight->addSpacing( 6 );
	pRight->addLayout( pForm );
	pRight->addWidget( pNote );
	pRight->addLayout( pButtons );
	auto *pLayout = new QHBoxLayout( this );
	pLayout->addWidget( pPicture );
	pLayout->addLayout( pRight, 1 );
	resize( 720, sizeHint().height() );
	MarkShown();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPremiumDialog::HasBeenShown()
{
	return QSettings{}.value( SHOWN_KEY, false ).toBool();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPremiumDialog::MarkShown()
{
	QSettings{}.setValue( SHOWN_KEY, true );
}
