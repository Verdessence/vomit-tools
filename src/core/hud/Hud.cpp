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

//-----------------------------------------------------------------------------
// Purpose: Whether a [$...] condition holds on the PC build this previews: Windows, not a
//          console. Handles !, && and || without parentheses, which is what the files use.
//-----------------------------------------------------------------------------
bool ConditionHolds( std::string_view szCondition )
{
	if ( szCondition.starts_with( '[' ) )
		szCondition.remove_prefix( 1 );
	if ( szCondition.ends_with( ']' ) )
		szCondition.remove_suffix( 1 );
	auto term = []( std::string_view szTerm ) {
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
		const std::string szName = Lower( szTerm );
		const bool bTrue = szName == "$win32" || szName == "$windows" || szName == "$win64";
		return bTrue != bNot;
	};
	auto all = [ & ]( std::string_view szAnd ) {
		for ( std::size_t nAt = 0;; )
		{
			const std::size_t nNext = szAnd.find( "&&", nAt );
			if ( !term( szAnd.substr( nAt, nNext == std::string_view::npos ? std::string_view::npos : nNext - nAt ) ) )
				return false;
			if ( nNext == std::string_view::npos )
				return true;
			nAt = nNext + 2;
		}
	};
	for ( std::size_t nAt = 0;; )
	{
		const std::size_t nNext = szCondition.find( "||", nAt );
		if ( all( szCondition.substr( nAt, nNext == std::string_view::npos ? std::string_view::npos : nNext - nAt ) ) )
			return true;
		if ( nNext == std::string_view::npos )
			return false;
		nAt = nNext + 2;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Whether a node applies here (no condition, or one that holds).
//-----------------------------------------------------------------------------
bool Applies( const kv1::CNode &node )
{
	const auto condition = node.GetCondition();
	return !condition || ConditionHolds( *condition );
}

} // namespace

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
// Purpose: The Colors and BaseSettings blocks of a scheme file's Scheme block.
//-----------------------------------------------------------------------------
CScheme CScheme::FromDocument( const kv1::CDocument &document )
{
	CScheme scheme;
	const kv1::CNode *pScheme = document.Find( "Scheme" );
	if ( !pScheme || !pScheme->IsBlock() )
		return scheme;
	for ( const char *pszBlock : { "Colors", "BaseSettings" } )
	{
		for ( const kv1::CNode *pBlock : pScheme->FindAll( pszBlock ) )
		{
			if ( !pBlock->IsBlock() || !Applies( *pBlock ) )
				continue;
			for ( const kv1::CNode &entry : pBlock->GetChildren() )
			{
				if ( entry.IsPair() && Applies( entry ) )
					scheme.m_Colors[ Lower( entry.GetKey() ) ] = entry.GetValue();
			}
		}
	}
	return scheme;
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
	for ( std::size_t i = 0; i < children.size(); i++ )
	{
		const kv1::CNode &child = children[ i ];
		if ( !child.IsBlock() || !Applies( child ) )
			continue;
		Element_t element;
		element.m_nIndex = i;
		element.m_szName = child.GetKey();
		element.m_szControl = GetKey( child, "ControlName" ).value_or( std::string{} );
		element.m_Rect.m_nX = ResolvePos( ParseCoord( GetKey( child, "xpos" ).value_or( "0" ) ), screen.m_nWide, flScale );
		element.m_Rect.m_nY = ResolvePos( ParseCoord( GetKey( child, "ypos" ).value_or( "0" ) ), screen.m_nTall, flScale );
		element.m_Rect.m_nWide = ResolveSize( ParseCoord( GetKey( child, "wide" ).value_or( "0" ) ), screen.m_nWide, flScale );
		element.m_Rect.m_nTall = ResolveSize( ParseCoord( GetKey( child, "tall" ).value_or( "0" ) ), screen.m_nTall, flScale );
		element.m_nZ = static_cast<int>( LeadingNumber( GetKey( child, "zpos" ).value_or( "0" ) ) );
		element.m_bVisible = LeadingNumber( GetKey( child, "visible" ).value_or( "1" ) ) != 0.0;
		element.m_szLabel = GetKey( child, "labelText" ).value_or( std::string{} );
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
MovedPos_t MoveTo( const kv1::CNode &element, int nX, int nY, const Screen_t &screen )
{
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
	return { inverse( "xpos", nX, screen.m_nWide ), inverse( "ypos", nY, screen.m_nTall ) };
}

} // namespace xv::hud
