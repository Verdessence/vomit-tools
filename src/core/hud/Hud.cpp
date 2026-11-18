//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Hud.h. The position rules are VGUI's as its files use them: a plain number from
//          the start, "c" from the middle, "r" back from the end; sizes also "f" (full less)
//          and "p" (a fraction of the parent).
//
//===========================================================================//
#include "Hud.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>

#include "util/Numbers.h"

namespace xv::hud
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The number at the front of text, read leniently as the engine's atof does ("12px"
//          is 12, nothing is 0).
//-----------------------------------------------------------------------------
double LeadingNumber( std::string_view szText )
{
	while ( !szText.empty() && std::isspace( static_cast<unsigned char>( szText.front() ) ) )
		szText.remove_prefix( 1 );
	std::size_t nEnd = 0;
	if ( nEnd < szText.size() && ( szText[ nEnd ] == '-' || szText[ nEnd ] == '+' ) )
		nEnd++;
	while ( nEnd < szText.size() && ( std::isdigit( static_cast<unsigned char>( szText[ nEnd ] ) ) || szText[ nEnd ] == '.' ) )
		nEnd++;
	return ParseNumber<double>( szText.substr( 0, nEnd ) ).value_or( 0.0 );
}

//-----------------------------------------------------------------------------
// Purpose: Lower-case copy, for VGUI's case-insensitive names.
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut( szText );
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return szOut;
}

bool HoldsHere( std::string_view szCondition );

//-----------------------------------------------------------------------------
// Purpose: Whether a node applies here (no condition, or one that holds).
//-----------------------------------------------------------------------------
bool Applies( const kv1::CNode &node )
{
	const auto condition = node.GetCondition();
	return !condition || HoldsHere( *condition );
}

//-----------------------------------------------------------------------------
// Purpose: Whether a ControlName is a panel others are put in (a Frame, or a class of the
//          game's own: "CTeamMenu", "CCSClientScoreBoardDialog") and not one of VGUI's plain
//          controls.
//-----------------------------------------------------------------------------
bool IsDialogControl( std::string_view szControl )
{
	static constexpr std::string_view PLAIN[] = { "label", "button", "imagepanel", "scalableimagepanel", "panel", "editablepanel", "richtext", "html", "checkbutton", "radiobutton", "togglebutton", "combobox", "textentry", "divider", "listpanel", "sectionedlistpanel", "progressbar", "continuousprogressbar", "slider", "menu", "menubutton", "urllabel", "scrollbar", "bitmapimagepanel", "mouseoverpanelbutton", "ciconpanel", "cexlabel", "cexbutton", "cexrichtext", "ceximagebutton", "cbitmapimagepanel", "cmodelpanel", "circularprogressbar", "propertysheet", "propertypage", "tooltip" };
	if ( szControl.empty() )
		return false;
	const std::string szLower = Lower( szControl );
	return std::ranges::find( PLAIN, std::string_view{ szLower } ) == std::end( PLAIN );
}

} // namespace

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A condition's terms, with what joins them: pfnTerm gets each name (lower-case, with
//          its "$") and whether it is negated; pfnJoin, "&&" or "||" between two.
//-----------------------------------------------------------------------------
template <typename Term, typename Join>
void EachTerm( std::string_view szCondition, Term &&pfnTerm, Join &&pfnJoin )
{
	if ( szCondition.starts_with( '[' ) )
		szCondition.remove_prefix( 1 );
	if ( szCondition.ends_with( ']' ) )
		szCondition.remove_suffix( 1 );
	for ( std::size_t nAt = 0; nAt <= szCondition.size(); )
	{
		const std::size_t nAnd = szCondition.find( "&&", nAt );
		const std::size_t nOr = szCondition.find( "||", nAt );
		const std::size_t nNext = std::min( nAnd, nOr );
		std::string_view szTerm = szCondition.substr( nAt, nNext == std::string_view::npos ? std::string_view::npos : nNext - nAt );
		while ( !szTerm.empty() && szTerm.front() == ' ' )
			szTerm.remove_prefix( 1 );
		while ( !szTerm.empty() && szTerm.back() == ' ' )
			szTerm.remove_suffix( 1 );
		bool bNot = false;
		while ( szTerm.starts_with( '!' ) )
		{
			bNot = !bNot;
			szTerm.remove_prefix( 1 );
		}
		pfnTerm( Lower( szTerm ), bNot );
		if ( nNext == std::string_view::npos )
			break;
		pfnJoin( nNext == nAnd );
		nAt = nNext + 2;
	}
}

//-----------------------------------------------------------------------------
// Purpose: See ConditionHolds.
//-----------------------------------------------------------------------------
bool HoldsHere( std::string_view szCondition )
{
	return ConditionHolds( szCondition );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: "&&" binds before "||", without parentheses, which is what the files use.
//-----------------------------------------------------------------------------
bool ConditionHolds( std::string_view szCondition )
{
	bool bAny = false;
	bool bAll = true;
	EachTerm(
		szCondition,
		[ & ]( const std::string &szName, bool bNot ) {
#if defined( _WIN32 )
			const bool bTrue = szName == "$win32" || szName == "$windows" || szName == "$win64";
#elif defined( __APPLE__ )
			const bool bTrue = szName == "$posix" || szName == "$osx";
#else
			const bool bTrue = szName == "$posix" || szName == "$linux";
#endif
			bAll = bAll && ( bTrue != bNot );
		},
		[ & ]( bool bAnd ) {
			if ( !bAnd )
			{
				bAny = bAny || bAll;
				bAll = true;
			}
		} );
	return bAny || bAll;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string DescribeCondition( std::string_view szCondition )
{
	static constexpr std::pair<std::string_view, std::string_view> NAMES[] = { { "$win32", "Windows" }, { "$windows", "Windows" }, { "$win64", "Windows (64-bit)" }, { "$posix", "Linux and macOS" }, { "$linux", "Linux" }, { "$osx", "macOS" }, { "$x360", "Xbox 360" }, { "$ps3", "PlayStation 3" }, { "$gameconsole", "consoles" }, { "$steamdeck", "Steam Deck" } };
	std::string szOut;
	EachTerm(
		szCondition,
		[ & ]( const std::string &szName, bool bNot ) {
			const auto *pFound = std::ranges::find( NAMES, std::string_view{ szName }, &std::pair<std::string_view, std::string_view>::first );
			szOut += bNot ? "not " : "";
			szOut += pFound != std::end( NAMES ) ? std::string( pFound->second ) : szName;
		},
		[ & ]( bool bAnd ) { szOut += bAnd ? " and " : " or "; } );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Splits a value into its anchor prefix and its number.
//-----------------------------------------------------------------------------
Coord_t ParseCoord( std::string_view szText )
{
	while ( !szText.empty() && std::isspace( static_cast<unsigned char>( szText.front() ) ) )
		szText.remove_prefix( 1 );
	Coord_t coord;
	if ( !szText.empty() )
	{
		switch ( std::tolower( static_cast<unsigned char>( szText.front() ) ) )
		{
			case 'c':
				coord.m_nAnchor = Anchor_t::ANCHOR_CENTER;
				break;
			case 'r':
				coord.m_nAnchor = Anchor_t::ANCHOR_END;
				break;
			case 'f':
				coord.m_nAnchor = Anchor_t::ANCHOR_FULL;
				break;
			case 'p':
				coord.m_nAnchor = Anchor_t::ANCHOR_PARENT;
				break;
			default:
				break;
		}
		if ( coord.m_nAnchor != Anchor_t::ANCHOR_START )
			szText.remove_prefix( 1 );
	}
	coord.m_flValue = LeadingNumber( szText );
	return coord;
}

//-----------------------------------------------------------------------------
// Purpose: The text for a coordinate, its prefix kept; whole numbers without a decimal point.
//-----------------------------------------------------------------------------
std::string FormatCoord( const Coord_t &coord )
{
	std::string szNumber;
	if ( coord.m_flValue == std::round( coord.m_flValue ) )
	{
		szNumber = std::to_string( static_cast<long long>( std::llround( coord.m_flValue ) ) );
	}
	else
	{
		std::ostringstream stream;
		stream.imbue( std::locale::classic() );
		stream << coord.m_flValue;
		szNumber = stream.str();
	}
	switch ( coord.m_nAnchor )
	{
		case Anchor_t::ANCHOR_CENTER:
			return "c" + szNumber;
		case Anchor_t::ANCHOR_END:
			return "r" + szNumber;
		case Anchor_t::ANCHOR_FULL:
			return "f" + szNumber;
		case Anchor_t::ANCHOR_PARENT:
			return "p" + szNumber;
		default:
			return szNumber;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A position in pixels along one axis.
//-----------------------------------------------------------------------------
int ResolvePos( const Coord_t &coord, int nParent, double flScale )
{
	const int nScaled = static_cast<int>( std::lround( coord.m_flValue * flScale ) );
	switch ( coord.m_nAnchor )
	{
		case Anchor_t::ANCHOR_CENTER:
			return nParent / 2 + nScaled;
		case Anchor_t::ANCHOR_END:
			return nParent - nScaled;
		case Anchor_t::ANCHOR_PARENT:
			return static_cast<int>( std::lround( coord.m_flValue * nParent ) );
		default:
			return nScaled;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A size in pixels along one axis.
//-----------------------------------------------------------------------------
int ResolveSize( const Coord_t &coord, int nParent, double flScale )
{
	const int nScaled = static_cast<int>( std::lround( coord.m_flValue * flScale ) );
	switch ( coord.m_nAnchor )
	{
		case Anchor_t::ANCHOR_FULL:
			return nParent - nScaled;
		case Anchor_t::ANCHOR_PARENT:
			return static_cast<int>( std::lround( coord.m_flValue * nParent ) );
		default:
			return nScaled;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Three or four numbers 0-255.
//-----------------------------------------------------------------------------
std::optional<Color_t> ParseColor( std::string_view szText )
{
	int nValues[ 4 ] = { 255, 255, 255, 255 };
	int nCount = 0;
	std::size_t nAt = 0;
	while ( nCount < 4 )
	{
		while ( nAt < szText.size() && std::isspace( static_cast<unsigned char>( szText[ nAt ] ) ) )
			nAt++;
		if ( nAt >= szText.size() )
			break;
		std::size_t nEnd = nAt;
		while ( nEnd < szText.size() && !std::isspace( static_cast<unsigned char>( szText[ nEnd ] ) ) )
			nEnd++;
		const auto nValue = ParseNumber<double>( szText.substr( nAt, nEnd - nAt ) );
		if ( !nValue )
			return std::nullopt;
		nValues[ nCount++ ] = std::clamp( static_cast<int>( std::lround( *nValue ) ), 0, 255 );
		nAt = nEnd;
	}
	while ( nAt < szText.size() && std::isspace( static_cast<unsigned char>( szText[ nAt ] ) ) )
		nAt++;
	if ( nCount < 3 || nAt < szText.size() )
		return std::nullopt;
	return Color_t{ nValues[ 0 ], nValues[ 1 ], nValues[ 2 ], nValues[ 3 ] };
}

//-----------------------------------------------------------------------------
// Purpose: A scheme file's Scheme block: Colors and BaseSettings, Fonts, Borders and
//          CustomFontFiles (a path each, or a block with a "font" key).
//-----------------------------------------------------------------------------
CScheme CScheme::FromDocument( const kv1::CDocument &document )
{
	CScheme scheme;
	const kv1::CNode *pScheme = document.Find( "Scheme" );
	if ( !pScheme || !pScheme->IsBlock() )
		return scheme;
	for ( const char *pszBlock : { "Colors", "BaseSettings" } )
	{
		const bool bColors = std::string_view( pszBlock ) == "Colors";
		for ( const kv1::CNode *pBlock : pScheme->FindAll( pszBlock ) )
		{
			if ( !pBlock->IsBlock() || !Applies( *pBlock ) )
				continue;
			for ( const kv1::CNode &entry : pBlock->GetChildren() )
			{
				if ( !entry.IsPair() || !Applies( entry ) )
					continue;
				scheme.m_Colors[ Lower( entry.GetKey() ) ] = entry.GetValue();
				( bColors ? scheme.m_ColorList : scheme.m_SettingList ).push_back( { std::string( entry.GetKey() ), std::string( entry.GetValue() ) } );
			}
		}
	}
	for ( const kv1::CNode *pFonts : pScheme->FindAll( "Fonts" ) )
	{
		if ( !pFonts->IsBlock() || !Applies( *pFonts ) )
			continue;
		for ( const kv1::CNode &font : pFonts->GetChildren() )
		{
			if ( !font.IsBlock() || !Applies( font ) )
				continue;
			SchemeFont_t out;
			out.m_szName = font.GetKey();
			for ( const kv1::CNode &entry : font.GetChildren() )
			{
				if ( !entry.IsBlock() || !Applies( entry ) )
					continue;
				FontEntry_t face;
				face.m_szFace = GetKey( entry, "name" ).value_or( std::string{} );
				face.m_nTall = static_cast<int>( LeadingNumber( GetKey( entry, "tall" ).value_or( "0" ) ) );
				face.m_nWeight = static_cast<int>( LeadingNumber( GetKey( entry, "weight" ).value_or( "0" ) ) );
				face.m_bAntialias = LeadingNumber( GetKey( entry, "antialias" ).value_or( "0" ) ) != 0.0;
				face.m_bAdditive = LeadingNumber( GetKey( entry, "additive" ).value_or( "0" ) ) != 0.0;
				face.m_bItalic = LeadingNumber( GetKey( entry, "italic" ).value_or( "0" ) ) != 0.0;
				face.m_nBlur = static_cast<int>( LeadingNumber( GetKey( entry, "blur" ).value_or( "0" ) ) );
				face.m_nScanlines = static_cast<int>( LeadingNumber( GetKey( entry, "scanlines" ).value_or( "0" ) ) );
				if ( const auto szRange = GetKey( entry, "yres" ) )
				{
					std::istringstream stream( *szRange );
					stream.imbue( std::locale::classic() );
					int nFrom = 0;
					int nTo = 0;
					if ( stream >> nFrom >> nTo )
						face.m_YRes = std::pair{ nFrom, nTo };
				}
				out.m_Entries.push_back( std::move( face ) );
			}
			scheme.m_Fonts.push_back( std::move( out ) );
		}
	}
	// A border's keys by their path: its blocks are sides holding numbered lines.
	const auto flatten = []( const auto &self, const kv1::CNode &block, const std::string &szPath, std::vector<SchemeValue_t> &out ) -> void {
		for ( const kv1::CNode &child : block.GetChildren() )
		{
			if ( !Applies( child ) )
				continue;
			const std::string szChild = szPath.empty() ? std::string( child.GetKey() ) : szPath + "/" + std::string( child.GetKey() );
			if ( child.IsPair() )
				out.push_back( { szChild, std::string( child.GetValue() ) } );
			else if ( child.IsBlock() )
				self( self, child, szChild, out );
		}
	};
	for ( const kv1::CNode *pBorders : pScheme->FindAll( "Borders" ) )
	{
		if ( !pBorders->IsBlock() || !Applies( *pBorders ) )
			continue;
		for ( const kv1::CNode &border : pBorders->GetChildren() )
		{
			if ( !Applies( border ) )
				continue;
			SchemeBorder_t out;
			out.m_szName = border.GetKey();
			if ( border.IsPair() )
				out.m_Keys.push_back( { "same as", std::string( border.GetValue() ) } ); // "ButtonKeyFocusBorder" "ButtonBorder"
			else if ( border.IsBlock() )
				flatten( flatten, border, {}, out.m_Keys );
			scheme.m_Borders.push_back( std::move( out ) );
		}
	}
	for ( const kv1::CNode *pFiles : pScheme->FindAll( "CustomFontFiles" ) )
	{
		if ( !pFiles->IsBlock() || !Applies( *pFiles ) )
			continue;
		for ( const kv1::CNode &file : pFiles->GetChildren() )
		{
			if ( !Applies( file ) )
				continue;
			if ( file.IsPair() )
				scheme.m_FontFiles.emplace_back( file.GetValue() );
			else if ( const auto szFont = file.IsBlock() ? GetKey( file, "font" ) : std::nullopt )
				scheme.m_FontFiles.push_back( *szFont );
		}
	}
	return scheme;
}

//-----------------------------------------------------------------------------
// Purpose: The first face of the font that is for this screen's height. One given for a range
//          of heights is in real pixels; one for any screen is in base units, scaled with a
//          proportional layout.
//-----------------------------------------------------------------------------
std::optional<Font_t> CScheme::ResolveFont( std::string_view szName, const Screen_t &screen ) const
{
	const auto found = std::ranges::find_if( m_Fonts, [ & ]( const SchemeFont_t &font ) { return kv1::EqualsIgnoreCase( font.m_szName, szName ); } );
	if ( found == m_Fonts.end() )
		return std::nullopt;
	for ( const FontEntry_t &entry : found->m_Entries )
	{
		if ( entry.m_YRes && ( screen.m_nTall < entry.m_YRes->first || screen.m_nTall > entry.m_YRes->second ) )
			continue;
		Font_t font;
		font.m_szFace = entry.m_szFace;
		font.m_nPixels = entry.m_YRes ? entry.m_nTall : static_cast<int>( std::lround( entry.m_nTall * screen.GetScale() ) );
		font.m_nWeight = entry.m_nWeight;
		font.m_bAntialias = entry.m_bAntialias;
		font.m_bAdditive = entry.m_bAdditive;
		font.m_bItalic = entry.m_bItalic;
		return font;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Every block under a TextureData block, wherever the file keeps it.
//-----------------------------------------------------------------------------
std::map<std::string, Icon_t> ReadIcons( const kv1::CDocument &document )
{
	std::map<std::string, Icon_t> icons;
	const auto visit = [ & ]( const auto &self, const kv1::CNode &node ) -> void {
		if ( !node.IsBlock() || !Applies( node ) )
			return;
		if ( !kv1::EqualsIgnoreCase( node.GetKey(), "TextureData" ) )
		{
			for ( const kv1::CNode &child : node.GetChildren() )
				self( self, child );
			return;
		}
		for ( const kv1::CNode &entry : node.GetChildren() )
		{
			if ( !entry.IsBlock() || !Applies( entry ) )
				continue;
			Icon_t icon;
			icon.m_szFont = GetKey( entry, "font" ).value_or( std::string{} );
			icon.m_szCharacter = GetKey( entry, "character" ).value_or( std::string{} );
			icon.m_szFile = GetKey( entry, "file" ).value_or( std::string{} );
			icon.m_Rect.m_nX = static_cast<int>( LeadingNumber( GetKey( entry, "x" ).value_or( "0" ) ) );
			icon.m_Rect.m_nY = static_cast<int>( LeadingNumber( GetKey( entry, "y" ).value_or( "0" ) ) );
			icon.m_Rect.m_nWide = static_cast<int>( LeadingNumber( GetKey( entry, "width" ).value_or( "0" ) ) );
			icon.m_Rect.m_nTall = static_cast<int>( LeadingNumber( GetKey( entry, "height" ).value_or( "0" ) ) );
			icons[ Lower( entry.GetKey() ) ] = std::move( icon );
		}
	};
	for ( const kv1::CNode &root : document.m_Roots )
		visit( visit, root );
	return icons;
}

//-----------------------------------------------------------------------------
// Purpose: UTF-16 code units (pairs joined) to UTF-8.
//-----------------------------------------------------------------------------
std::string LanguageText( std::string_view szBytes )
{
	if ( szBytes.size() < 2 || static_cast<unsigned char>( szBytes[ 0 ] ) != 0xFF || static_cast<unsigned char>( szBytes[ 1 ] ) != 0xFE )
		return std::string( szBytes );
	std::string szOut;
	szOut.reserve( szBytes.size() / 2 );
	const auto unit = [ & ]( std::size_t i ) { return static_cast<std::uint32_t>( static_cast<unsigned char>( szBytes[ i ] ) ) | static_cast<std::uint32_t>( static_cast<unsigned char>( szBytes[ i + 1 ] ) ) << 8; };
	for ( std::size_t i = 2; i + 1 < szBytes.size(); i += 2 )
	{
		std::uint32_t nCode = unit( i );
		// A surrogate pair: the code point past the first plane.
		if ( nCode >= 0xD800 && nCode < 0xDC00 && i + 3 < szBytes.size() )
		{
			const std::uint32_t nLow = unit( i + 2 );
			if ( nLow >= 0xDC00 && nLow < 0xE000 )
			{
				nCode = 0x10000 + ( ( nCode - 0xD800 ) << 10 ) + ( nLow - 0xDC00 );
				i += 2;
			}
		}
		if ( nCode < 0x80 )
			szOut += static_cast<char>( nCode );
		else if ( nCode < 0x800 )
		{
			szOut += static_cast<char>( 0xC0 | nCode >> 6 );
			szOut += static_cast<char>( 0x80 | ( nCode & 0x3F ) );
		}
		else if ( nCode < 0x10000 )
		{
			szOut += static_cast<char>( 0xE0 | nCode >> 12 );
			szOut += static_cast<char>( 0x80 | ( ( nCode >> 6 ) & 0x3F ) );
			szOut += static_cast<char>( 0x80 | ( nCode & 0x3F ) );
		}
		else
		{
			szOut += static_cast<char>( 0xF0 | nCode >> 18 );
			szOut += static_cast<char>( 0x80 | ( ( nCode >> 12 ) & 0x3F ) );
			szOut += static_cast<char>( 0x80 | ( ( nCode >> 6 ) & 0x3F ) );
			szOut += static_cast<char>( 0x80 | ( nCode & 0x3F ) );
		}
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: The pairs of the lang block's Tokens block.
//-----------------------------------------------------------------------------
std::map<std::string, std::string> ReadTokens( const kv1::CDocument &document )
{
	std::map<std::string, std::string> tokens;
	for ( const kv1::CNode &root : document.m_Roots )
	{
		if ( !root.IsBlock() )
			continue;
		for ( const kv1::CNode *pTokens : root.FindAll( "Tokens" ) )
		{
			if ( !pTokens->IsBlock() )
				continue;
			for ( const kv1::CNode &token : pTokens->GetChildren() )
			{
				if ( token.IsPair() && Applies( token ) )
					tokens[ Lower( token.GetKey() ) ] = token.GetValue();
			}
		}
	}
	return tokens;
}

//-----------------------------------------------------------------------------
// Purpose: The elements whose contents are known well enough to show: the health, armour,
//          ammunition, timer and money panels of Half-Life 2 and Counter-Strike: Source.
//-----------------------------------------------------------------------------
const Sample_t *SampleFor( std::string_view szElement )
{
	struct Known_t
	{
		std::string_view m_szName;
		Sample_t m_Sample;
	};
	static constexpr Known_t KNOWN[] = {
		{ "HudHealth", { "health_icon", "100", {}, {}, "HEALTH" } },
		{ "HudArmor", { "shield_bright", "100", {}, {}, {} } },
		{ "HudSuit", { {}, "100", {}, {}, "SUIT" } },
		{ "HudRoundTimer", { "timer_icon", "1:55", {}, {}, {} } },
		{ "HudAmmo", { "ammo_556", "30", {}, "90", "AMMO" } },
		{ "HudAmmoSecondary", { {}, "2", {}, {}, "ALT" } },
		{ "HudAccount", { "dollar_sign", "800", {}, {}, {} } },
	};
	for ( const Known_t &known : KNOWN )
	{
		if ( kv1::EqualsIgnoreCase( known.m_szName, szElement ) )
			return &known.m_Sample;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: A literal colour, else a name followed through the scheme (a few steps at most,
//          so a loop can't hang).
//-----------------------------------------------------------------------------
std::optional<Color_t> CScheme::Resolve( std::string_view szValue ) const
{
	std::string szAt( szValue );
	for ( int nStep = 0; nStep < 8; nStep++ )
	{
		if ( const auto color = ParseColor( szAt ) )
			return color;
		const auto it = m_Colors.find( Lower( szAt ) );
		if ( it == m_Colors.end() )
			return std::nullopt;
		szAt = it->second;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: hudanimations.txt, line by line: "event Name", then in braces commands, of which
//          "Animate panel property value interpolator [its numbers] start duration" is the
//          one that sets something. The last two numbers say when it ends.
//-----------------------------------------------------------------------------
Animations_t ReadAnimations( std::string_view szText )
{
	// By panel and property: each value an event settles on, with how many events do.
	std::map<std::string, std::map<std::string, std::vector<std::pair<std::string, int>>>> settled;
	std::map<std::string, std::map<std::string, std::pair<std::string, double>>> event; // the event being read: value and when it ends
	const auto endEvent = [ & ] {
		for ( const auto &[ szPanel, properties ] : event )
		{
			for ( const auto &[ szProperty, last ] : properties )
			{
				auto &values = settled[ szPanel ][ szProperty ];
				const auto found = std::ranges::find( values, last.first, &std::pair<std::string, int>::first );
				if ( found != values.end() )
					found->second++;
				else
					values.emplace_back( last.first, 1 );
			}
		}
		event.clear();
	};
	std::size_t nAt = 0;
	while ( nAt < szText.size() )
	{
		std::size_t nEnd = szText.find( '\n', nAt );
		if ( nEnd == std::string_view::npos )
			nEnd = szText.size();
		std::string_view szLine = szText.substr( nAt, nEnd - nAt );
		nAt = nEnd + 1;
		// Words, quoted or bare, up to a comment.
		std::vector<std::string> words;
		for ( std::size_t n = 0; n < szLine.size(); )
		{
			const char ch = szLine[ n ];
			if ( std::isspace( static_cast<unsigned char>( ch ) ) )
			{
				n++;
			}
			else if ( ch == '/' && n + 1 < szLine.size() && szLine[ n + 1 ] == '/' )
			{
				break;
			}
			else if ( ch == '"' )
			{
				const std::size_t nClose = szLine.find( '"', n + 1 );
				words.emplace_back( szLine.substr( n + 1, ( nClose == std::string_view::npos ? szLine.size() : nClose ) - n - 1 ) );
				n = nClose == std::string_view::npos ? szLine.size() : nClose + 1;
			}
			else
			{
				std::size_t nStop = n;
				while ( nStop < szLine.size() && !std::isspace( static_cast<unsigned char>( szLine[ nStop ] ) ) && szLine[ nStop ] != '"' )
					nStop++;
				words.emplace_back( szLine.substr( n, nStop - n ) );
				n = nStop;
			}
		}
		if ( words.empty() )
			continue;
		const std::string szCommand = Lower( words[ 0 ] );
		if ( szCommand == "event" || szCommand == "}" )
		{
			endEvent();
		}
		else if ( szCommand == "animate" && words.size() >= 6 )
		{
			const double flEnds = LeadingNumber( words[ words.size() - 2 ] ) + LeadingNumber( words.back() );
			auto &last = event[ Lower( words[ 1 ] ) ][ Lower( words[ 2 ] ) ];
			if ( last.first.empty() || flEnds >= last.second )
				last = { words[ 3 ], flEnds };
		}
	}
	endEvent();
	Animations_t out;
	for ( const auto &[ szPanel, properties ] : settled )
	{
		for ( const auto &[ szProperty, values ] : properties )
		{
			const std::pair<std::string, int> *pBest = nullptr;
			for ( const auto &value : values )
			{
				// Faded right out is how a panel is hidden, not what it looks like.
				const auto color = ParseColor( value.first );
				if ( ( color && color->m_nA == 0 ) || value.first == "0" )
					continue;
				if ( !pBest || value.second > pBest->second )
					pBest = &value;
			}
			if ( pBest )
				out[ szPanel ][ szProperty ] = pBest->first;
		}
	}
	return out;
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Goes over a text's variables: "%name%", and "%s1" to "%s9". pfnVisit gets where
//          one is (the signs included) and its name.
//-----------------------------------------------------------------------------
template <typename Visit>
void EachVariable( std::string_view szText, Visit &&pfnVisit )
{
	for ( std::size_t n = 0; n < szText.size(); )
	{
		if ( szText[ n ] != '%' )
		{
			n++;
			continue;
		}
		std::size_t nStop = n + 1;
		while ( nStop < szText.size() && ( std::isalnum( static_cast<unsigned char>( szText[ nStop ] ) ) || szText[ nStop ] == '_' ) )
			nStop++;
		const std::string_view szName = szText.substr( n + 1, nStop - n - 1 );
		if ( !szName.empty() && nStop < szText.size() && szText[ nStop ] == '%' )
		{
			pfnVisit( n, nStop + 1 - n, szName );
			n = nStop + 1;
		}
		else if ( szName.size() >= 2 && szName[ 0 ] == 's' && std::isdigit( static_cast<unsigned char>( szName[ 1 ] ) ) )
		{
			pfnVisit( n, std::size_t{ 3 }, szName.substr( 0, 2 ) );
			n += 3;
		}
		else
		{
			n++;
		}
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> FindVariables( std::string_view szText )
{
	std::vector<std::string> names;
	EachVariable( szText, [ & ]( std::size_t, std::size_t, std::string_view szName ) {
		if ( std::ranges::none_of( names, [ & ]( const std::string &szHave ) { return kv1::EqualsIgnoreCase( szHave, szName ); } ) )
			names.emplace_back( szName );
	} );
	return names;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string ReplaceVariables( std::string_view szText, const std::map<std::string, std::string> &values )
{
	std::string szOut;
	std::size_t nDone = 0;
	EachVariable( szText, [ & ]( std::size_t nAt, std::size_t nLength, std::string_view szName ) {
		const auto found = values.find( Lower( szName ) );
		if ( found == values.end() )
			return;
		szOut += szText.substr( nDone, nAt - nDone );
		szOut += found->second;
		nDone = nAt + nLength;
	} );
	szOut += szText.substr( nDone );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: The first block at the top of the file.
//-----------------------------------------------------------------------------
const kv1::CNode *LayoutRoot( const kv1::CDocument &document )
{
	for ( const kv1::CNode &root : document.m_Roots )
	{
		if ( root.IsBlock() )
			return &root;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: See the const one.
//-----------------------------------------------------------------------------
kv1::CNode *LayoutRoot( kv1::CDocument &document )
{
	return const_cast<kv1::CNode *>( LayoutRoot( std::as_const( document ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: A root block with at least one child block that has a position or size.
//-----------------------------------------------------------------------------
bool IsLayout( const kv1::CDocument &document )
{
	const kv1::CNode *pRoot = LayoutRoot( document );
	if ( !pRoot )
		return false;
	return std::ranges::any_of( pRoot->GetChildren(), []( const kv1::CNode &child ) {
		return child.IsBlock() && ( child.Find( "xpos" ) || child.Find( "wide" ) || child.Find( "fieldName" ) );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: A root block named GameMenu whose blocks have labels.
//-----------------------------------------------------------------------------
bool IsGameMenu( const kv1::CDocument &document )
{
	const kv1::CNode *pRoot = LayoutRoot( document );
	if ( !pRoot || !kv1::EqualsIgnoreCase( pRoot->GetKey(), "GameMenu" ) )
		return false;
	return std::ranges::any_of( pRoot->GetChildren(), []( const kv1::CNode &child ) { return child.IsBlock() && child.Find( "label" ); } );
}

//-----------------------------------------------------------------------------
// Purpose: The items one under the other on the left, the last one's foot some two thirds
//          down the screen; those that show are given places, hidden ones none.
//-----------------------------------------------------------------------------
std::vector<Element_t> LayOutGameMenu( const kv1::CDocument &document, const Screen_t &screen, const CScheme *pScheme, bool bInGame )
{
	std::vector<Element_t> elements;
	const kv1::CNode *pRoot = LayoutRoot( document );
	if ( !pRoot )
		return elements;
	const double flScale = screen.GetScale();
	const auto setting = [ & ]( const char *pszName, double flDefault ) {
		if ( pScheme )
		{
			for ( const SchemeValue_t &value : pScheme->GetSettings() )
			{
				if ( kv1::EqualsIgnoreCase( value.m_szName, pszName ) )
					return LeadingNumber( value.m_szValue );
			}
		}
		return flDefault;
	};
	// Measured off the game at 2560x1440 with a MenuItemHeight of 15: lines 44 pixels apart,
	// one short of the height scaled.
	const int nLine = std::max( 8, static_cast<int>( std::lround( setting( "MainMenu.MenuItemHeight", 30.0 ) * flScale ) ) - 1 );
	const int nLeft = static_cast<int>( std::lround( setting( "MainMenu.Inset", 32.0 ) * flScale ) );
	const auto &children = pRoot->GetChildren();
	for ( std::size_t i = 0; i < children.size(); i++ )
	{
		const kv1::CNode &child = children[ i ];
		if ( !child.IsBlock() || !Applies( child ) )
			continue;
		Element_t element;
		element.m_nIndex = i;
		element.m_szName = child.GetKey();
		element.m_szControl = "MenuItem";
		element.m_bMenuItem = true;
		element.m_szLabel = GetKey( child, "label" ).value_or( std::string{} );
		element.m_szFont = "MenuLarge";
		element.m_szAlignment = "west";
		const bool bOnlyInGame = LeadingNumber( GetKey( child, "OnlyInGame" ).value_or( "0" ) ) != 0.0;
		const bool bNotInGame = LeadingNumber( GetKey( child, "notmulti" ).value_or( "0" ) ) != 0.0 && LeadingNumber( GetKey( child, "notsingle" ).value_or( "0" ) ) != 0.0;
		element.m_bVisible = bInGame ? !bNotInGame : !bOnlyInGame;
		if ( const auto color = pScheme ? pScheme->Resolve( "MainMenu.TextColor" ) : std::nullopt )
			element.m_FgColor = color;
		elements.push_back( std::move( element ) );
	}
	const auto nShown = static_cast<int>( std::ranges::count( elements, true, &Element_t::m_bVisible ) );
	// The last item's foot 335 of the screen's 480 units down, in the same capture (1006 of
	// 1440); items a loaded map adds go on top.
	const int nFoot = static_cast<int>( std::lround( screen.m_nTall * 335.0 / 480.0 ) ) + 1;
	int nRow = 0;
	for ( Element_t &element : elements )
	{
		if ( !element.m_bVisible )
			continue;
		element.m_Rect = { nLeft, nFoot - ( nShown - nRow ) * nLine, screen.m_nWide / 3, nLine };
		nRow++;
	}
	return elements;
}

//-----------------------------------------------------------------------------
// Purpose: The last applicable pair named key.
//-----------------------------------------------------------------------------
std::optional<std::string> GetKey( const kv1::CNode &element, std::string_view szKey )
{
	std::optional<std::string> value;
	for ( const kv1::CNode &child : element.GetChildren() )
	{
		if ( child.IsPair() && kv1::EqualsIgnoreCase( child.GetKey(), szKey ) && Applies( child ) )
			value = child.GetValue();
	}
	return value;
}

//-----------------------------------------------------------------------------
// Purpose: Each element block of the root, placed on the screen.
//-----------------------------------------------------------------------------
std::vector<Element_t> LayOut( const kv1::CDocument &document, const Screen_t &screen, const CScheme *pScheme )
{
	std::vector<Element_t> elements;
	const kv1::CNode *pRoot = LayoutRoot( document );
	if ( !pRoot )
		return elements;
	const double flScale = screen.GetScale();
	const auto &children = pRoot->GetChildren();
	// The panel the rest are in, once met: its place and size.
	std::optional<Rect_t> dialog;
	for ( std::size_t i = 0; i < children.size(); i++ )
	{
		const kv1::CNode &child = children[ i ];
		if ( !child.IsBlock() || !Applies( child ) )
			continue;
		Element_t element;
		element.m_nIndex = i;
		element.m_szName = child.GetKey();
		element.m_szControl = GetKey( child, "ControlName" ).value_or( std::string{} );
		// Anchors ("c", "r", "f") measure the screen even inside a panel, unless the control
		// asks for its parent's size: a scoreboard far larger than the screen still has its
		// "c-486" controls about the screen's middle.
		const bool bByParent = dialog && LeadingNumber( GetKey( child, "proportionalToParent" ).value_or( "0" ) ) == 1.0;
		const int nParentWide = bByParent ? dialog->m_nWide : screen.m_nWide;
		const int nParentTall = bByParent ? dialog->m_nTall : screen.m_nTall;
		element.m_Rect.m_nX = ResolvePos( ParseCoord( GetKey( child, "xpos" ).value_or( "0" ) ), nParentWide, flScale );
		element.m_Rect.m_nY = ResolvePos( ParseCoord( GetKey( child, "ypos" ).value_or( "0" ) ), nParentTall, flScale );
		element.m_Rect.m_nWide = ResolveSize( ParseCoord( GetKey( child, "wide" ).value_or( "0" ) ), nParentWide, flScale );
		element.m_Rect.m_nTall = ResolveSize( ParseCoord( GetKey( child, "tall" ).value_or( "0" ) ), nParentTall, flScale );
		if ( dialog )
		{
			element.m_Rect.m_nX += dialog->m_nX;
			element.m_Rect.m_nY += dialog->m_nY;
		}
		else if ( elements.empty() && IsDialogControl( element.m_szControl ) )
		{
			element.m_bDialog = true;
			// The game's own full-screen menus are laid out for a 4:3 screen and centred
			// on a wider one.
			const std::string szControl = Lower( element.m_szControl );
			element.m_bMenu = szControl.find( "menu" ) != std::string::npos;
			if ( element.m_bMenu && element.m_Rect.m_nX == 0 && element.m_Rect.m_nWide > 0 && element.m_Rect.m_nWide < screen.m_nWide )
				element.m_Rect.m_nX = ( screen.m_nWide - element.m_Rect.m_nWide ) / 2;
			dialog = element.m_Rect;
		}
		element.m_nZ = static_cast<int>( LeadingNumber( GetKey( child, "zpos" ).value_or( "0" ) ) );
		element.m_bVisible = LeadingNumber( GetKey( child, "visible" ).value_or( "1" ) ) != 0.0;
		element.m_szLabel = GetKey( child, "labelText" ).value_or( std::string{} );
		if ( const auto szValue = GetKey( child, "TextColor" ) )
			element.m_TextColor = pScheme ? pScheme->Resolve( *szValue ) : ParseColor( *szValue );
		if ( const auto szValue = GetKey( child, "IconColor" ) )
			element.m_IconColor = pScheme ? pScheme->Resolve( *szValue ) : ParseColor( *szValue );
		element.m_szTextFont = GetKey( child, "TextFont" ).value_or( std::string{} );
		element.m_szNumberFont = GetKey( child, "NumberFont" ).value_or( std::string{} );
		element.m_szFont = GetKey( child, "font" ).value_or( std::string{} );
		element.m_szAlignment = Lower( GetKey( child, "textAlignment" ).value_or( std::string{} ) );
		element.m_szImage = GetKey( child, "image" ).value_or( std::string{} );
		element.m_bScaleImage = LeadingNumber( GetKey( child, "scaleImage" ).value_or( "0" ) ) != 0.0;
		if ( const auto szType = GetKey( child, "PaintBackgroundType" ) )
			element.m_nPaintBackground = static_cast<int>( LeadingNumber( *szType ) );
		const auto part = [ & ]( const char *pszX, const char *pszY ) -> std::optional<std::pair<int, int>> {
			const auto szX = GetKey( child, pszX );
			const auto szY = GetKey( child, pszY );
			if ( !szX && !szY )
				return std::nullopt;
			return std::pair{ static_cast<int>( std::lround( LeadingNumber( szX.value_or( "0" ) ) * flScale ) ), static_cast<int>( std::lround( LeadingNumber( szY.value_or( "0" ) ) * flScale ) ) };
		};
		element.m_Icon = part( "icon_xpos", "icon_ypos" );
		element.m_Digit = part( "digit_xpos", "digit_ypos" );
		element.m_Icon2 = part( "icon2_xpos", "icon2_ypos" );
		element.m_Digit2 = part( "digit2_xpos", "digit2_ypos" );
		element.m_Text = part( "text_xpos", "text_ypos" );
		if ( const auto bar = part( "bar_xpos", "bar_ypos" ) )
		{
			element.m_Bar = Rect_t{ bar->first, bar->second, static_cast<int>( std::lround( LeadingNumber( GetKey( child, "bar_width" ).value_or( "0" ) ) * flScale ) ), static_cast<int>( std::lround( LeadingNumber( GetKey( child, "bar_height" ).value_or( "0" ) ) * flScale ) ) };
		}
		for ( const char *pszKey : { "bgcolor_override", "BgColor" } )
		{
			if ( element.m_BgColor )
				break;
			if ( const auto szValue = GetKey( child, pszKey ) )
				element.m_BgColor = pScheme ? pScheme->Resolve( *szValue ) : ParseColor( *szValue );
		}
		for ( const char *pszKey : { "fgcolor_override", "FgColor" } )
		{
			if ( element.m_FgColor )
				break;
			if ( const auto szValue = GetKey( child, pszKey ) )
				element.m_FgColor = pScheme ? pScheme->Resolve( *szValue ) : ParseColor( *szValue );
		}
		elements.push_back( std::move( element ) );
	}
	return elements;
}

//-----------------------------------------------------------------------------
// Purpose: The inverse of ResolvePos for each axis, in whole base units, keeping the anchor
//          ("c-50" stays centred, "r40" stays on the right).
//-----------------------------------------------------------------------------
MovedPos_t MoveTo( const kv1::CNode &element, int nX, int nY, const Screen_t &screen, const Rect_t *pParent )
{
	if ( pParent )
	{
		nX -= pParent->m_nX;
		nY -= pParent->m_nY;
	}
	const bool bByParent = pParent && LeadingNumber( GetKey( element, "proportionalToParent" ).value_or( "0" ) ) == 1.0;
	const int nParentWide = bByParent ? pParent->m_nWide : screen.m_nWide;
	const int nParentTall = bByParent ? pParent->m_nTall : screen.m_nTall;
	const double flScale = screen.GetScale();
	auto inverse = [ & ]( std::string_view szKey, int nPixel, int nParent ) {
		Coord_t coord = ParseCoord( GetKey( element, szKey ).value_or( "0" ) );
		switch ( coord.m_nAnchor )
		{
			case Anchor_t::ANCHOR_CENTER:
				coord.m_flValue = std::round( ( nPixel - nParent / 2 ) / flScale );
				break;
			case Anchor_t::ANCHOR_END:
				coord.m_flValue = std::round( ( nParent - nPixel ) / flScale );
				break;
			case Anchor_t::ANCHOR_PARENT:
				coord.m_flValue = nParent > 0 ? std::round( 1000.0 * nPixel / nParent ) / 1000.0 : 0.0;
				break;
			default:
				coord.m_nAnchor = Anchor_t::ANCHOR_START;
				coord.m_flValue = std::round( nPixel / flScale );
				break;
		}
		return FormatCoord( coord );
	};
	return { inverse( "xpos", nX, nParentWide ), inverse( "ypos", nY, nParentTall ) };
}

} // namespace xv::hud
