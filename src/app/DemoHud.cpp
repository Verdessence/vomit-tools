//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoHud.h.
//
//===========================================================================//
#include "DemoHud.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

#include "DemoWorld.h"
#include "hud/Hud.h"
#include "kv1/Kv1.h"

using namespace xv::demo;

namespace
{

// How long the chat keeps a line and how many it shows (hud_saytext_time's 12 seconds).
constexpr double CHAT_SECONDS = 12.0;
constexpr int CHAT_LINES = 6;
// The kill feed's (Counter-Strike: Source keeps a notice six seconds, five at most).
constexpr double KILL_SECONDS = 6.0;
constexpr int KILL_LINES = 5;
// VGUI's proportional layouts are made for a screen this tall.
constexpr double BASE_TALL = 480.0;

//-----------------------------------------------------------------------------
// Purpose: A file's text: UTF-16 with its mark (the language files), else as it is.
//-----------------------------------------------------------------------------
std::string TextOf( const std::vector<std::byte> &bytes )
{
	const auto *pData = reinterpret_cast<const char *>( bytes.data() );
	if ( bytes.size() >= 2 && static_cast<unsigned char>( pData[ 0 ] ) == 0xFF && static_cast<unsigned char>( pData[ 1 ] ) == 0xFE )
		return QString::fromUtf16( reinterpret_cast<const char16_t *>( pData + 2 ), static_cast<qsizetype>( ( bytes.size() - 2 ) / 2 ) ).toStdString();
	return std::string( pData, bytes.size() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QColor ColorOf( std::uint32_t nRgba )
{
	return QColor( static_cast<int>( nRgba >> 24 ), static_cast<int>( ( nRgba >> 16 ) & 0xff ), static_cast<int>( ( nRgba >> 8 ) & 0xff ), static_cast<int>( nRgba & 0xff ) );
}

//-----------------------------------------------------------------------------
// Purpose: The chat's colour for a code's run.
//-----------------------------------------------------------------------------
QColor RunColor( const ColorRun_t &run, const QColor &team, const QColor &normal )
{
	switch ( run.m_nColor )
	{
		case TextColor_t::TEXT_COLOR_TEAM:
			return team;
		case TextColor_t::TEXT_COLOR_GREEN:
			return QColor( 153, 255, 153 );
		case TextColor_t::TEXT_COLOR_OLIVE:
			return QColor( 200, 220, 120 );
		case TextColor_t::TEXT_COLOR_RGB:
			return ColorOf( run.m_nRgba | 0xff );
		default:
			return normal;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A string of the demo's as Qt text.
//-----------------------------------------------------------------------------
QString Utf8( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Text with a dark edge under it, as the HUD's text reads over any picture.
//-----------------------------------------------------------------------------
void ShadowedText( QPainter &painter, QPointF at, const QString &szText, const QColor &color )
{
	painter.setPen( QColor( 0, 0, 0, color.alpha() * 3 / 4 ) );
	painter.drawText( at + QPointF( 1, 1 ), szText );
	painter.setPen( color );
	painter.drawText( at, szText );
}

//-----------------------------------------------------------------------------
// Purpose: Coloured runs one after the other from a point; returns where they end.
//-----------------------------------------------------------------------------
double RunsAt( QPainter &painter, QPointF at, const std::vector<ColorRun_t> &runs, const QColor &team, const QColor &normal )
{
	for ( const ColorRun_t &run : runs )
	{
		const QString szText = Utf8( run.m_szText );
		ShadowedText( painter, at, szText, RunColor( run, team, normal ) );
		at.rx() += painter.fontMetrics().horizontalAdvance( szText );
	}
	return at.x();
}

//-----------------------------------------------------------------------------
// Purpose: A dark rounded box behind a block of text.
//-----------------------------------------------------------------------------
void Backing( QPainter &painter, const QRectF &box, double flScale, int nAlpha = 150 )
{
	painter.save();
	painter.setRenderHint( QPainter::Antialiasing, true );
	painter.setPen( Qt::NoPen );
	painter.setBrush( QColor( 0, 0, 0, nAlpha ) );
	painter.drawRoundedRect( box, 4 * flScale, 4 * flScale );
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: "1:05" for seconds left.
//-----------------------------------------------------------------------------
std::string Clock( double flSeconds )
{
	const int nSeconds = std::max( 0, static_cast<int>( std::ceil( flSeconds ) ) );
	char szText[ 32 ];
	std::snprintf( szText, sizeof( szText ), "%d:%02d", nSeconds / 60, nSeconds % 60 );
	return szText;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: -1 is the middle, below 0 back from the right or bottom edge, else that
//          fraction across; a centred block centres each line.
//-----------------------------------------------------------------------------
void DemoPaintHudText( QPainter &painter, const QRect &rect, const HudText_t &text, double flSeconds, const QFont &font )
{
	painter.setFont( font );
	const QFontMetricsF metrics( font );
	const QStringList lines = Utf8( text.m_szText ).split( u'\n' );
	double flWide = 0.0;
	for ( const QString &szLine : lines )
		flWide = std::max( flWide, metrics.horizontalAdvance( szLine ) );
	const double flTall = metrics.height() * static_cast<double>( lines.size() );
	const auto place = []( float flAt, double flLength, double flSize ) {
		if ( flAt == -1.0f )
			return ( flLength - flSize ) / 2.0;
		if ( flAt < 0.0f )
			return ( 1.0 + flAt ) * flLength - flSize;
		return static_cast<double>( flAt ) * flLength;
	};
	double flY = rect.top() + place( text.m_flY, rect.height(), flTall );
	int nLetter = 0;
	for ( const QString &szLine : lines )
	{
		double flX = rect.left() + place( text.m_flX, rect.width(), flWide );
		if ( text.m_flX == -1.0f )
			flX = rect.left() + ( rect.width() - metrics.horizontalAdvance( szLine ) ) / 2.0;
		for ( const QChar ch : szLine )
		{
			const std::uint32_t nColor = HudLetterColor( text, nLetter++, flSeconds );
			if ( ( nColor & 0xff ) != 0 )
				ShadowedText( painter, QPointF( flX, flY + metrics.ascent() ), QString( ch ), ColorOf( nColor ) );
			flX += metrics.horizontalAdvance( ch );
		}
		nLetter++; // the line break counts as a letter
		flY += metrics.height();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Its height as a scheme's "tall" is drawn on this system (four fifths of it).
//-----------------------------------------------------------------------------
QFont DemoHudTextFont( int nTall )
{
	QFont font( QStringLiteral( "Trebuchet MS" ) );
	font.setWeight( QFont::Black );
	font.setPixelSize( std::max( 6, static_cast<int>( std::lround( 24.0 * nTall / BASE_TALL * 0.8 ) ) ) );
	return font;
}

//-----------------------------------------------------------------------------
// Purpose: Each run in its colour, the text escaped, line breaks kept.
//-----------------------------------------------------------------------------
QString DemoColoredHtml( std::string_view szText, const QColor &team, const QColor &normal, const QColor &background )
{
	QString szHtml;
	for ( const ColorRun_t &run : SplitColors( szText ) )
	{
		const QString szEscaped = Utf8( run.m_szText ).toHtmlEscaped().replace( u'\n', QStringLiteral( "<br>" ) );
		szHtml += QStringLiteral( "<span style=\"%1\">%2</span>" ).arg( DemoReadableStyle( RunColor( run, team, normal ), background ), szEscaped );
	}
	return szHtml;
}

//-----------------------------------------------------------------------------
// Purpose: The WCAG contrast between the text and the background; under 3:1 (the least for
//          large or bold text) the text gets a backing it reads against, near black for pale
//          text and near white for dark.
//-----------------------------------------------------------------------------
QString DemoReadableStyle( const QColor &color, const QColor &background )
{
	const QString szColor = QStringLiteral( "color:%1" ).arg( color.name() );
	if ( !background.isValid() )
		return szColor;
	const auto luminance = []( const QColor &c ) {
		const auto channel = []( double fl ) { return fl <= 0.04045 ? fl / 12.92 : std::pow( ( fl + 0.055 ) / 1.055, 2.4 ); };
		return 0.2126 * channel( c.redF() ) + 0.7152 * channel( c.greenF() ) + 0.0722 * channel( c.blueF() );
	};
	const auto contrast = [ & ]( const QColor &a, const QColor &b ) {
		const double flA = luminance( a ), flB = luminance( b );
		return ( std::max( flA, flB ) + 0.05 ) / ( std::min( flA, flB ) + 0.05 );
	};
	constexpr double READABLE = 3.0;
	if ( contrast( color, background ) >= READABLE )
		return szColor;
	const QColor dark( 24, 24, 28 ), light( 242, 242, 245 );
	const QColor backing = contrast( color, dark ) >= contrast( color, light ) ? dark : light;
	return szColor + QStringLiteral( ";background-color:%1" ).arg( backing.name() );
}

//-----------------------------------------------------------------------------
// Purpose: Links are ticks to go to, not pages.
//-----------------------------------------------------------------------------
CDemoLog::CDemoLog( QWidget *pParent )
	: QTextBrowser( pParent )
{
	setOpenLinks( false );
	setOpenExternalLinks( false );
	connect( this, &QTextBrowser::anchorClicked, this, [ this ]( const QUrl &url ) {
		if ( url.scheme() == QStringLiteral( "tick" ) )
			emit TickPicked( url.path().toInt() );
		else if ( url.scheme() == QStringLiteral( "item" ) )
			emit ItemPicked( url.path().toInt() );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoLog::SetEntries( std::vector<Entry_t> entries, std::function<QString( int )> pfnTime )
{
	m_Entries = std::move( entries );
	m_pfnTime = std::move( pfnTime );
	m_nShown = -1;
}

//-----------------------------------------------------------------------------
// Purpose: The time as a link to its tick, then the line.
//-----------------------------------------------------------------------------
QString CDemoLog::LineHtml( const Entry_t &entry ) const
{
	const QString szTime = m_pfnTime ? m_pfnTime( entry.m_nTick ) : QString::number( entry.m_nTick );
	return QStringLiteral( "<a href=\"tick:%1\" style=\"color:gray;text-decoration:none\">%2</a>&nbsp; %3" ).arg( entry.m_nTick ).arg( szTime, entry.m_szHtml );
}

//-----------------------------------------------------------------------------
// Purpose: Moving on adds the new lines; going back writes the log again.
//-----------------------------------------------------------------------------
void CDemoLog::ShowUpTo( int nTick )
{
	const int nCount = static_cast<int>( CFeed::CountUpTo( m_Entries, nTick ) );
	if ( nCount == m_nShown )
		return;
	QScrollBar *pBar = verticalScrollBar();
	const bool bAtEnd = pBar->value() >= pBar->maximum() - 4 || m_nShown < 0;
	const int nKeep = pBar->value();
	if ( nCount < m_nShown || m_nShown < 0 )
	{
		QStringList lines;
		lines.reserve( nCount );
		for ( int i = 0; i < nCount; i++ )
			lines << LineHtml( m_Entries[ static_cast<std::size_t>( i ) ] );
		setHtml( lines.join( QStringLiteral( "<br>" ) ) );
	}
	else
	{
		for ( int i = m_nShown; i < nCount; i++ )
			append( LineHtml( m_Entries[ static_cast<std::size_t>( i ) ] ) );
	}
	m_nShown = nCount;
	pBar->setValue( bAtEnd ? pBar->maximum() : nKeep );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoHud::CDemoHud() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoHud::~CDemoHud() = default;

//-----------------------------------------------------------------------------
// Purpose: The language's tokens, and the game's HUD layout with its scheme, read now.
//-----------------------------------------------------------------------------
void CDemoHud::SetFiles( CHudEditor::ReadFile_t pfnRead )
{
	m_pfnRead = std::move( pfnRead );
	m_Tokens.clear();
	m_pLayout.reset();
	m_pScoreboard.reset();
	m_ScreenFor = {};
	m_ScoreboardFor = {};
	m_ScoreboardVariables.clear();
	if ( !m_pfnRead )
		return;
	for ( const char *pszFile : { "resource/gameui_english.txt", "resource/valve_english.txt", "resource/cstrike_english.txt" } )
	{
		if ( const auto bytes = m_pfnRead( pszFile ) )
		{
			for ( auto &[ szToken, szValue ] : xv::hud::ReadTokens( xv::kv1::Parse( TextOf( *bytes ) ).m_Document ) )
				m_Tokens[ szToken ] = std::move( szValue );
		}
	}
	std::optional<xv::hud::CScheme> scheme;
	if ( const auto schemeFile = m_pfnRead( "resource/clientscheme.res" ) )
		scheme = xv::hud::CScheme::FromDocument( xv::kv1::Parse( TextOf( *schemeFile ) ).m_Document );
	const auto open = [ & ]( const char *pszFile, const QString &szName ) -> std::unique_ptr<CHudEditor> {
		const auto text = m_pfnRead( pszFile );
		if ( !text )
			return nullptr;
		auto pEditor = std::make_unique<CHudEditor>( szName, TextOf( *text ), std::filesystem::path{}, true );
		pEditor->setAttribute( Qt::WA_DontShowOnScreen, true );
		if ( scheme )
			pEditor->SetScheme( *scheme );
		pEditor->SetFiles( m_pfnRead );
		pEditor->SetRendering( true );
		return pEditor;
	};
	m_pLayout = open( "scripts/hudlayout.res", QStringLiteral( "HudLayout.res" ) );
	m_pScoreboard = open( "resource/ui/scoreboard.res", QStringLiteral( "ScoreBoard.res" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string> CDemoHud::Localize( std::string_view szToken ) const
{
	const auto found = m_Tokens.find( Utf8( szToken ).toLower().toStdString() );
	return found == m_Tokens.end() ? std::nullopt : std::optional<std::string>( found->second );
}

//-----------------------------------------------------------------------------
// Purpose: The first of the scheme's fonts it has, at this screen; else the system's at the
//          size the HUD's small text has.
//-----------------------------------------------------------------------------
QFont CDemoHud::FontFor( std::initializer_list<const char *> names, double flScale ) const
{
	if ( m_pLayout )
	{
		for ( const char *pszName : names )
		{
			if ( m_pLayout->GetScheme().ResolveFont( pszName, m_pLayout->GetScreen() ) )
				return m_pLayout->FontFor( pszName, 1.0 );
		}
	}
	QFont font;
	font.setPixelSize( std::max( 8, static_cast<int>( 9.0 * flScale ) ) );
	font.setBold( true );
	return font;
}

//-----------------------------------------------------------------------------
// Purpose: The fade, then the game's HUD elements (or plain numbers), then the messages
//          over them.
//-----------------------------------------------------------------------------
void CDemoHud::Paint( QPainter &painter, const QRect &rect, const CFeed &feed, const DemoHudMoment_t &moment, const std::function<QString( int )> &pfnName, const std::function<int( int )> &pfnTeam )
{
	if ( rect.isEmpty() )
		return;
	const double flScale = rect.height() / BASE_TALL;
	painter.save();
	painter.setClipRect( rect );
	// A fade covers the world, not the HUD: the game draws it first.
	if ( moment.m_bOwnMessages )
	{
		if ( const std::uint32_t nFade = feed.FadeAt( moment.m_flTick ); ( nFade & 0xff ) != 0 )
			painter.fillRect( rect, ColorOf( nFade ) );
	}
	if ( moment.m_nPlayer != 0 && moment.m_bAlive )
	{
		if ( m_pLayout )
		{
			if ( m_ScreenFor != rect.size() )
			{
				m_pLayout->SetScreen( { rect.width(), rect.height(), true } );
				m_ScreenFor = rect.size();
			}
			std::map<std::string, CHudEditor::LiveValues_t> live;
			live[ "hudhealth" ] = { "health_icon", std::to_string( std::max( 0, moment.m_nHealth ) ), {}, {}, {} };
			if ( moment.m_nArmor )
				live[ "hudarmor" ] = { "shield_bright", std::to_string( *moment.m_nArmor ), {}, {}, {} };
			if ( moment.m_nMoney )
				live[ "hudaccount" ] = { "dollar_sign", std::to_string( *moment.m_nMoney ), {}, {}, {} };
			if ( moment.m_flRoundLeft )
				live[ "hudroundtimer" ] = { "timer_icon", Clock( *moment.m_flRoundLeft ), {}, {}, {} };
			if ( moment.m_nClip )
				live[ "hudammo" ] = { moment.m_szAmmoIcon, std::to_string( *moment.m_nClip ), {}, moment.m_nReserve ? std::to_string( *moment.m_nReserve ) : std::string{}, {} };
			if ( moment.m_bCrosshair )
				live[ "hudcrosshair" ] = {};
			m_pLayout->SetLive( std::move( live ) );
			m_pLayout->PaintScreen( painter, QRectF( rect ) );
		}
		else
		{
			PaintPlainHud( painter, rect, moment, flScale );
		}
	}
	else if ( moment.m_flRoundLeft )
	{
		// Dead or watching: the round's clock alone, as the spectator's bar has it.
		painter.setFont( FontFor( { "HudNumbersSmall", "Default" }, flScale ) );
		const QString szClock = Utf8( Clock( *moment.m_flRoundLeft ) );
		ShadowedText( painter, QPointF( rect.center().x() - painter.fontMetrics().horizontalAdvance( szClock ) / 2.0, rect.top() + 20 * flScale ), szClock, QColor( 255, 220, 0, 220 ) );
	}
	if ( moment.m_bOwnMessages )
	{
		PaintHudTexts( painter, rect, feed, moment.m_flTick, flScale );
		PaintBoxes( painter, rect, feed, moment, flScale );
	}
	PaintChat( painter, rect, feed, moment, flScale, pfnTeam );
	PaintKills( painter, rect, feed, moment.m_flTick, flScale, pfnName, pfnTeam );

	// Who is talking: a column of names at the right, a speaker mark before each.
	painter.setFont( FontFor( { "DefaultSmall", "Default" }, flScale ) );
	double flY = rect.top() + rect.height() * 0.55;
	for ( const int nEntity : moment.m_Speakers )
	{
		const QString szName = pfnName( nEntity );
		const double flWide = painter.fontMetrics().horizontalAdvance( szName ) + 22 * flScale;
		const QRectF box( rect.right() - flWide - 10 * flScale, flY, flWide, painter.fontMetrics().height() + 4 * flScale );
		Backing( painter, box, flScale, 120 );
		painter.save();
		painter.setRenderHint( QPainter::Antialiasing, true );
		painter.setPen( Qt::NoPen );
		painter.setBrush( QColor( 255, 255, 255, 220 ) );
		const double flMid = box.center().y();
		const double flU = 3.0 * flScale;
		QPolygonF speaker;
		speaker << QPointF( box.left() + flU, flMid - flU * 0.6 ) << QPointF( box.left() + flU * 2, flMid - flU * 0.6 ) << QPointF( box.left() + flU * 3.5, flMid - flU * 1.6 ) << QPointF( box.left() + flU * 3.5, flMid + flU * 1.6 ) << QPointF( box.left() + flU * 2, flMid + flU * 0.6 ) << QPointF( box.left() + flU, flMid + flU * 0.6 );
		painter.drawPolygon( speaker );
		if ( std::ranges::find( moment.m_MutedSpeakers, nEntity ) != moment.m_MutedSpeakers.end() )
		{
			const double flX = box.left() + flU * 4;
			painter.setPen( QPen( QColor( 255, 130, 130 ), 1.3 * flScale ) );
			painter.drawLine( QPointF( flX, flMid - flU * 0.65 ), QPointF( flX + flU * 1.3, flMid + flU * 0.65 ) );
			painter.drawLine( QPointF( flX + flU * 1.3, flMid - flU * 0.65 ), QPointF( flX, flMid + flU * 0.65 ) );
		}
		painter.restore();
		const int nTeam = pfnTeam( nEntity );
		ShadowedText( painter, QPointF( box.left() + 18 * flScale, box.top() + 2 * flScale + painter.fontMetrics().ascent() ), szName, nTeam >= 2 ? DemoTeamColor( nTeam ) : QColor( 230, 230, 230 ) );
		flY += box.height() + 2 * flScale;
	}

	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: The numbers alone, where Counter-Strike puts them, without the game's files.
//-----------------------------------------------------------------------------
void CDemoHud::PaintPlainHud( QPainter &painter, const QRect &rect, const DemoHudMoment_t &moment, double flScale ) const
{
	QFont font = painter.font();
	font.setPixelSize( std::max( 10, static_cast<int>( 20 * flScale ) ) );
	font.setBold( true );
	painter.setFont( font );
	const QColor amber( 255, 176, 0, 220 );
	const double flBase = rect.bottom() - 12 * flScale;
	QString szLeft = QStringLiteral( "+ %1" ).arg( std::max( 0, moment.m_nHealth ) );
	if ( moment.m_nArmor )
		szLeft += QStringLiteral( "    ◈ %1" ).arg( *moment.m_nArmor );
	ShadowedText( painter, QPointF( rect.left() + 12 * flScale, flBase ), szLeft, amber );
	if ( moment.m_flRoundLeft )
	{
		const QString szClock = Utf8( Clock( *moment.m_flRoundLeft ) );
		ShadowedText( painter, QPointF( rect.center().x() - painter.fontMetrics().horizontalAdvance( szClock ) / 2.0, flBase ), szClock, amber );
	}
	if ( moment.m_nClip )
	{
		const QString szAmmo = moment.m_nReserve ? QStringLiteral( "%1 | %2" ).arg( *moment.m_nClip ).arg( *moment.m_nReserve ) : QString::number( *moment.m_nClip );
		ShadowedText( painter, QPointF( rect.right() - painter.fontMetrics().horizontalAdvance( szAmmo ) - 12 * flScale, flBase ), szAmmo, amber );
	}
	if ( moment.m_nMoney )
	{
		const QString szMoney = QStringLiteral( "$ %1" ).arg( *moment.m_nMoney );
		ShadowedText( painter, QPointF( rect.right() - painter.fontMetrics().horizontalAdvance( szMoney ) - 12 * flScale, flBase - painter.fontMetrics().height() * 1.2 ), szMoney, amber );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Each channel's message where its x and y put it: -1 in the middle, below 0 back
//          from the right or bottom edge, else that fraction across; each letter in its
//          colour for the moment.
//-----------------------------------------------------------------------------
// Purpose: Each channel's message.
//-----------------------------------------------------------------------------
void CDemoHud::PaintHudTexts( QPainter &painter, const QRect &rect, const CFeed &feed, double flTick, double flScale ) const
{
	const QFont font = m_pLayout ? FontFor( { "Trebuchet24", "Default" }, flScale ) : DemoHudTextFont( rect.height() );
	for ( const CFeed::ShownText_t &shown : feed.HudTextsAt( flTick ) )
	{
		if ( shown.m_pText )
			DemoPaintHudText( painter, rect, *shown.m_pText, shown.m_flSeconds, font );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The last few chat lines of the last seconds, low at the left, over a faint box.
//-----------------------------------------------------------------------------
void CDemoHud::PaintChat( QPainter &painter, const QRect &rect, const CFeed &feed, const DemoHudMoment_t &moment, double flScale, const std::function<int( int )> &pfnTeam ) const
{
	std::vector<const FeedLine_t *> lines;
	const std::vector<FeedLine_t> &all = feed.GetLines();
	for ( std::size_t i = CFeed::CountUpTo( all, static_cast<int>( moment.m_flTick ) ); i-- > 0 && static_cast<int>( lines.size() ) < CHAT_LINES; )
	{
		const FeedLine_t &line = all[ i ];
		if ( ( moment.m_flTick - line.m_nTick ) * feed.GetTickInterval() > CHAT_SECONDS )
			break;
		if ( line.m_nKind == FeedKind_t::FEED_KIND_CHAT || line.m_nKind == FeedKind_t::FEED_KIND_RADIO || ( line.m_nKind == FeedKind_t::FEED_KIND_NOTICE && moment.m_bOwnMessages ) )
			lines.push_back( &line );
	}
	if ( lines.empty() )
		return;
	painter.setFont( FontFor( { "ChatFont", "Default" }, flScale ) );
	const double flLine = painter.fontMetrics().height();
	double flY = rect.top() + rect.height() * 0.78 - flLine * static_cast<double>( lines.size() );
	for ( auto line = lines.rbegin(); line != lines.rend(); ++line )
	{
		const int nTeam = ( *line )->m_nEntity ? pfnTeam( ( *line )->m_nEntity ) : 0;
		const QColor team = nTeam >= 2 ? DemoTeamColor( nTeam ) : QColor( 200, 200, 200 );
		const QColor normal = ( *line )->m_nKind == FeedKind_t::FEED_KIND_RADIO ? QColor( 153, 255, 153 ) : QColor( 255, 235, 200 );
		RunsAt( painter, QPointF( rect.left() + 10 * flScale, flY + painter.fontMetrics().ascent() ), SplitColors( ( *line )->m_szText ), team, normal );
		flY += flLine;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The last few deaths at the top right: the killer, the weapon's icon from the
//          HUD's textures (its name when there is none), a headshot's mark, the victim.
//-----------------------------------------------------------------------------
void CDemoHud::PaintKills( QPainter &painter, const QRect &rect, const CFeed &feed, double flTick, double flScale, const std::function<QString( int )> &pfnName, const std::function<int( int )> &pfnTeam ) const
{
	const std::vector<Kill_t> &kills = feed.GetKills();
	std::vector<const Kill_t *> shown;
	for ( std::size_t i = CFeed::CountUpTo( kills, static_cast<int>( flTick ) ); i-- > 0 && static_cast<int>( shown.size() ) < KILL_LINES; )
	{
		if ( ( flTick - kills[ i ].m_nTick ) * feed.GetTickInterval() > KILL_SECONDS )
			break;
		shown.push_back( &kills[ i ] );
	}
	const QFont nameFont = FontFor( { "ChatFont", "Default" }, flScale );
	double flY = rect.top() + 8 * flScale;
	for ( auto pKill = shown.rbegin(); pKill != shown.rend(); ++pKill )
	{
		const Kill_t &kill = **pKill;
		const auto iconFor = [ & ]( const std::string &szName ) -> std::pair<QFont, QString> {
			if ( m_pLayout )
			{
				if ( const xv::hud::Icon_t *pIcon = m_pLayout->IconFor( szName ); pIcon && !pIcon->m_szFont.empty() )
					return { m_pLayout->FontFor( pIcon->m_szFont, 1.0 ), Utf8( pIcon->m_szCharacter ) };
			}
			return { nameFont, {} };
		};
		auto [ weaponFont, szWeapon ] = iconFor( "d_" + kill.m_szWeapon );
		if ( szWeapon.isEmpty() )
			szWeapon = QStringLiteral( " [%1] " ).arg( Utf8( kill.m_szWeapon ) );
		auto [ headFont, szHead ] = kill.m_bHeadshot ? iconFor( "d_headshot" ) : std::pair<QFont, QString>{ nameFont, {} };
		if ( kill.m_bHeadshot && szHead.isEmpty() )
			szHead = QStringLiteral( "(HS) " );
		const QString szKiller = kill.m_nAttacker && kill.m_nAttacker != kill.m_nVictim ? pfnName( kill.m_nAttacker ) : QString{};
		const QString szVictim = pfnName( kill.m_nVictim );
		const QFontMetricsF nameMetrics( nameFont ), weaponMetrics( weaponFont ), headMetrics( headFont );
		const double flGap = 6 * flScale;
		const double flWide = nameMetrics.horizontalAdvance( szKiller ) + weaponMetrics.horizontalAdvance( szWeapon ) + headMetrics.horizontalAdvance( szHead ) + nameMetrics.horizontalAdvance( szVictim ) + flGap * 4;
		const double flTall = std::max( { nameMetrics.height(), weaponMetrics.height(), headMetrics.height() } ) + 2 * flScale;
		const QRectF box( rect.right() - flWide - 10 * flScale, flY, flWide, flTall );
		Backing( painter, box, flScale, 140 );
		double flX = box.left() + flGap;
		const double flBase = box.top() + ( flTall + nameMetrics.ascent() - nameMetrics.descent() ) / 2.0;
		const auto put = [ & ]( const QFont &font, const QString &szText, const QColor &color ) {
			if ( szText.isEmpty() )
				return;
			painter.setFont( font );
			ShadowedText( painter, QPointF( flX, flBase ), szText, color );
			flX += QFontMetricsF( font ).horizontalAdvance( szText ) + flGap;
		};
		const auto teamColor = [ & ]( int nEntity ) {
			const int nTeam = nEntity ? pfnTeam( nEntity ) : 0;
			return nTeam >= 2 ? DemoTeamColor( nTeam ) : QColor( 230, 230, 230 );
		};
		put( nameFont, szKiller, teamColor( kill.m_nAttacker ) );
		put( weaponFont, szWeapon, QColor( 255, 255, 255 ) );
		put( headFont, szHead, QColor( 255, 255, 255 ) );
		put( nameFont, szVictim, teamColor( kill.m_nVictim ) );
		flY += flTall + 2 * flScale;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The hint low in the middle, the key hint at the right, centre text, the server's
//          dialog line at the top left and the menu at the left, each while it lasts.
//-----------------------------------------------------------------------------
void CDemoHud::PaintBoxes( QPainter &painter, const QRect &rect, const CFeed &feed, const DemoHudMoment_t &moment, double flScale ) const
{
	const double flTick = moment.m_flTick;
	const QColor normal( 255, 235, 200 );
	const auto block = [ & ]( const QString &szText, const QFont &font, const std::function<QPointF( QSizeF )> &pfnPlace, bool bBox, const QColor &color ) {
		painter.setFont( font );
		const QFontMetricsF metrics( font );
		const QStringList lines = szText.split( u'\n' );
		double flWide = 0.0;
		for ( const QString &szLine : lines )
			flWide = std::max( flWide, metrics.horizontalAdvance( Utf8( StripColors( szLine.toStdString() ) ) ) );
		const QSizeF size( flWide + 16 * flScale, metrics.height() * static_cast<double>( lines.size() ) + 10 * flScale );
		const QPointF at = pfnPlace( size );
		if ( bBox )
			Backing( painter, QRectF( at, size ), flScale );
		double flY = at.y() + 5 * flScale + metrics.ascent();
		for ( const QString &szLine : lines )
		{
			// Menus grey what can't be picked ("->1." is SourceMod's mark for it).
			const bool bGrey = szLine.startsWith( QStringLiteral( "->" ) );
			RunsAt( painter, QPointF( at.x() + 8 * flScale, flY ), SplitColors( szLine.toStdString() ), color, bGrey ? QColor( 150, 150, 150 ) : color );
			flY += metrics.height();
		}
	};
	const QFont small = FontFor( { "HudHintText", "ChatFont", "Default" }, flScale );
	if ( const FeedLine_t *pHint = feed.LineAt( FeedKind_t::FEED_KIND_HINT, flTick ) )
		block( Utf8( pHint->m_szText ), small, [ & ]( QSizeF size ) { return QPointF( rect.center().x() - size.width() / 2.0, rect.top() + rect.height() * 0.62 ); }, true, normal );
	if ( const FeedLine_t *pKey = feed.LineAt( FeedKind_t::FEED_KIND_KEY_HINT, flTick ) )
		block( Utf8( pKey->m_szText ).trimmed(), small, [ & ]( QSizeF size ) { return QPointF( rect.right() - size.width() - 10 * flScale, rect.top() + rect.height() * 0.3 ); }, true, normal );
	if ( const FeedLine_t *pCenter = feed.LineAt( FeedKind_t::FEED_KIND_CENTER, flTick ) )
		block( Utf8( pCenter->m_szText ), FontFor( { "CenterPrintText", "Default" }, flScale ), [ & ]( QSizeF size ) { return QPointF( rect.center().x() - size.width() / 2.0, rect.top() + rect.height() * 0.3 ); }, false, normal );
	if ( const FeedLine_t *pDialog = feed.LineAt( FeedKind_t::FEED_KIND_DIALOG, flTick ) )
		block( Utf8( pDialog->m_szText ), small, [ & ]( QSizeF ) { return QPointF( rect.left() + 4 * flScale, rect.top() + 4 * flScale ); }, false, ColorOf( pDialog->m_nRgba | 0xff ) );
	if ( const MenuShown_t *pMenu = feed.MenuAt( flTick ) )
		block( Utf8( pMenu->m_szText ).trimmed(), FontFor( { "HudSelectionText", "Default" }, flScale ), [ & ]( QSizeF size ) { return QPointF( rect.left() + 10 * flScale, rect.center().y() - size.height() / 2.0 ); }, true, QColor( 255, 255, 255 ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoHud::SetScoreboardVariable( const char *pszName, const QString &szValue )
{
	// An empty value would take the variable out, leaving "%name%" written: a space instead.
	const QString szShown = szValue.isEmpty() ? QStringLiteral( " " ) : szValue;
	QString &szWas = m_ScoreboardVariables[ pszName ];
	if ( szWas == szShown )
		return;
	szWas = szShown;
	m_pScoreboard->SetVariable( QString::fromLatin1( pszName ), szShown );
}

//-----------------------------------------------------------------------------
// Purpose: The file's elements with its variables filled in (the map, the teams' names,
//          scores and how many are alive, the spectators), then each team's rows in the cells
//          its first row's elements give, down by that row's height while the team's area has
//          room; the server's name in its label, which the game fills itself.
//-----------------------------------------------------------------------------
void CDemoHud::PaintScoreboard( QPainter &painter, const QRect &rect, const DemoScoreboard_t &board )
{
	if ( rect.isEmpty() )
		return;
	const double flScale = rect.height() / BASE_TALL;
	// Each team's players by score, then fewer deaths, then name.
	std::array<std::vector<const DemoScoreRow_t *>, 4> teams;
	QStringList spectators;
	for ( const DemoScoreRow_t &row : board.m_Rows )
	{
		if ( row.m_nTeam >= 2 && row.m_nTeam <= 3 )
			teams[ static_cast<std::size_t>( row.m_nTeam ) ].push_back( &row );
		else
			spectators << row.m_szName;
	}
	for ( auto &team : teams )
	{
		std::ranges::sort( team, []( const DemoScoreRow_t *a, const DemoScoreRow_t *b ) {
			if ( a->m_nScore.value_or( 0 ) != b->m_nScore.value_or( 0 ) )
				return a->m_nScore.value_or( 0 ) > b->m_nScore.value_or( 0 );
			if ( a->m_nDeaths.value_or( 0 ) != b->m_nDeaths.value_or( 0 ) )
				return a->m_nDeaths.value_or( 0 ) < b->m_nDeaths.value_or( 0 );
			return a->m_szName.localeAwareCompare( b->m_szName ) < 0;
		} );
	}
	if ( !m_pScoreboard )
	{
		PaintPlainScoreboard( painter, rect, board, flScale );
		return;
	}
	painter.save();
	painter.setClipRect( rect );
	if ( m_ScoreboardFor != rect.size() )
	{
		m_pScoreboard->SetScreen( { rect.width(), rect.height(), true } );
		m_ScoreboardFor = rect.size();
	}
	const auto alive = [ & ]( int nTeam ) {
		return static_cast<int>( std::ranges::count_if( teams[ static_cast<std::size_t>( nTeam ) ], []( const DemoScoreRow_t *pRow ) { return pRow->m_bAlive; } ) );
	};
	const auto score = [ & ]( int nTeam ) {
		const auto &n = board.m_TeamScores[ static_cast<std::size_t>( nTeam ) ];
		return n ? QString::number( *n ) : QStringLiteral( "-" );
	};
	SetScoreboardVariable( "mapname", board.m_szMap );
	SetScoreboardVariable( "t_teamname", board.m_TeamNames[ 2 ] );
	SetScoreboardVariable( "ct_teamname", board.m_TeamNames[ 3 ] );
	SetScoreboardVariable( "t_totalteamscore", score( 2 ) );
	SetScoreboardVariable( "ct_totalteamscore", score( 3 ) );
	SetScoreboardVariable( "t_alivecount", QString::number( alive( 2 ) ) );
	SetScoreboardVariable( "ct_alivecount", QString::number( alive( 3 ) ) );
	SetScoreboardVariable( "spectators", spectators.isEmpty() ? QString{} : QStringLiteral( "Spectators: %1" ).arg( spectators.join( QStringLiteral( ", " ) ) ) );
	SetScoreboardVariable( "statsstatus", QString{} );
	SetScoreboardVariable( "sourcetv", QString{} );
	m_pScoreboard->PaintScreen( painter, QRectF( rect ) );

	const double flZoom = rect.height() / static_cast<double>( m_pScoreboard->GetScreen().m_nTall );
	const auto find = [ & ]( const std::string &szName ) -> const xv::hud::Element_t * {
		for ( const xv::hud::Element_t &element : m_pScoreboard->GetElements() )
		{
			if ( xv::kv1::EqualsIgnoreCase( element.m_szName, szName ) )
				return &element;
		}
		return nullptr;
	};
	const auto boxOf = [ & ]( const xv::hud::Element_t &element ) {
		return QRectF( rect.left() + element.m_Rect.m_nX * flZoom, rect.top() + element.m_Rect.m_nY * flZoom, element.m_Rect.m_nWide * flZoom, element.m_Rect.m_nTall * flZoom );
	};
	// The server's name, which the game writes into its label itself.
	if ( const xv::hud::Element_t *pServer = find( "ServerNameLabel" ); pServer && pServer->m_bVisible )
	{
		painter.setFont( m_pScoreboard->FontFor( pServer->m_szFont.empty() ? "Default" : pServer->m_szFont, flZoom ) );
		painter.setPen( pServer->m_FgColor ? QColor( pServer->m_FgColor->m_nR, pServer->m_FgColor->m_nG, pServer->m_FgColor->m_nB, pServer->m_FgColor->m_nA ) : QColor( 255, 255, 255 ) );
		painter.drawText( boxOf( *pServer ), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, painter.fontMetrics().elidedText( board.m_szServer, Qt::ElideRight, static_cast<int>( boxOf( *pServer ).width() ) ) );
	}
	std::string szBody;
	for ( const char *pszFont : { "ScoreboardBody_1", "ScoreboardBody", "Default" } )
	{
		if ( m_pScoreboard->GetScheme().ResolveFont( pszFont, m_pScoreboard->GetScreen() ) )
		{
			szBody = pszFont;
			break;
		}
	}
	const QFont body = m_pScoreboard->FontFor( szBody.empty() ? "Default" : szBody, flZoom );
	for ( const int nTeam : { 2, 3 } )
	{
		const std::string szPrefix = nTeam == 3 ? "CT" : "T";
		const xv::hud::Element_t *pName = find( szPrefix + "PlayerName0" );
		if ( !pName )
			continue;
		const xv::hud::Element_t *pArea = find( szPrefix + "PlayerArea" );
		const xv::hud::Element_t *pHeading = find( szPrefix + "PlayerLabel" );
		const QColor teamColor = pHeading && pHeading->m_FgColor ? QColor( pHeading->m_FgColor->m_nR, pHeading->m_FgColor->m_nG, pHeading->m_FgColor->m_nB ) : DemoTeamColor( nTeam );
		const double flRow = std::max( 1.0, pName->m_Rect.m_nTall * flZoom );
		const double flBottom = pArea ? boxOf( *pArea ).bottom() : rect.bottom();
		painter.setFont( body );
		for ( std::size_t i = 0; i < teams[ static_cast<std::size_t>( nTeam ) ].size(); i++ )
		{
			const DemoScoreRow_t &row = *teams[ static_cast<std::size_t>( nTeam ) ][ i ];
			const double flDown = static_cast<double>( i ) * flRow;
			if ( boxOf( *pName ).top() + flDown + flRow > flBottom + 0.5 )
				break;
			QColor color = teamColor;
			if ( !row.m_bAlive )
				color.setAlphaF( 0.45f );
			const auto cell = [ & ]( const char *pszColumn, const QString &szText, Qt::Alignment nAlign ) {
				const xv::hud::Element_t *pCell = find( szPrefix + pszColumn + "0" );
				if ( !pCell || szText.isEmpty() )
					return;
				const QRectF box = boxOf( *pCell ).translated( 0, flDown );
				painter.setPen( color );
				painter.drawText( box, nAlign | Qt::AlignVCenter | Qt::TextSingleLine, painter.fontMetrics().elidedText( szText, Qt::ElideRight, static_cast<int>( box.width() ) ) );
			};
			if ( row.m_bFollowed )
			{
				QRectF line = boxOf( *pName ).translated( 0, flDown );
				if ( pArea )
					line = QRectF( boxOf( *pArea ).left(), line.top(), boxOf( *pArea ).width(), line.height() );
				QColor backing = teamColor;
				backing.setAlpha( 60 );
				painter.fillRect( line, backing );
			}
			cell( "PlayerName", row.m_szName, Qt::AlignLeft );
			cell( "PlayerScore", row.m_nScore ? QString::number( *row.m_nScore ) : QString{}, Qt::AlignHCenter );
			cell( "PlayerDeaths", row.m_nDeaths ? QString::number( *row.m_nDeaths ) : QString{}, Qt::AlignHCenter );
			cell( "PlayerLatency", row.m_nPing ? QString::number( *row.m_nPing ) : QString{}, Qt::AlignHCenter );
			cell( "PlayerStatus", row.m_bAlive ? QString{} : QStringLiteral( "\u2620" ), Qt::AlignHCenter );
			if ( row.m_bTalking )
				cell( "PlayerAvatar", row.m_bMuted ? QStringLiteral( "\U0001F507" ) : QStringLiteral( "\U0001F50A" ), Qt::AlignHCenter );
		}
	}
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: Without the game's files: the two teams side by side in a dark box, a line each.
//-----------------------------------------------------------------------------
void CDemoHud::PaintPlainScoreboard( QPainter &painter, const QRect &rect, const DemoScoreboard_t &board, double flScale ) const
{
	painter.save();
	const QRectF box( rect.center().x() - 300 * flScale, rect.center().y() - 190 * flScale, 600 * flScale, 380 * flScale );
	Backing( painter, box, flScale, 190 );
	QFont font = FontFor( { "Default" }, flScale );
	painter.setFont( font );
	const QFontMetricsF metrics( font );
	const double flLine = metrics.height() + 2 * flScale;
	painter.setPen( QColor( 230, 230, 230 ) );
	painter.drawText( QRectF( box.left() + 12 * flScale, box.top() + 8 * flScale, box.width() - 24 * flScale, flLine ), Qt::AlignLeft | Qt::AlignVCenter, board.m_szServer );
	painter.drawText( QRectF( box.left() + 12 * flScale, box.top() + 8 * flScale, box.width() - 24 * flScale, flLine ), Qt::AlignRight | Qt::AlignVCenter, board.m_szMap );
	const double flHalf = box.width() / 2.0;
	for ( const int nTeam : { 3, 2 } )
	{
		const double flLeft = box.left() + ( nTeam == 3 ? 0.0 : flHalf ) + 12 * flScale;
		const double flWide = flHalf - 24 * flScale;
		double flY = box.top() + 8 * flScale + flLine * 1.6;
		const auto &score = board.m_TeamScores[ static_cast<std::size_t>( nTeam ) ];
		painter.setPen( DemoTeamColor( nTeam ) );
		painter.drawText( QRectF( flLeft, flY, flWide, flLine ), Qt::AlignLeft | Qt::AlignVCenter, board.m_TeamNames[ static_cast<std::size_t>( nTeam ) ] );
		if ( score )
			painter.drawText( QRectF( flLeft, flY, flWide, flLine ), Qt::AlignRight | Qt::AlignVCenter, QString::number( *score ) );
		flY += flLine * 1.4;
		std::vector<const DemoScoreRow_t *> rows;
		for ( const DemoScoreRow_t &row : board.m_Rows )
		{
			if ( row.m_nTeam == nTeam )
				rows.push_back( &row );
		}
		std::ranges::sort( rows, []( const DemoScoreRow_t *a, const DemoScoreRow_t *b ) { return a->m_nScore.value_or( 0 ) != b->m_nScore.value_or( 0 ) ? a->m_nScore.value_or( 0 ) > b->m_nScore.value_or( 0 ) : a->m_nDeaths.value_or( 0 ) < b->m_nDeaths.value_or( 0 ); } );
		for ( const DemoScoreRow_t *pRow : rows )
		{
			if ( flY + flLine > box.bottom() - 8 * flScale )
				break;
			QColor color = DemoTeamColor( nTeam );
			if ( !pRow->m_bAlive )
				color.setAlphaF( 0.45f );
			if ( pRow->m_bFollowed )
				painter.fillRect( QRectF( flLeft - 4 * flScale, flY, flWide + 8 * flScale, flLine ), QColor( 255, 255, 255, 40 ) );
			painter.setPen( color );
			const QString szNumbers = QStringLiteral( "%1   %2   %3" ).arg( pRow->m_nScore.value_or( 0 ) ).arg( pRow->m_nDeaths.value_or( 0 ) ).arg( pRow->m_nPing ? QString::number( *pRow->m_nPing ) : QStringLiteral( "-" ) );
			const double flNumbers = metrics.horizontalAdvance( szNumbers );
			painter.drawText( QRectF( flLeft, flY, flWide - flNumbers - 8 * flScale, flLine ), Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText( pRow->m_szName, Qt::ElideRight, flWide - flNumbers - 8 * flScale ) );
			painter.drawText( QRectF( flLeft, flY, flWide, flLine ), Qt::AlignRight | Qt::AlignVCenter, szNumbers );
			flY += flLine;
		}
	}
	painter.restore();
}
