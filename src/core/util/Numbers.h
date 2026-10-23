//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Parses the whole of text as a number, always with '.' as the decimal point (never the
//          user's locale, which Qt applies to the C library). Leading '+' is accepted, like the
//          engine's atof.
//
//===========================================================================//
#pragma once

#include <charconv>
#include <optional>
#include <string_view>
#include <type_traits>
#include <version>

#if !defined( __cpp_lib_to_chars )
#include <locale>
#include <sstream>
#include <string>
#endif

namespace xv
{

template <typename T>
[[nodiscard]] std::optional<T> ParseNumber( std::string_view szText )
{
	if ( szText.starts_with( '+' ) )
	{
		szText.remove_prefix( 1 );
	}
	if ( szText.empty() )
	{
		return std::nullopt;
	}
	T value{};
#if defined( __cpp_lib_to_chars )
	constexpr bool bUseFromChars = true;
#else
	// libc++ only gained floating-point from_chars in version 20 (llvm-mingw and Apple clang lag behind).
	constexpr bool bUseFromChars = std::is_integral_v<T>;
#endif
	if constexpr ( bUseFromChars )
	{
		const auto [ end, ec ] = std::from_chars( szText.data(), szText.data() + szText.size(), value );
		if ( ec != std::errc{} || end != szText.data() + szText.size() )
		{
			return std::nullopt;
		}
	}
#if !defined( __cpp_lib_to_chars )
	else
	{
		// from_chars rejects these; the stream would skip or accept them.
		if ( szText.front() == ' ' || szText.front() == '\t' || szText.front() == '+' )
		{
			return std::nullopt;
		}
		std::istringstream stream{ std::string{ szText } };
		stream.imbue( std::locale::classic() );
		stream >> value;
		if ( stream.fail() || stream.peek() != std::char_traits<char>::eof() )
		{
			return std::nullopt;
		}
	}
#endif
	return value;
}

} // namespace xv
