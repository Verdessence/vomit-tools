//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Feed.h.
//
//===========================================================================//
#include "Feed.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace xv::demo
{

namespace
{

// A fade's times are in 512ths of a second.
constexpr double FADE_UNIT_SECONDS = 1.0 / 512.0;
// A server's message dialog stays this long at least and at most, whatever it asks for.
constexpr double DIALOG_LEAST_SECONDS = 10.0;
constexpr double DIALOG_MOST_SECONDS = 200.0;
// How long the client keeps a hint, centre text and a key hint up.
constexpr double HINT_SECONDS = 6.0;
constexpr double CENTER_SECONDS = 5.0;
constexpr double KEY_HINT_SECONDS = 6.0;
// TextMsg's destinations.
constexpr int TEXT_DEST_NOTIFY = 1;
constexpr int TEXT_DEST_CONSOLE = 2;
constexpr int TEXT_DEST_CHAT = 3;
constexpr int TEXT_DEST_CENTER = 4;
// A flicker's colour changes this many times a second.
constexpr double FLICKER_RATE = 20.0;

//-----------------------------------------------------------------------------
// Purpose: Hex digits as a number; nullopt when one isn't.
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> ParseHex( std::string_view szDigits )
{
	std::uint32_t nValue = 0;
	for ( const char ch : szDigits )
	{
		int nDigit = 0;
		if ( ch >= '0' && ch <= '9' )
			nDigit = ch - '0';
		else if ( ch >= 'a' && ch <= 'f' )
			nDigit = ch - 'a' + 10;
		else if ( ch >= 'A' && ch <= 'F' )
			nDigit = ch - 'A' + 10;
		else
			return std::nullopt;
		nValue = nValue * 16 + static_cast<std::uint32_t>( nDigit );
	}
	return nValue;
}

//-----------------------------------------------------------------------------
// Purpose: A user message's text field, or empty.
//-----------------------------------------------------------------------------
std::string TextField( const UserMessage_t &message, std::string_view szName )
{
	const Value_t *pValue = message.Find( szName );
	return pValue ? pValue->m_szText : std::string{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double NumberField( const UserMessage_t &message, std::string_view szName, double flDefault = 0.0 )
{
	const Value_t *pValue = message.Find( szName );
	return pValue ? pValue->m_flNumber : flDefault;
}

//-----------------------------------------------------------------------------
// Purpose: param1 to param4, as many as the message has.
//-----------------------------------------------------------------------------
std::vector<std::string> Params( const UserMessage_t &message )
{
	std::vector<std::string> params;
	for ( const char *pszName : { "param1", "param2", "param3", "param4" } )
	{
		if ( const Value_t *pValue = message.Find( pszName ) )
			params.push_back( pValue->m_szText );
	}
	return params;
}

//-----------------------------------------------------------------------------
// Purpose: Four bytes of colour, red first, as one number with red in the top byte.
//-----------------------------------------------------------------------------
std::uint32_t Rgba( double flR, double flG, double flB, double flA )
{
	const auto byte = []( double fl ) { return static_cast<std::uint32_t>( std::clamp( static_cast<int>( fl ), 0, 255 ) ); };
	return byte( flR ) << 24 | byte( flG ) << 16 | byte( flB ) << 8 | byte( flA );
}

//-----------------------------------------------------------------------------
// Purpose: "255 64 0 255" as a colour; alpha 255 when left out.
//-----------------------------------------------------------------------------
std::uint32_t ParseRgba( std::string_view szText )
{
	int nParts[ 4 ] = { 255, 255, 255, 255 };
	std::size_t nAt = 0;
	for ( int i = 0; i < 4 && nAt < szText.size(); i++ )
	{
		while ( nAt < szText.size() && szText[ nAt ] == ' ' )
			nAt++;
		const std::size_t nEnd = std::min( szText.find( ' ', nAt ), szText.size() );
		if ( nEnd > nAt )
			nParts[ i ] = std::atoi( std::string( szText.substr( nAt, nEnd - nAt ) ).c_str() );
		nAt = nEnd;
	}
	return Rgba( nParts[ 0 ], nParts[ 1 ], nParts[ 2 ], nParts[ 3 ] );
}

//-----------------------------------------------------------------------------
// Purpose: A message dialog's keys: its title, colour, level and time.
//-----------------------------------------------------------------------------
const MenuKey_t *FindMenuKey( const std::vector<MenuKey_t> &keys, std::string_view szName )
{
	for ( const MenuKey_t &key : keys )
	{
		if ( key.m_szName == szName )
			return &key;
		if ( const MenuKey_t *pFound = FindMenuKey( key.m_Children, szName ) )
			return pFound;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: How bright a fade is at a time: rising, holding, falling, all 0 to 1.
//-----------------------------------------------------------------------------
double Ramp( double flSeconds, double flIn, double flHold, double flOut )
{
	if ( flSeconds < 0.0 )
		return 0.0;
	if ( flSeconds < flIn )
		return flIn > 0.0 ? flSeconds / flIn : 1.0;
	flSeconds -= flIn;
	if ( flSeconds < flHold )
		return 1.0;
	flSeconds -= flHold;
	if ( flSeconds < flOut )
		return flOut > 0.0 ? 1.0 - flSeconds / flOut : 0.0;
	return 0.0;
}

//-----------------------------------------------------------------------------
// Purpose: One colour part way to another, alpha scaled.
//-----------------------------------------------------------------------------
std::uint32_t Blend( std::uint32_t nFrom, std::uint32_t nTo, double flPart, double flAlpha )
{
	const auto part = [ & ]( int nShift ) {
		const double flA = static_cast<double>( ( nFrom >> nShift ) & 0xff );
		const double flB = static_cast<double>( ( nTo >> nShift ) & 0xff );
		return flA + ( flB - flA ) * flPart;
	};
	return Rgba( part( 24 ), part( 16 ), part( 8 ), part( 0 ) * std::clamp( flAlpha, 0.0, 1.0 ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: \x01 to \x05 change to a colour of the chat's; \x07 and \x08 bring their own in
//          hex. \x02 and \x06 start the default colour again; other control characters are
//          left out.
//-----------------------------------------------------------------------------
std::vector<ColorRun_t> SplitColors( std::string_view szText )
{
	std::vector<ColorRun_t> runs;
	ColorRun_t run;
	const auto flush = [ & ] {
		if ( !run.m_szText.empty() )
			runs.push_back( run );
		run.m_szText.clear();
	};
	for ( std::size_t i = 0; i < szText.size(); i++ )
	{
		const unsigned char ch = static_cast<unsigned char>( szText[ i ] );
		if ( ch == 0x07 || ch == 0x08 )
		{
			const std::size_t nDigits = ch == 0x07 ? 6 : 8;
			const std::optional<std::uint32_t> nValue = i + nDigits < szText.size() + 1 ? ParseHex( szText.substr( i + 1, nDigits ) ) : std::nullopt;
			if ( nValue && i + nDigits < szText.size() + 1 )
			{
				flush();
				run.m_nColor = TextColor_t::TEXT_COLOR_RGB;
				run.m_nRgba = ch == 0x07 ? ( *nValue << 8 | 0xff ) : *nValue;
				i += nDigits;
				continue;
			}
		}
		if ( ch >= 0x01 && ch <= 0x06 )
		{
			flush();
			run.m_nColor = ch == 0x03 ? TextColor_t::TEXT_COLOR_TEAM : ch == 0x04 ? TextColor_t::TEXT_COLOR_GREEN
				: ch == 0x05                                                      ? TextColor_t::TEXT_COLOR_OLIVE
																				  : TextColor_t::TEXT_COLOR_DEFAULT;
			continue;
		}
		// Other control characters (a code cut short) show as nothing.
		if ( ch < 0x20 && ch != '\n' && ch != '\t' )
			continue;
		run.m_szText += static_cast<char>( ch );
	}
	flush();
	return runs;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string StripColors( std::string_view szText )
{
	std::string szOut;
	for ( const ColorRun_t &run : SplitColors( szText ) )
		szOut += run.m_szText;
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: A token the files lack stays as written (the client shows it so); a param that is
//          a token is put in words before it goes in.
//-----------------------------------------------------------------------------
std::string FormatGameText( std::string_view szText, std::span<const std::string> params, const Localize_t &pfnLocalize )
{
	const auto localize = [ & ]( std::string_view szIn ) -> std::string {
		if ( !szIn.starts_with( '#' ) || !pfnLocalize )
			return std::string( szIn );
		const std::string_view szToken = szIn.substr( 1 );
		if ( std::optional<std::string> szFound = pfnLocalize( szToken ) )
			return *szFound;
		if ( std::optional<std::string> szFound = pfnLocalize( "Cstrike_TitlesTXT_" + std::string( szToken ) ) )
			return *szFound;
		return std::string( szIn );
	};
	const std::string szFormat = localize( szText );
	std::string szOut;
	for ( std::size_t i = 0; i < szFormat.size(); i++ )
	{
		if ( szFormat[ i ] == '%' && i + 2 < szFormat.size() + 1 && szFormat[ i + 1 ] == 's' && i + 2 < szFormat.size() && szFormat[ i + 2 ] >= '1' && szFormat[ i + 2 ] <= '4' )
		{
			const std::size_t nParam = static_cast<std::size_t>( szFormat[ i + 2 ] - '1' );
			if ( nParam < params.size() )
				szOut += localize( params[ nParam ] );
			i += 2;
			continue;
		}
		szOut += szFormat[ i ];
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Its keys as the game_text entity has them: colours as "r g b", times in seconds,
//          the message with "\n" for its line breaks.
//-----------------------------------------------------------------------------
HudText_t HudTextFromGameText( const std::function<std::string_view( std::string_view )> &pfnValue )
{
	const auto number = [ & ]( std::string_view szKey, double flDefault ) {
		const std::string szValue( pfnValue( szKey ) );
		return szValue.empty() ? flDefault : std::atof( szValue.c_str() );
	};
	HudText_t text;
	text.m_nChannel = std::clamp( static_cast<int>( number( "channel", 1.0 ) ), 0, HUD_CHANNEL_COUNT - 1 );
	text.m_flX = static_cast<float>( number( "x", -1.0 ) );
	text.m_flY = static_cast<float>( number( "y", -1.0 ) );
	text.m_nEffect = static_cast<int>( number( "effect", 0.0 ) );
	text.m_flFadeIn = static_cast<float>( number( "fadein", 1.5 ) );
	text.m_flFadeOut = static_cast<float>( number( "fadeout", 0.5 ) );
	text.m_flHold = static_cast<float>( number( "holdtime", 1.2 ) );
	text.m_flFxTime = static_cast<float>( number( "fxtime", 0.25 ) );
	const std::string_view szColor = pfnValue( "color" );
	const std::string_view szColor2 = pfnValue( "color2" );
	text.m_nColor1 = szColor.empty() ? 0x646464ff : ( ParseRgba( szColor ) | 0xff );
	text.m_nColor2 = szColor2.empty() ? 0xf06e00ff : ( ParseRgba( szColor2 ) | 0xff );
	const std::string_view szMessage = pfnValue( "message" );
	for ( std::size_t i = 0; i < szMessage.size(); i++ )
	{
		if ( szMessage[ i ] == '\\' && i + 1 < szMessage.size() && szMessage[ i + 1 ] == 'n' )
		{
			text.m_szText += '\n';
			i++;
			continue;
		}
		text.m_szText += szMessage[ i ];
	}
	return text;
}

//-----------------------------------------------------------------------------
// Purpose: Faded in (each letter in, for the scan-out, and settled to its colour).
//-----------------------------------------------------------------------------
double HudTextHeldAt( const HudText_t &text )
{
	if ( text.m_nEffect == HUD_EFFECT_SCAN_OUT )
		return static_cast<double>( text.m_flFadeIn ) * static_cast<double>( text.m_szText.size() ) + text.m_flFxTime + 0.001;
	return static_cast<double>( text.m_flFadeIn ) + 0.001;
}

//-----------------------------------------------------------------------------
// Purpose: The scan-out writes a letter each fadein seconds; the others fade whole.
//-----------------------------------------------------------------------------
double HudText_t::GetDuration() const
{
	const double flIn = m_nEffect == HUD_EFFECT_SCAN_OUT ? static_cast<double>( m_flFadeIn ) * static_cast<double>( m_szText.size() ) : m_flFadeIn;
	return std::max( 0.0, flIn + static_cast<double>( m_flHold ) + static_cast<double>( m_flFadeOut ) );
}

//-----------------------------------------------------------------------------
// Purpose: The fade: the whole text rises over fadein, holds, falls over fadeout. The
//          flicker: the same, its colour now and then the second one. The scan-out: each
//          letter comes fadein seconds after the one before, shows in the second colour for
//          fxtime and blends to the first, then all fade out together after the hold.
//-----------------------------------------------------------------------------
std::uint32_t HudLetterColor( const HudText_t &text, int nLetter, double flSeconds )
{
	if ( text.m_nEffect == HUD_EFFECT_SCAN_OUT )
	{
		const double flLetterIn = static_cast<double>( text.m_flFadeIn ) * nLetter;
		if ( flSeconds < flLetterIn )
			return 0;
		const double flShown = flSeconds - flLetterIn;
		const double flAllIn = static_cast<double>( text.m_flFadeIn ) * static_cast<double>( text.m_szText.size() );
		const double flFall = flSeconds - flAllIn - text.m_flHold;
		const double flAlpha = flFall <= 0.0 ? 1.0 : text.m_flFadeOut > 0.0 ? 1.0 - flFall / text.m_flFadeOut
																			: 0.0;
		if ( flAlpha <= 0.0 )
			return 0;
		const double flToFirst = text.m_flFxTime > 0.0f ? std::clamp( flShown / text.m_flFxTime, 0.0, 1.0 ) : 1.0;
		return Blend( text.m_nColor2, text.m_nColor1, flToFirst, flAlpha );
	}
	const double flAlpha = Ramp( flSeconds, text.m_flFadeIn, text.m_flHold, text.m_flFadeOut );
	if ( flAlpha <= 0.0 )
		return 0;
	std::uint32_t nColor = text.m_nColor1;
	if ( text.m_nEffect == HUD_EFFECT_FLICKER )
	{
		// A steady pattern by the time, so the same moment always looks the same.
		const int nStep = static_cast<int>( flSeconds * FLICKER_RATE );
		if ( ( ( nStep * 2654435761u ) >> 29 ) % 3 == 0 )
			nColor = text.m_nColor2;
	}
	return Blend( nColor, nColor, 0.0, flAlpha );
}

//-----------------------------------------------------------------------------
// Purpose: To the colour: rising over the duration, then held. From it: held, then falling
//          over the duration. Staying out keeps it on the colour after.
//-----------------------------------------------------------------------------
double Fade_t::CoverAt( double flSeconds ) const
{
	if ( flSeconds < 0.0 )
		return 0.0;
	if ( m_nFlags & FADE_FLAG_OUT )
	{
		if ( flSeconds < m_flDuration )
			return m_flDuration > 0.0 ? flSeconds / m_flDuration : 1.0;
		return ( flSeconds < m_flDuration + m_flHold || ( m_nFlags & FADE_FLAG_STAY_OUT ) ) ? 1.0 : 0.0;
	}
	if ( flSeconds < m_flHold )
		return 1.0;
	const double flFall = flSeconds - m_flHold;
	return flFall < m_flDuration && m_flDuration > 0.0 ? 1.0 - flFall / m_flDuration : 0.0;
}

//-----------------------------------------------------------------------------
// Purpose: The user id's slot at the tick, from the identities the players' tracks keep.
//-----------------------------------------------------------------------------
int EntityOfUserId( const CPlayers &players, int nUserId, int nTick )
{
	int nAny = 0;
	for ( const PlayerTrack_t &track : players.GetTracks() )
	{
		if ( const PlayerIdentity_t *pIdentity = track.IdentityAt( nTick ); pIdentity && pIdentity->m_nUserId == nUserId )
			return track.m_nEntity;
		for ( const PlayerIdentity_t &identity : track.m_Identities )
		{
			if ( identity.m_nUserId == nUserId )
				nAny = track.m_nEntity;
		}
	}
	return nAny;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<int> SpeakersAt( const CContents &contents, int nTick )
{
	std::vector<int> speakers;
	for ( const Speech_t &speech : contents.GetSpeech() )
	{
		if ( speech.m_nFrom <= nTick && nTick <= speech.m_nTo && std::ranges::find( speakers, speech.m_nEntity ) == speakers.end() )
			speakers.push_back( speech.m_nEntity );
	}
	return speakers;
}

//-----------------------------------------------------------------------------
// Purpose: Every user message, game event, menu and print that shows on a screen, by kind,
//          each list in tick order.
//-----------------------------------------------------------------------------
CFeed CFeed::Build( const CContents &contents, const CPlayers &players, const Localize_t &pfnLocalize, std::span<const Command_t> commands )
{
	CFeed feed;
	feed.m_flTickInterval = contents.GetTickInterval() > 0.0 ? contents.GetTickInterval() : 0.015;
	MenuShown_t partMenu; // a ShowMenu's parts so far
	bool bMenuOpen = false;
	for ( const UserMessage_t &message : contents.GetUserMessages() )
	{
		if ( !message.m_bRead )
			continue;
		const std::string_view szKind = message.m_szKind;
		FeedLine_t line;
		line.m_nTick = message.m_nTick;
		if ( szKind == "SayText2" || szKind == "SayText" )
		{
			line.m_nKind = FeedKind_t::FEED_KIND_CHAT;
			line.m_nEntity = static_cast<int>( NumberField( message, "entity" ) );
			line.m_szText = FormatGameText( TextField( message, "text" ), Params( message ), pfnLocalize );
		}
		else if ( szKind == "RadioText" )
		{
			line.m_nKind = FeedKind_t::FEED_KIND_RADIO;
			line.m_nEntity = static_cast<int>( NumberField( message, "entity" ) );
			line.m_szText = FormatGameText( TextField( message, "text" ), Params( message ), pfnLocalize );
		}
		else if ( szKind == "TextMsg" )
		{
			const int nDest = static_cast<int>( NumberField( message, "destination" ) );
			line.m_nKind = nDest == TEXT_DEST_CENTER ? FeedKind_t::FEED_KIND_CENTER : nDest == TEXT_DEST_CONSOLE ? FeedKind_t::FEED_KIND_CONSOLE
																												 : FeedKind_t::FEED_KIND_NOTICE;
			if ( nDest != TEXT_DEST_NOTIFY && nDest != TEXT_DEST_CHAT && nDest != TEXT_DEST_CONSOLE && nDest != TEXT_DEST_CENTER )
				continue;
			line.m_szText = FormatGameText( TextField( message, "text" ), Params( message ), pfnLocalize );
			while ( !line.m_szText.empty() && ( line.m_szText.back() == '\n' || line.m_szText.back() == '\r' ) )
				line.m_szText.pop_back();
			if ( line.m_nKind == FeedKind_t::FEED_KIND_CENTER )
				line.m_flSeconds = CENTER_SECONDS;
		}
		else if ( szKind == "HintText" )
		{
			line.m_nKind = FeedKind_t::FEED_KIND_HINT;
			line.m_szText = FormatGameText( TextField( message, "text" ), {}, pfnLocalize );
			line.m_flSeconds = HINT_SECONDS;
		}
		else if ( szKind == "KeyHintText" )
		{
			line.m_nKind = FeedKind_t::FEED_KIND_KEY_HINT;
			for ( const UserField_t &field : message.m_Fields )
			{
				if ( field.m_szName == "text" )
					line.m_szText += ( line.m_szText.empty() ? "" : "\n" ) + FormatGameText( field.m_Value.m_szText, {}, pfnLocalize );
			}
			line.m_flSeconds = KEY_HINT_SECONDS;
		}
		else if ( szKind == "HudMsg" )
		{
			HudText_t text;
			text.m_nTick = message.m_nTick;
			text.m_nChannel = static_cast<int>( NumberField( message, "channel" ) ) % HUD_CHANNEL_COUNT;
			text.m_flX = static_cast<float>( NumberField( message, "x", -1.0 ) );
			text.m_flY = static_cast<float>( NumberField( message, "y", -1.0 ) );
			text.m_nColor1 = Rgba( NumberField( message, "r1" ), NumberField( message, "g1" ), NumberField( message, "b1" ), NumberField( message, "a1" ) );
			text.m_nColor2 = Rgba( NumberField( message, "r2" ), NumberField( message, "g2" ), NumberField( message, "b2" ), NumberField( message, "a2" ) );
			text.m_nEffect = static_cast<int>( NumberField( message, "effect" ) );
			text.m_flFadeIn = static_cast<float>( NumberField( message, "fadein" ) );
			text.m_flFadeOut = static_cast<float>( NumberField( message, "fadeout" ) );
			text.m_flHold = static_cast<float>( NumberField( message, "holdtime" ) );
			text.m_flFxTime = static_cast<float>( NumberField( message, "fxtime" ) );
			text.m_szText = FormatGameText( TextField( message, "text" ), {}, pfnLocalize );
			feed.m_HudTexts.push_back( std::move( text ) );
			continue;
		}
		else if ( szKind == "ShowMenu" )
		{
			// A long menu comes in parts, each but the last saying more follows.
			if ( !bMenuOpen )
			{
				partMenu = {};
				partMenu.m_nTick = message.m_nTick;
				bMenuOpen = true;
			}
			partMenu.m_szText += TextField( message, "text" );
			partMenu.m_nSlots = static_cast<int>( NumberField( message, "slots" ) );
			const int nTime = static_cast<int>( NumberField( message, "time", -1.0 ) );
			partMenu.m_flSeconds = nTime < 0 ? -1.0 : static_cast<double>( nTime );
			if ( NumberField( message, "more" ) == 0.0 )
			{
				partMenu.m_szText = FormatGameText( partMenu.m_szText, {}, pfnLocalize );
				feed.m_Menus.push_back( std::move( partMenu ) );
				bMenuOpen = false;
			}
			continue;
		}
		else if ( szKind == "VGUIMenu" )
		{
			PanelShown_t panel;
			panel.m_nTick = message.m_nTick;
			bool bFirst = true;
			std::string szKey;
			for ( const UserField_t &field : message.m_Fields )
			{
				if ( field.m_szName == "name" && bFirst )
				{
					panel.m_szName = field.m_Value.m_szText;
					bFirst = false;
				}
				else if ( field.m_szName == "show" )
					panel.m_bShow = field.m_Value.m_flNumber != 0.0;
				else if ( field.m_szName == "name" )
					szKey = field.m_Value.m_szText;
				else if ( field.m_szName == "key" )
					panel.m_Keys.emplace_back( szKey, field.m_Value.m_szText );
			}
			feed.m_Panels.push_back( std::move( panel ) );
			continue;
		}
		else if ( szKind == "Fade" )
		{
			Fade_t fade;
			fade.m_nTick = message.m_nTick;
			fade.m_flDuration = NumberField( message, "duration" ) * FADE_UNIT_SECONDS;
			fade.m_flHold = NumberField( message, "hold" ) * FADE_UNIT_SECONDS;
			fade.m_nFlags = static_cast<int>( NumberField( message, "flags" ) );
			fade.m_nRgba = Rgba( NumberField( message, "r" ), NumberField( message, "g" ), NumberField( message, "b" ), NumberField( message, "a" ) );
			feed.m_Fades.push_back( fade );
			continue;
		}
		else
		{
			continue;
		}
		if ( !line.m_szText.empty() )
			feed.m_Lines.push_back( std::move( line ) );
	}
	for ( const auto &[ nTick, szPrint ] : contents.GetPrints() )
	{
		FeedLine_t line;
		line.m_nTick = nTick;
		line.m_nKind = FeedKind_t::FEED_KIND_CONSOLE;
		line.m_szText = szPrint;
		while ( !line.m_szText.empty() && ( line.m_szText.back() == '\n' || line.m_szText.back() == '\r' ) )
			line.m_szText.pop_back();
		if ( !line.m_szText.empty() )
			feed.m_Lines.push_back( std::move( line ) );
	}
	for ( const Menu_t &menu : contents.GetMenus() )
	{
		if ( menu.m_nKind != static_cast<int>( MenuKind_t::MENU_KIND_MESSAGE ) || !menu.m_bRead )
			continue;
		const MenuKey_t *pTitle = FindMenuKey( menu.m_Keys, "title" );
		if ( !pTitle )
			continue;
		FeedLine_t line;
		line.m_nTick = menu.m_nTick;
		line.m_nKind = FeedKind_t::FEED_KIND_DIALOG;
		line.m_szText = FormatGameText( pTitle->m_szValue, {}, pfnLocalize );
		if ( const MenuKey_t *pColor = FindMenuKey( menu.m_Keys, "color" ) )
			line.m_nRgba = ParseRgba( pColor->m_szValue );
		if ( const MenuKey_t *pLevel = FindMenuKey( menu.m_Keys, "level" ) )
			line.m_nLevel = std::atoi( pLevel->m_szValue.c_str() );
		const MenuKey_t *pTime = FindMenuKey( menu.m_Keys, "time" );
		line.m_flSeconds = std::clamp( pTime ? std::atof( pTime->m_szValue.c_str() ) : 0.0, DIALOG_LEAST_SECONDS, DIALOG_MOST_SECONDS );
		feed.m_Lines.push_back( std::move( line ) );
	}
	std::ranges::stable_sort( feed.m_Lines, {}, &FeedLine_t::m_nTick );
	for ( const Command_t &command : commands )
	{
		if ( !command.m_szText.starts_with( "menuselect" ) )
			continue;
		const std::size_t nUpTo = CountUpTo( feed.m_Menus, command.m_nTick );
		if ( nUpTo > 0 && feed.m_Menus[ nUpTo - 1 ].m_nPicked < 0 )
			feed.m_Menus[ nUpTo - 1 ].m_nPicked = command.m_nTick;
	}

	const int nDeath = contents.FindEventKind( "player_death" );
	const int nRoundStart = contents.FindEventKind( "round_start" );
	const int nRoundEnd = contents.FindEventKind( "round_end" );
	for ( const GameEvent_t &event : contents.GetEvents() )
	{
		const auto number = [ & ]( std::string_view szKey ) {
			const Value_t *pValue = contents.EventValue( event, szKey );
			return pValue ? pValue->m_flNumber : 0.0;
		};
		if ( event.m_nKind == nDeath && nDeath >= 0 )
		{
			Kill_t kill;
			kill.m_nTick = event.m_nTick;
			kill.m_nVictim = EntityOfUserId( players, static_cast<int>( number( "userid" ) ), event.m_nTick );
			const int nAttacker = static_cast<int>( number( "attacker" ) );
			kill.m_nAttacker = nAttacker ? EntityOfUserId( players, nAttacker, event.m_nTick ) : 0;
			if ( const Value_t *pWeapon = contents.EventValue( event, "weapon" ) )
				kill.m_szWeapon = pWeapon->m_szText;
			kill.m_bHeadshot = number( "headshot" ) != 0.0;
			feed.m_Kills.push_back( std::move( kill ) );
		}
		else if ( event.m_nKind == nRoundStart && nRoundStart >= 0 )
		{
			if ( !feed.m_Rounds.empty() && feed.m_Rounds.back().m_nEnd < 0 && feed.m_Rounds.back().m_nStart == event.m_nTick )
				continue;
			Round_t round;
			round.m_nStart = event.m_nTick;
			feed.m_Rounds.push_back( round );
		}
		else if ( event.m_nKind == nRoundEnd && nRoundEnd >= 0 )
		{
			// A demo that starts mid-round sees its end first: a round from the start.
			if ( feed.m_Rounds.empty() || feed.m_Rounds.back().m_nEnd >= 0 )
				feed.m_Rounds.push_back( Round_t{ 0, -1, 0, {} } );
			Round_t &round = feed.m_Rounds.back();
			round.m_nEnd = event.m_nTick;
			round.m_nWinner = static_cast<int>( number( "winner" ) );
			if ( const Value_t *pMessage = contents.EventValue( event, "message" ) )
				round.m_szMessage = FormatGameText( pMessage->m_szText, {}, pfnLocalize );
		}
	}
	return feed;
}

//-----------------------------------------------------------------------------
// Purpose: The newest message on each channel, while it lasts.
//-----------------------------------------------------------------------------
std::vector<CFeed::ShownText_t> CFeed::HudTextsAt( double flTick ) const
{
	std::vector<ShownText_t> shown( HUD_CHANNEL_COUNT );
	const std::size_t nUpTo = CountUpTo( m_HudTexts, static_cast<int>( std::floor( flTick ) ) );
	std::vector<bool> settled( HUD_CHANNEL_COUNT, false );
	for ( std::size_t i = nUpTo; i-- > 0; )
	{
		const HudText_t &text = m_HudTexts[ i ];
		const std::size_t nChannel = static_cast<std::size_t>( std::clamp( text.m_nChannel, 0, HUD_CHANNEL_COUNT - 1 ) );
		if ( settled[ nChannel ] )
			continue;
		settled[ nChannel ] = true;
		const double flSeconds = ( flTick - text.m_nTick ) * m_flTickInterval;
		if ( flSeconds <= text.GetDuration() )
			shown[ nChannel ] = { &text, flSeconds };
		if ( std::ranges::all_of( settled, []( bool b ) { return b; } ) )
			break;
	}
	return shown;
}

//-----------------------------------------------------------------------------
// Purpose: The newest menu, unless its time ran out or it was an empty one (a close).
//-----------------------------------------------------------------------------
const MenuShown_t *CFeed::MenuAt( double flTick ) const
{
	const std::size_t nUpTo = CountUpTo( m_Menus, static_cast<int>( std::floor( flTick ) ) );
	if ( nUpTo == 0 )
		return nullptr;
	const MenuShown_t &menu = m_Menus[ nUpTo - 1 ];
	if ( menu.m_nSlots == 0 && menu.m_szText.find_first_not_of( " \n" ) == std::string::npos )
		return nullptr;
	if ( menu.m_flSeconds >= 0.0 && ( flTick - menu.m_nTick ) * m_flTickInterval > menu.m_flSeconds )
		return nullptr;
	if ( menu.m_nPicked >= 0 && flTick >= menu.m_nPicked )
		return nullptr;
	return &menu;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const FeedLine_t *CFeed::LineAt( FeedKind_t nKind, double flTick ) const
{
	for ( std::size_t i = CountUpTo( m_Lines, static_cast<int>( std::floor( flTick ) ) ); i-- > 0; )
	{
		const FeedLine_t &line = m_Lines[ i ];
		if ( line.m_nKind != nKind )
			continue;
		return ( flTick - line.m_nTick ) * m_flTickInterval <= line.m_flSeconds ? &line : nullptr;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The fades since the last that purged the rest; the strongest cover of them.
//-----------------------------------------------------------------------------
std::uint32_t CFeed::FadeAt( double flTick ) const
{
	const std::size_t nUpTo = CountUpTo( m_Fades, static_cast<int>( std::floor( flTick ) ) );
	double flBest = 0.0;
	std::uint32_t nBest = 0;
	for ( std::size_t i = nUpTo; i-- > 0; )
	{
		const Fade_t &fade = m_Fades[ i ];
		const double flCover = fade.CoverAt( ( flTick - fade.m_nTick ) * m_flTickInterval ) * static_cast<double>( fade.m_nRgba & 0xff ) / 255.0;
		if ( flCover > flBest )
		{
			flBest = flCover;
			nBest = ( fade.m_nRgba & 0xffffff00 ) | static_cast<std::uint32_t>( std::lround( flCover * 255.0 ) );
		}
		if ( fade.m_nFlags & FADE_FLAG_PURGE )
			break;
	}
	return nBest;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Round_t *CFeed::RoundAt( int nTick ) const
{
	const Round_t *pRound = nullptr;
	for ( const Round_t &round : m_Rounds )
	{
		if ( round.m_nStart <= nTick )
			pRound = &round;
	}
	return pRound;
}

} // namespace xv::demo
