//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See VmfValues.h.
//
//===========================================================================//
#include "VmfValues.h"

#include <algorithm>
#include <charconv>
#include <cmath>

#include "util/Scan.h"

namespace xv::map
{

//-----------------------------------------------------------------------------
// Purpose: "x y z", with or without brackets or parentheses around it.
//-----------------------------------------------------------------------------
std::optional<Point_t> ParsePoint( std::string_view szText )
{
	CScanner scanner{ szText };
	const bool bBracket = scanner.Skip( '[' );
	const bool bParen = !bBracket && scanner.Skip( '(' );
	Point_t p{};
	for ( double &flValue : p )
	{
		const auto value = scanner.Double();
		if ( !value )
		{
			return std::nullopt;
		}
		flValue = *value;
	}
	if ( ( bBracket && !scanner.Skip( ']' ) ) || ( bParen && !scanner.Skip( ')' ) ) || !scanner.IsAtEnd() )
	{
		return std::nullopt;
	}
	return p;
}

//-----------------------------------------------------------------------------
// Purpose: A side's plane: "(x y z) (x y z) (x y z)".
//-----------------------------------------------------------------------------
std::optional<std::array<Point_t, 3>> ParsePlane( std::string_view szText )
{
	CScanner scanner{ szText };
	std::array<Point_t, 3> points{};
	for ( Point_t &p : points )
	{
		if ( !scanner.Skip( '(' ) )
		{
			return std::nullopt;
		}
		for ( double &flValue : p )
		{
			const auto value = scanner.Double();
			if ( !value )
			{
				return std::nullopt;
			}
			flValue = *value;
		}
		if ( !scanner.Skip( ')' ) )
		{
			return std::nullopt;
		}
	}
	if ( !scanner.IsAtEnd() )
	{
		return std::nullopt;
	}
	return points;
}

//-----------------------------------------------------------------------------
// Purpose: A texture axis: "[x y z shift] scale".
//-----------------------------------------------------------------------------
std::optional<TextureAxis_t> ParseAxis( std::string_view szText )
{
	CScanner scanner{ szText };
	TextureAxis_t axis;
	if ( !scanner.Skip( '[' ) )
	{
		return std::nullopt;
	}
	for ( double &flValue : axis.m_Axis )
	{
		const auto value = scanner.Double();
		if ( !value )
		{
			return std::nullopt;
		}
		flValue = *value;
	}
	const auto flShift = scanner.Double();
	if ( !flShift || !scanner.Skip( ']' ) )
	{
		return std::nullopt;
	}
	const auto flScale = scanner.Double();
	if ( !flScale || !scanner.IsAtEnd() )
	{
		return std::nullopt;
	}
	axis.m_flShift = *flShift;
	axis.m_flScale = *flScale;
	return axis;
}

//-----------------------------------------------------------------------------
// Purpose: "r g b" (anything after the third number, such as a brightness, is ignored).
//-----------------------------------------------------------------------------
std::optional<Color_t> ParseColor( std::string_view szText )
{
	CScanner scanner{ szText };
	std::array<double, 3> values{};
	for ( double &flValue : values )
	{
		const auto value = scanner.Double();
		if ( !value || !std::isfinite( *value ) )
		{
			return std::nullopt;
		}
		flValue = std::clamp( *value, 0.0, 255.0 );
	}
	return Color_t{ static_cast<std::uint8_t>( values[ 0 ] ), static_cast<std::uint8_t>( values[ 1 ] ), static_cast<std::uint8_t>( values[ 2 ] ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool ParseNumbers( std::string_view szText, std::size_t nCount, std::vector<double> &out )
{
	out.clear();
	CScanner scanner{ szText };
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		const auto value = scanner.Double();
		if ( !value )
		{
			return false;
		}
		out.push_back( *value );
	}
	return scanner.IsAtEnd();
}

//-----------------------------------------------------------------------------
// Purpose: Rounded to millionths first, so sums like 0.1 + 64 don't write their last bits.
//-----------------------------------------------------------------------------
std::string FormatNumber( double flValue )
{
	if ( !std::isfinite( flValue ) )
	{
		return "0";
	}
	double flRounded = std::round( flValue * 1e6 ) / 1e6;
	if ( flRounded == 0.0 )
	{
		flRounded = 0.0; // no "-0"
	}
	if ( flRounded == std::floor( flRounded ) && std::abs( flRounded ) < 1e15 )
	{
		return std::to_string( static_cast<long long>( flRounded ) );
	}
	char buffer[ 64 ];
	const auto result = std::to_chars( buffer, buffer + sizeof buffer, flRounded, std::chars_format::fixed, 6 );
	std::string szText( buffer, result.ptr );
	while ( szText.back() == '0' )
	{
		szText.pop_back();
	}
	if ( szText.back() == '.' )
	{
		szText.pop_back();
	}
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string FormatPoint( const Point_t &p )
{
	return FormatNumber( p[ 0 ] ) + ' ' + FormatNumber( p[ 1 ] ) + ' ' + FormatNumber( p[ 2 ] );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string FormatPlane( const std::array<Point_t, 3> &plane )
{
	return '(' + FormatPoint( plane[ 0 ] ) + ") (" + FormatPoint( plane[ 1 ] ) + ") (" + FormatPoint( plane[ 2 ] ) + ')';
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string FormatAxis( const TextureAxis_t &axis )
{
	return '[' + FormatPoint( axis.m_Axis ) + ' ' + FormatNumber( axis.m_flShift ) + "] " + FormatNumber( axis.m_flScale );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string FormatColor( Color_t color )
{
	return std::to_string( color.m_nR ) + ' ' + std::to_string( color.m_nG ) + ' ' + std::to_string( color.m_nB );
}

} // namespace xv::map
