//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Lexer.h.
//
//===========================================================================//
#include "Lexer.h"

#include <algorithm>
#include <cstring>

namespace xv::kv1
{

namespace
{

constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";

//-----------------------------------------------------------------------------
// Purpose: The same blanks Kv1.cpp skips.
//-----------------------------------------------------------------------------
bool IsSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view Token_t::GetText() const
{
	std::string_view szText = m_szRaw;
	if ( m_nKind == TokenKind_t::TOKEN_KIND_QUOTED )
	{
		szText.remove_prefix( 1 );
		if ( !m_bUnterminated && !szText.empty() )
		{
			szText.remove_suffix( 1 );
		}
	}
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CLexer::CLexer( std::string_view szText, bool bEscapeSequences )
	: m_szText( szText ),
	  m_bEscapeSequences( bEscapeSequences )
{
	if ( m_szText.starts_with( UTF8_BOM ) )
	{
		m_nPos = UTF8_BOM.size();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Skips blanks and // comments, then reads one token by Kv1.cpp's rules.
//-----------------------------------------------------------------------------
bool CLexer::Next( Token_t &token )
{
	const char *pText = m_szText.data();
	const std::size_t nSize = m_szText.size();
	std::size_t nPos = m_nPos;
	while ( nPos < nSize )
	{
		if ( IsSpace( pText[ nPos ] ) )
		{
			nPos++;
		}
		else if ( pText[ nPos ] == '/' && nPos + 1 < nSize && pText[ nPos + 1 ] == '/' )
		{
			const void *pEnd = std::memchr( pText + nPos, '\n', nSize - nPos );
			nPos = pEnd ? static_cast<std::size_t>( static_cast<const char *>( pEnd ) - pText ) : nSize;
		}
		else
		{
			break;
		}
	}
	if ( nPos >= nSize )
	{
		m_nPos = nSize;
		return false;
	}

	const std::size_t nStart = nPos;
	token.m_nOffset = nStart;
	token.m_bUnterminated = false;
	switch ( pText[ nPos ] )
	{
		case '{':
			token.m_nKind = TokenKind_t::TOKEN_KIND_OPEN_BRACE;
			nPos++;
			break;
		case '}':
			token.m_nKind = TokenKind_t::TOKEN_KIND_CLOSE_BRACE;
			nPos++;
			break;
		case '"':
		{
			token.m_nKind = TokenKind_t::TOKEN_KIND_QUOTED;
			nPos++;
			if ( !m_bEscapeSequences )
			{
				const void *pEnd = std::memchr( pText + nPos, '"', nSize - nPos );
				if ( pEnd )
				{
					nPos = static_cast<std::size_t>( static_cast<const char *>( pEnd ) - pText ) + 1;
				}
				else
				{
					nPos = nSize;
					token.m_bUnterminated = true;
				}
				break;
			}
			token.m_bUnterminated = true;
			while ( nPos < nSize )
			{
				if ( pText[ nPos ] == '\\' && nPos + 1 < nSize )
				{
					nPos += 2;
					continue;
				}
				if ( pText[ nPos++ ] == '"' )
				{
					token.m_bUnterminated = false;
					break;
				}
			}
			break;
		}
		case '[':
			token.m_nKind = TokenKind_t::TOKEN_KIND_CONDITIONAL;
			while ( nPos < nSize && pText[ nPos ] != '\n' && pText[ nPos++ ] != ']' )
			{
			}
			break;
		default:
			token.m_nKind = TokenKind_t::TOKEN_KIND_UNQUOTED;
			while ( nPos < nSize && !IsSpace( pText[ nPos ] ) && pText[ nPos ] != '"' && pText[ nPos ] != '{' && pText[ nPos ] != '}' )
			{
				nPos++;
			}
			break;
	}
	token.m_szRaw = m_szText.substr( nStart, nPos - nStart );
	m_nPos = nPos;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CLineTable::CLineTable( std::string_view szText )
{
	m_Starts.push_back( 0 );
	const char *pBegin = szText.data();
	const char *pEnd = pBegin + szText.size();
	for ( const char *p = pBegin; p < pEnd; p++ )
	{
		p = static_cast<const char *>( std::memchr( p, '\n', static_cast<std::size_t>( pEnd - p ) ) );
		if ( !p )
		{
			break;
		}
		m_Starts.push_back( static_cast<std::size_t>( p - pBegin ) + 1 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::pair<std::size_t, std::size_t> CLineTable::LineColumn( std::size_t nOffset ) const
{
	const auto it = std::ranges::upper_bound( m_Starts, nOffset );
	const auto nLine = static_cast<std::size_t>( it - m_Starts.begin() );
	return { nLine, nOffset - m_Starts[ nLine - 1 ] + 1 };
}

} // namespace xv::kv1
