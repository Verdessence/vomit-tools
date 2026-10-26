//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Scan.h.
//
//===========================================================================//
#include "Scan.h"

#include <array>
#include <cstdint>

#include "Numbers.h"

namespace xv
{

namespace
{

// Every power of ten a double holds exactly.
constexpr std::array<double, 23> EXACT_POWERS = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };

//-----------------------------------------------------------------------------
// Purpose: Whether ch ends a word: blanks and the brackets map files wrap numbers in.
//-----------------------------------------------------------------------------
bool IsBreak( char ch )
{
	switch ( ch )
	{
		case ' ':
		case '\t':
		case '\r':
		case '\n':
		case '\v':
		case '\f':
		case '(':
		case ')':
		case '[':
		case ']':
		case '{':
		case '}':
			return true;
		default:
			return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Whether ch is a blank.
//-----------------------------------------------------------------------------
bool IsBlank( char ch )
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v' || ch == '\f';
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Converts sign, up to 15 digits and an exponent within 22 exactly; else asks
//          ParseNumber, which gives the same answer more slowly.
//-----------------------------------------------------------------------------
std::optional<double> ParseDecimal( std::string_view szText )
{
	std::size_t i = 0;
	bool bNegative = false;
	if ( i < szText.size() && ( szText[ i ] == '-' || szText[ i ] == '+' ) )
	{
		bNegative = szText[ i ] == '-';
		i++;
	}
	std::uint64_t nMantissa = 0;
	int nDigits = 0;      // significant digits kept in the mantissa
	int nExponent = 0;    // power of ten to apply to the mantissa
	bool bAnyDigit = false;
	for ( ; i < szText.size() && szText[ i ] >= '0' && szText[ i ] <= '9'; i++ )
	{
		bAnyDigit = true;
		if ( nMantissa == 0 && szText[ i ] == '0' )
		{
			continue;
		}
		nMantissa = nMantissa * 10 + static_cast<std::uint64_t>( szText[ i ] - '0' );
		nDigits++;
	}
	if ( i < szText.size() && szText[ i ] == '.' )
	{
		for ( i++; i < szText.size() && szText[ i ] >= '0' && szText[ i ] <= '9'; i++ )
		{
			bAnyDigit = true;
			nExponent--;
			if ( nMantissa == 0 && szText[ i ] == '0' )
			{
				continue;
			}
			nMantissa = nMantissa * 10 + static_cast<std::uint64_t>( szText[ i ] - '0' );
			nDigits++;
			if ( nDigits > 15 )
			{
				return ParseNumber<double>( szText );
			}
		}
	}
	if ( !bAnyDigit || nDigits > 15 )
	{
		return ParseNumber<double>( szText );
	}
	if ( i < szText.size() && ( szText[ i ] == 'e' || szText[ i ] == 'E' ) )
	{
		i++;
		bool bNegativePower = false;
		if ( i < szText.size() && ( szText[ i ] == '-' || szText[ i ] == '+' ) )
		{
			bNegativePower = szText[ i ] == '-';
			i++;
		}
		int nPower = 0;
		bool bAnyPowerDigit = false;
		for ( ; i < szText.size() && szText[ i ] >= '0' && szText[ i ] <= '9'; i++ )
		{
			bAnyPowerDigit = true;
			if ( nPower > 1000 )
			{
				return ParseNumber<double>( szText );
			}
			nPower = nPower * 10 + ( szText[ i ] - '0' );
		}
		if ( !bAnyPowerDigit )
		{
			return ParseNumber<double>( szText );
		}
		nExponent += bNegativePower ? -nPower : nPower;
	}
	if ( i != szText.size() )
	{
		return ParseNumber<double>( szText );
	}
	double flValue = static_cast<double>( nMantissa );
	if ( nMantissa != 0 )
	{
		if ( nExponent < -22 || nExponent > 22 )
		{
			return ParseNumber<double>( szText );
		}
		flValue = nExponent < 0 ? flValue / EXACT_POWERS[ static_cast<std::size_t>( -nExponent ) ] : flValue * EXACT_POWERS[ static_cast<std::size_t>( nExponent ) ];
	}
	return bNegative ? -flValue : flValue;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CScanner::SkipBlanks()
{
	while ( m_nPos < m_szText.size() && IsBlank( m_szText[ m_nPos ] ) )
	{
		m_nPos++;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The word at the current position, without consuming it.
//-----------------------------------------------------------------------------
std::string_view CScanner::PeekWord()
{
	SkipBlanks();
	std::size_t nEnd = m_nPos;
	while ( nEnd < m_szText.size() && !IsBreak( m_szText[ nEnd ] ) )
	{
		nEnd++;
	}
	return m_szText.substr( m_nPos, nEnd - m_nPos );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CScanner::Skip( char ch )
{
	SkipBlanks();
	if ( m_nPos < m_szText.size() && m_szText[ m_nPos ] == ch )
	{
		m_nPos++;
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<double> CScanner::Double()
{
	const std::string_view szWord = PeekWord();
	std::optional<double> value = ParseDecimal( szWord );
	if ( value )
	{
		m_nPos += szWord.size();
	}
	return value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<int> CScanner::Int()
{
	const std::string_view szWord = PeekWord();
	std::optional<int> value = ParseNumber<int>( szWord );
	if ( value )
	{
		m_nPos += szWord.size();
	}
	return value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view CScanner::Word()
{
	const std::string_view szWord = PeekWord();
	m_nPos += szWord.size();
	return szWord;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CScanner::IsAtEnd()
{
	SkipBlanks();
	return m_nPos >= m_szText.size();
}

} // namespace xv
