//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Reads numbers out of text like "(0 0 64) (64 0 64)" or "[1 0 0 0] 0.25" quickly. Map
//          files hold millions of them, so the common short decimals skip the general parser.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace xv
{

// Parses the whole of text as a double, accepting what ParseNumber<double> accepts and giving
// the same result. Plain decimals of up to 15 digits are converted directly (exactly, since both
// the digits and the power of ten fit a double); anything else goes to ParseNumber.
[[nodiscard]] std::optional<double> ParseDecimal( std::string_view szText );

class CScanner
{
public:
	explicit CScanner( std::string_view szText )
		: m_szText( szText )
	{
	}

	// Skips blanks, then ch if it comes next. False if something else does.
	bool Skip( char ch );
	// The next word (up to a blank or a bracket) as a number. Nothing is consumed on failure.
	[[nodiscard]] std::optional<double> Double();
	[[nodiscard]] std::optional<int> Int();
	// The next word, whatever it is; empty at the end.
	[[nodiscard]] std::string_view Word();
	// Only blanks are left.
	[[nodiscard]] bool IsAtEnd();
	[[nodiscard]] std::size_t GetPosition() const { return m_nPos; }

private:
	void SkipBlanks();
	[[nodiscard]] std::string_view PeekWord();

	std::string_view m_szText;
	std::size_t m_nPos = 0;
};

} // namespace xv
