//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See FileViews.h.
//
//===========================================================================//
#include "FileViews.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <QFileInfo>
#include <QFontMetricsF>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ImageFiles.h"
#include "MaterialView.h"
#include "SoundPlayer.h"
#include "audio/Audio.h"
#include "dmx/Dmx.h"
#include "kv1/Kv1.h"
#include "sound/Wav.h"

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
// Purpose:
//-----------------------------------------------------------------------------
QPlainTextEdit *MakeTextView( const QString &szText )
{
	auto *pView = new QPlainTextEdit;
	pView->setReadOnly( true );
	pView->setLineWrapMode( QPlainTextEdit::NoWrap );
	pView->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
	pView->setTabStopDistance( QFontMetricsF{ pView->font() }.horizontalAdvance( u' ' ) * 4 );
	pView->setPlainText( szText );
	pView->setAcceptDrops( false ); // dropped files go to the main window, which opens them
	return pView;
}

// The decoded sound's envelope, with the loop point marked.
class CWaveformView : public QWidget
{
public:
	CWaveformView( std::shared_ptr<const xv::audio::CClip> pClip, QWidget *pParent = nullptr )
		: QWidget( pParent )
	{
		setMinimumHeight( 120 );
		setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
		setObjectName( QStringLiteral( "waveform" ) );
		if ( !pClip )
		{
			return;
		}
		// Peaks per bucket; painting picks buckets per pixel column.
		constexpr std::size_t nBUCKETS = 4000;
		const std::uint64_t nFrames = pClip->GetFrames();
		m_Peaks.assign( std::min<std::uint64_t>( nBUCKETS, nFrames ), { 0.0f, 0.0f } );
		for ( std::uint64_t nFrame = 0; nFrame < nFrames; nFrame++ )
		{
			auto &[ low, high ] = m_Peaks[ static_cast<std::size_t>( nFrame * m_Peaks.size() / nFrames ) ];
			for ( std::uint32_t c = 0; c < pClip->m_nChannels; c++ )
			{
				const float flSample = pClip->m_Samples[ nFrame * pClip->m_nChannels + c ];
				low = std::min( low, flSample );
				high = std::max( high, flSample );
			}
		}
		if ( pClip->m_LoopStart )
		{
			m_Loop = static_cast<double>( *pClip->m_LoopStart ) / static_cast<double>( nFrames );
		}
	}

	// Where playback is, 0-1 across the sound; nullopt hides the playhead.
	void SetPlayhead( std::optional<double> fraction )
	{
		if ( fraction != m_Playhead )
		{
			m_Playhead = fraction;
			update();
		}
	}
	// Called with the 0-1 position clicked.
	std::function<void( double )> m_pfnOnSeek;

protected:
	void paintEvent( QPaintEvent * ) override
	{
		QPainter painter{ this };
		painter.fillRect( rect(), palette().base() );
		const int nMiddle = height() / 2;
		painter.setPen( palette().color( QPalette::Mid ) );
		painter.drawLine( 0, nMiddle, width(), nMiddle );
		if ( m_Peaks.empty() )
		{
			return;
		}
		// The part already played is shaded, as in Audacity.
		const int nPlayed = m_Playhead ? static_cast<int>( *m_Playhead * width() ) : -1;
		if ( nPlayed > 0 )
		{
			QColor shade = palette().color( QPalette::Highlight );
			shade.setAlpha( 40 );
			painter.fillRect( 0, 0, nPlayed, height(), shade );
		}
		painter.setPen( palette().color( QPalette::Highlight ) );
		for ( int x = 0; x < width(); x++ )
		{
			const std::size_t nFirst = static_cast<std::size_t>( x ) * m_Peaks.size() / static_cast<std::size_t>( width() );
			const std::size_t nLast = std::max( nFirst + 1, static_cast<std::size_t>( x + 1 ) * m_Peaks.size() / static_cast<std::size_t>( width() ) );
			float flLow = 0;
			float flHigh = 0;
			for ( std::size_t i = nFirst; i < nLast && i < m_Peaks.size(); i++ )
			{
				flLow = std::min( flLow, m_Peaks[ i ].first );
				flHigh = std::max( flHigh, m_Peaks[ i ].second );
			}
			painter.drawLine( x, nMiddle - static_cast<int>( flHigh * nMiddle ), x, nMiddle - static_cast<int>( flLow * nMiddle ) );
		}
		if ( m_Loop )
		{
			const int x = static_cast<int>( *m_Loop * width() );
			painter.setPen( QPen{ QColor{ 230, 140, 20 }, 2 } );
			painter.drawLine( x, 0, x, height() );
			painter.drawText( x + 4, 14, QObject::tr( "loop" ) );
		}
		if ( nPlayed >= 0 )
		{
			painter.setPen( QPen{ palette().color( QPalette::Text ), 2 } );
			painter.drawLine( nPlayed, 0, nPlayed, height() );
		}
	}

	void mousePressEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->button() == Qt::LeftButton && m_pfnOnSeek && !m_Peaks.empty() && width() > 0 )
		{
			m_pfnOnSeek( std::clamp( pEvent->position().x() / width(), 0.0, 1.0 ) );
		}
	}

private:
	std::vector<std::pair<float, float>> m_Peaks;
	std::optional<double> m_Loop;
	std::optional<double> m_Playhead;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FormatName( std::uint16_t nFormat )
{
	switch ( nFormat )
	{
		case 1: return QObject::tr( "PCM" );
		case 2: return QObject::tr( "Microsoft ADPCM" );
		case 3: return QObject::tr( "IEEE float" );
		case 0x11: return QObject::tr( "IMA ADPCM" );
		case 0x55: return QObject::tr( "MP3 in WAV" );
		default: return QObject::tr( "format %1" ).arg( nFormat );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString RoundTripText( bool bIdentical )
{
	return bIdentical ? QObject::tr( "round-trip: byte-identical" ) : QObject::tr( "<b>round-trip: CHANGED</b>" );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CKeyValuesView::CKeyValuesView( const QString &szPath, std::span<const std::byte> bytes, QWidget *pParent )
	: QWidget( pParent )
{
	const std::string szText{ reinterpret_cast<const char *>( bytes.data() ), bytes.size() };
	const auto result = xv::kv1::Parse( szText );
	const bool bIdentical = xv::kv1::Write( result.m_Document ) == szText;
	const auto nErrors = std::ranges::count_if( result.m_Diagnostics, []( const auto &d ) {
		return d.m_nSeverity == xv::kv1::Diagnostic_t::Severity_t::SEVERITY_ERROR;
	} );
	const auto nWarnings = static_cast<qsizetype>( result.m_Diagnostics.size() ) - nErrors;

	auto *pSummary = new QLabel( tr( "<b>%1</b> · %2 top-level entries · %3 errors, %4 warnings · %5" )
			.arg( szPath.toHtmlEscaped() )
			.arg( result.m_Document.m_Roots.size() )
			.arg( nErrors )
			.arg( nWarnings )
			.arg( RoundTripText( bIdentical ) ) );

	auto *pEditor = MakeTextView( Qs( szText ) );
	auto *pDiagnostics = new QListWidget;
	for ( const auto &d : result.m_Diagnostics )
	{
		const QString szSeverity = d.m_nSeverity == xv::kv1::Diagnostic_t::Severity_t::SEVERITY_ERROR ? tr( "error" ) : tr( "warning" );
		auto *pItem = new QListWidgetItem( QStringLiteral( "%1:%2  %3: %4" ).arg( d.m_nLine ).arg( d.m_nColumn ).arg( szSeverity, Qs( d.m_szMessage ) ), pDiagnostics );
		pItem->setData( Qt::UserRole, static_cast<int>( d.m_nLine ) );
	}
	connect( pDiagnostics, &QListWidget::itemActivated, pEditor, [ pEditor ]( QListWidgetItem *pItem ) {
		const QTextBlock block = pEditor->document()->findBlockByLineNumber( pItem->data( Qt::UserRole ).toInt() - 1 );
		pEditor->setTextCursor( QTextCursor{ block } );
		pEditor->centerCursor();
		pEditor->setFocus();
	} );
	pDiagnostics->setVisible( !result.m_Diagnostics.empty() );

	auto *pSplitter = new QSplitter( Qt::Vertical );
	pSplitter->addWidget( pEditor );
	pSplitter->addWidget( pDiagnostics );
	pSplitter->setStretchFactor( 0, 4 );
	pSplitter->setStretchFactor( 1, 1 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addWidget( pSummary );
	pLayout->addWidget( pSplitter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDmxView::CDmxView( const QString &szPath, std::span<const std::byte> bytes, QWidget *pParent )
	: QWidget( pParent )
{
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );

	const auto result = xv::dmx::Read( bytes );
	if ( !result.m_Document )
	{
		pLayout->addWidget( new QLabel( tr( "<b>%1</b> · could not read: %2" ).arg( szPath.toHtmlEscaped(), Qs( result.m_szError ).toHtmlEscaped() ) ) );
		pLayout->addStretch();
		return;
	}
	const xv::dmx::Document_t &doc = *result.m_Document;
	const auto written = xv::dmx::WriteBinary( doc );
	const bool bIdentical = written && std::ranges::equal( *written, bytes );

	pLayout->addWidget( new QLabel( tr( "<b>%1</b> · binary %2, format %3 %4 · %5 elements · %6" )
			.arg( szPath.toHtmlEscaped() )
			.arg( doc.m_nEncodingVersion )
			.arg( Qs( doc.m_szFormat ) )
			.arg( doc.m_nFormatVersion )
			.arg( doc.m_Elements.size() )
			.arg( RoundTripText( bIdentical ) ) ) );

	auto *pTree = new QTreeWidget;
	pTree->setHeaderLabels( { tr( "Name" ), tr( "Type" ), tr( "Value" ) } );
	pTree->setUniformRowHeights( true );

	// Each element is expanded once, at its shallowest reference (found breadth-first), so particle
	// systems sit in the root's list rather than under whichever system uses them as a child.
	// Every other reference just points at it.
	struct Home_t
	{
		std::int32_t m_nElement = -1;
		const xv::dmx::Attribute_t *m_pAttribute = nullptr;
		std::size_t m_nPosition = 0;
	};
	std::vector<std::optional<Home_t>> homes( doc.m_Elements.size() );
	std::deque<std::int32_t> queue;
	if ( !doc.m_Elements.empty() )
	{
		homes[ 0 ] = Home_t{};
		queue.push_back( 0 );
	}
	while ( !queue.empty() )
	{
		const std::int32_t nIndex = queue.front();
		queue.pop_front();
		for ( const xv::dmx::Attribute_t &attribute : doc.m_Elements[ static_cast<std::size_t>( nIndex ) ].m_Attributes )
		{
			const auto claim = [ & ]( const xv::dmx::CElementRef &ref, std::size_t nPosition ) {
				if ( ref.m_nIndex >= 0 && !homes[ static_cast<std::size_t>( ref.m_nIndex ) ] )
				{
					homes[ static_cast<std::size_t>( ref.m_nIndex ) ] = Home_t{ nIndex, &attribute, nPosition };
					queue.push_back( ref.m_nIndex );
				}
			};
			if ( const auto *pRef = std::get_if<xv::dmx::CElementRef>( &attribute.m_Value ) )
			{
				claim( *pRef, 0 );
			}
			else if ( const auto *pRefs = std::get_if<std::vector<xv::dmx::CElementRef>>( &attribute.m_Value ) )
			{
				for ( std::size_t i = 0; i < pRefs->size(); i++ )
				{
					claim( ( *pRefs )[ i ], i );
				}
			}
		}
	}
	const auto isHome = [ &homes ]( std::int32_t nIndex, std::int32_t nOwner, const xv::dmx::Attribute_t &attribute, std::size_t nPosition ) {
		const auto &home = homes[ static_cast<std::size_t>( nIndex ) ];
		return home && home->m_nElement == nOwner && home->m_pAttribute == &attribute && home->m_nPosition == nPosition;
	};

	std::function<void( QTreeWidgetItem *, std::int32_t, const QString &, bool )> pfnAddElement;
	pfnAddElement = [ & ]( QTreeWidgetItem *pItem, std::int32_t nIndex, const QString &szLabel, bool bExpand ) {
		const xv::dmx::CElement &element = doc.m_Elements[ static_cast<std::size_t>( nIndex ) ];
		pItem->setText( 0, szLabel.isEmpty() ? Qs( element.m_szName ) : QStringLiteral( "%1: %2" ).arg( szLabel, Qs( element.m_szName ) ) );
		pItem->setText( 1, Qs( element.m_szType ) );
		if ( !bExpand )
		{
			pItem->setText( 2, tr( "→ element #%1" ).arg( nIndex ) );
			return;
		}
		for ( const xv::dmx::Attribute_t &attribute : element.m_Attributes )
		{
			const auto *pRef = std::get_if<xv::dmx::CElementRef>( &attribute.m_Value );
			if ( pRef && pRef->m_nIndex >= 0 )
			{
				pfnAddElement( new QTreeWidgetItem( pItem ), pRef->m_nIndex, Qs( attribute.m_szName ), isHome( pRef->m_nIndex, nIndex, attribute, 0 ) );
				continue;
			}
			auto *pRow = new QTreeWidgetItem( pItem, { Qs( attribute.m_szName ), Qs( xv::dmx::TypeName( attribute.m_nType ) ) } );
			const auto *pRefs = std::get_if<std::vector<xv::dmx::CElementRef>>( &attribute.m_Value );
			if ( !pRefs )
			{
				pRow->setText( 2, Qs( xv::dmx::ValueText( attribute.m_Value ) ) );
				continue;
			}
			pRow->setText( 2, tr( "%n element(s)", nullptr, static_cast<int>( pRefs->size() ) ) );
			for ( std::size_t i = 0; i < pRefs->size(); i++ )
			{
				const QString szPosition = QStringLiteral( "[%1]" ).arg( i );
				const xv::dmx::CElementRef &child = ( *pRefs )[ i ];
				if ( child.m_nIndex >= 0 )
				{
					pfnAddElement( new QTreeWidgetItem( pRow ), child.m_nIndex, szPosition, isHome( child.m_nIndex, nIndex, attribute, i ) );
				}
				else
				{
					new QTreeWidgetItem( pRow, { szPosition, QStringLiteral( "element" ), Qs( xv::dmx::ValueText( xv::dmx::Value_t{ child } ) ) } );
				}
			}
		}
	};
	if ( !doc.m_Elements.empty() )
	{
		pfnAddElement( new QTreeWidgetItem( pTree ), 0, {}, true );
	}
	for ( std::size_t i = 0; i < doc.m_Elements.size(); i++ )
	{
		if ( !homes[ i ] )
		{
			pfnAddElement( new QTreeWidgetItem( pTree ), static_cast<std::int32_t>( i ), tr( "unreferenced" ), true );
		}
	}
	pTree->expandToDepth( 1 );
	pTree->resizeColumnToContents( 0 );
	pTree->resizeColumnToContents( 1 );

	auto *pTabs = new QTabWidget;
	pTabs->addTab( pTree, tr( "Elements" ) );
	pTabs->addTab( MakeTextView( Qs( xv::dmx::WriteKeyValues2( doc ) ) ), tr( "keyvalues2" ) );
	pLayout->addWidget( pTabs );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CAudioView::CAudioView( const QString &szPath, const QString &szSource, std::span<const std::byte> bytes, CSoundPlayer *pPlayer, QWidget *pParent )
	: QWidget( pParent )
{
	std::string szError;
	// Huge files (music) are decoded only when played, not just for the picture.
	const auto pClip = bytes.size() <= 64u * 1024 * 1024 ? xv::audio::Decode( bytes, &szError ) : nullptr;
	const auto info = xv::sound::ReadWavInfo( bytes );

	QStringList details;
	if ( info )
	{
		details << QStringLiteral( "%1, %2-bit, %3 Hz, %4" ).arg( FormatName( info->m_nFormat ) ).arg( info->m_nBitsPerSample ).arg( info->m_nSampleRate ).arg( info->m_nChannels == 1 ? tr( "mono" ) : tr( "%n channel(s)", nullptr, info->m_nChannels ) );
	}
	else if ( pClip )
	{
		details << QStringLiteral( "MP3, %1 Hz, %2" ).arg( pClip->m_nSampleRate ).arg( pClip->m_nChannels == 1 ? tr( "mono" ) : tr( "stereo" ) );
	}
	if ( pClip )
	{
		details << tr( "%1 s" ).arg( pClip->GetSeconds(), 0, 'f', 2 );
		details << ( pClip->m_LoopStart ? tr( "loops from %1 s" ).arg( static_cast<double>( *pClip->m_LoopStart ) / pClip->m_nSampleRate, 0, 'f', 2 )
										: tr( "no loop point: plays once even in playlooping" ) );
	}
	else if ( !szError.empty() )
	{
		details << tr( "can't decode: %1" ).arg( QString::fromStdString( szError ) );
	}

	auto *pLayout = new QVBoxLayout( this );
	auto *pTitle = new QLabel( tr( "<b>%1</b>%2" ).arg( szPath.toHtmlEscaped(), szSource.isEmpty() ? QString{} : tr( " · read from %1" ).arg( szSource.toHtmlEscaped() ) ) );
	pTitle->setWordWrap( true );
	pLayout->addWidget( pTitle );
	pLayout->addWidget( new QLabel( details.join( QStringLiteral( " · " ) ) ) );

	auto *pWaveform = new CWaveformView( pClip );
	if ( pPlayer )
	{
		const auto pData = std::make_shared<std::vector<std::byte>>( bytes.begin(), bytes.end() );
		const double flDuration = pClip ? pClip->GetSeconds() : 0.0; // 0 for files too big to decode up front
		auto *pButtons = new QHBoxLayout;
		auto *pPlay = new QPushButton( tr( "▶ Play" ) );
		auto *pLoop = new QPushButton( tr( "⟳ Play Looped" ) );
		auto *pStop = new QPushButton( tr( "■ Stop" ) );
		auto *pTime = new QLabel;
		pTime->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
		pLoop->setToolTip( tr( "Loop the way playlooping does: from the loop point (or the start) to the end" ) );
		pWaveform->setToolTip( tr( "Click to play from that point" ) );
		pWaveform->setCursor( Qt::PointingHandCursor );

		// The playhead follows the voice this view started, until it ends or another sound replaces it.
		struct Playback_t
		{
			xv::audio::CEngine::Voice_t m_nVoice = 0;
			bool m_bLoop = false;
		};
		const auto pState = std::make_shared<Playback_t>();
		auto *pTimer = new QTimer( this );
		pTimer->setInterval( 30 );
		const auto clock = []( double flSeconds ) {
			return QStringLiteral( "%1:%2" ).arg( static_cast<int>( flSeconds ) / 60 ).arg( std::fmod( flSeconds, 60.0 ), 5, 'f', 2, u'0' );
		};
		const auto showTime = [ pTime, clock, flDuration ]( std::optional<double> at ) {
			const QString szNow = at ? clock( *at ) : QStringLiteral( "–:––.––" );
			pTime->setText( flDuration > 0 ? QStringLiteral( "%1 / %2" ).arg( szNow, clock( flDuration ) ) : szNow );
		};
		showTime( std::nullopt );
		const auto begin = [ pPlayer, szPath, pData, pState, pTimer ]( bool bLooped, double flFrom ) {
			if ( pPlayer->PlayData( szPath, *pData, bLooped, flFrom ) )
			{
				*pState = { pPlayer->GetLastVoice(), bLooped };
				pTimer->start();
			}
		};
		connect( pTimer, &QTimer::timeout, this, [ pPlayer, pState, pTimer, pWaveform, showTime, flDuration ] {
			const auto at = pPlayer->Position( pState->m_nVoice );
			if ( !at )
			{
				pTimer->stop();
				pWaveform->SetPlayhead( std::nullopt );
				showTime( std::nullopt );
				return;
			}
			pWaveform->SetPlayhead( flDuration > 0 ? std::optional{ std::min( *at / flDuration, 1.0 ) } : std::nullopt );
			showTime( *at );
		} );
		connect( pPlay, &QPushButton::clicked, this, [ begin ] { begin( false, 0.0 ); } );
		connect( pLoop, &QPushButton::clicked, this, [ begin ] { begin( true, 0.0 ); } );
		connect( pStop, &QPushButton::clicked, pPlayer, &CSoundPlayer::Stop );
		pWaveform->m_pfnOnSeek = [ begin, pState, flDuration ]( double flFraction ) { begin( pState->m_bLoop, flFraction * flDuration ); };
		pButtons->addWidget( pPlay );
		pButtons->addWidget( pLoop );
		pButtons->addWidget( pStop );
		pButtons->addSpacing( 12 );
		pButtons->addWidget( pTime );
		pButtons->addStretch();
		pLayout->addLayout( pButtons );
	}
	pLayout->addWidget( pWaveform, 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CFileInfoView::CFileInfoView( const QString &szPath, const QString &szSource, std::size_t nSize, QWidget *pParent )
	: QWidget( pParent )
{
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( new QLabel( tr( "<b>%1</b><br>Read from %2<br>%3" )
			.arg( szPath.toHtmlEscaped(), szSource.toHtmlEscaped(), QLocale{}.formattedDataSize( static_cast<qint64>( nSize ) ) ) ) );
	pLayout->addWidget( new QLabel( tr( "No viewer for this file type yet." ) ) );
	pLayout->addStretch();
}

namespace
{

// An image scaled to fit (never enlarged past twice its size), centred, over a checkerboard.
class CFittedImage : public QWidget
{
public:
	explicit CFittedImage( QImage image )
		: m_Image( std::move( image ) )
	{
		setMinimumSize( 64, 64 );
	}

protected:
	void paintEvent( QPaintEvent * ) override
	{
		QPainter painter( this );
		const QSize fitted = m_Image.size().scaled( size().boundedTo( m_Image.size() * 2 ), Qt::KeepAspectRatio );
		const QRect target{ QPoint{ ( width() - fitted.width() ) / 2, ( height() - fitted.height() ) / 2 }, fitted };
		for ( int y = target.top(); y < target.bottom(); y += 8 )
		{
			for ( int x = target.left(); x < target.right(); x += 8 )
			{
				const QRect cell = QRect( x, y, 8, 8 ).intersected( target );
				painter.fillRect( cell, ( ( x - target.left() ) / 8 + ( y - target.top() ) / 8 ) % 2 ? QColor( 200, 200, 200 ) : QColor( 240, 240, 240 ) );
			}
		}
		painter.setRenderHint( QPainter::SmoothPixmapTransform, fitted.width() < m_Image.width() );
		painter.drawImage( target, m_Image );
	}

private:
	QImage m_Image;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CImageView::CImageView( const QString &szPath, const QString &szSource, std::span<const std::byte> bytes, QWidget *pParent )
	: QWidget( pParent )
{
	QString szError;
	const auto image = DecodeImageFile( szPath, bytes, &szError );
	auto *pLayout = new QVBoxLayout( this );
	auto *pTitle = new QLabel( tr( "<b>%1</b>%2" ).arg( szPath.toHtmlEscaped(), szSource.isEmpty() ? QString{} : tr( " · read from %1" ).arg( szSource.toHtmlEscaped() ) ) );
	pTitle->setWordWrap( true );
	pLayout->addWidget( pTitle );
	auto *pDetails = new QLabel( image ? DescribeImageFile( szPath, bytes ) : tr( "can't decode: %1" ).arg( szError ) );
	pDetails->setWordWrap( true );
	pDetails->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pLayout->addWidget( pDetails );
	if ( image )
	{
		pLayout->addWidget( new CFittedImage( ToQImage( *image ) ), 1 );
	}
	else
	{
		pLayout->addStretch();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Picks the view for a path, by extension. player may be null.
//-----------------------------------------------------------------------------
QWidget *CreateFileView( const QString &szPath, const QString &szSource, std::span<const std::byte> bytes, CSoundPlayer *pPlayer, QWidget *pParent, const xv::material::Reader_t &pfnRead )
{
	const QString szSuffix = QFileInfo{ szPath }.suffix().toLower();
	if ( szSuffix == u"vmt" )
	{
		return new CMaterialView( szPath, szSource, bytes, pfnRead, pParent );
	}
	if ( IsImageFile( szPath ) )
	{
		return new CImageView( szPath, szSource, bytes, pParent );
	}
	if ( szSuffix == u"wav" || szSuffix == u"mp3" )
	{
		return new CAudioView( szPath, szSource, bytes, pPlayer, pParent );
	}
	if ( szSuffix == u"pcf" || szSuffix == u"dmx" )
	{
		return new CDmxView( szPath, bytes, pParent );
	}
	if ( szSuffix == u"txt" || szSuffix == u"vbsp" || szSuffix == u"vdf" || szSuffix == u"res" )
	{
		return new CKeyValuesView( szPath, bytes, pParent );
	}
	return new CFileInfoView( szPath, szSource, bytes.size(), pParent );
}
