//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A pull lexer for KeyValues1 that hands out tokens as views into the text, for files
//          too big for Kv1.h's document (a map can be tens of megabytes). It splits text exactly
//          as Kv1.h's Parse does; the tests hold the two together.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "Kv1.h"

namespace xv::kv1
{

struct Token_t
{
	TokenKind_t m_nKind = TokenKind_t::TOKEN_KIND_UNQUOTED;
	std::string_view m_szRaw;     // the token's bytes, quotes included
	std::size_t m_nOffset = 0;    // where m_szRaw starts in the text
	bool m_bUnterminated = false; // a quoted string that ran into the end of the text

	// The token without its quotes. Escape sequences are not decoded.
	[[nodiscard]] std::string_view GetText() const;
};

class CLexer
{
public:
	explicit CLexer( std::string_view szText, bool bEscapeSequences = false );

	// The next token; false at the end of the text.
	bool Next( Token_t &token );
	// Where the lexer has got to: just past the last token.
	[[nodiscard]] std::size_t GetOffset() const { return m_nPos; }
	[[nodiscard]] std::string_view GetText() const { return m_szText; }

private:
	std::string_view m_szText;
	std::size_t m_nPos = 0;
	bool m_bEscapeSequences = false;
};

// Turns byte offsets into 1-based lines and columns (counted in bytes).
class CLineTable
{
public:
	explicit CLineTable( std::string_view szText );

	[[nodiscard]] std::pair<std::size_t, std::size_t> LineColumn( std::size_t nOffset ) const;

private:
	std::vector<std::size_t> m_Starts;
};

} // namespace xv::kv1
