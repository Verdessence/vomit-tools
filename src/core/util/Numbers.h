// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <optional>
#include <string_view>
#include <type_traits>
#include <version>

#if !defined(__cpp_lib_to_chars)
#include <locale>
#include <sstream>
#include <string>
#endif

namespace xv {

// Parses the whole of text as a number, always with '.' as the decimal point (never the user's
// locale, which Qt applies to the C library). Leading '+' is accepted, like the engine's atof.
template<typename T>
[[nodiscard]] std::optional<T> parseNumber(std::string_view text) {
	if (text.starts_with('+')) {
		text.remove_prefix(1);
	}
	if (text.empty()) {
		return std::nullopt;
	}
	T value{};
#if defined(__cpp_lib_to_chars)
	constexpr bool useFromChars = true;
#else
	// libc++ only gained floating-point from_chars in version 20 (llvm-mingw and Apple clang lag behind).
	constexpr bool useFromChars = std::is_integral_v<T>;
#endif
	if constexpr (useFromChars) {
		const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (ec != std::errc{} || end != text.data() + text.size()) {
			return std::nullopt;
		}
	}
#if !defined(__cpp_lib_to_chars)
	else {
		// from_chars rejects these; the stream would skip or accept them.
		if (text.front() == ' ' || text.front() == '\t' || text.front() == '+') {
			return std::nullopt;
		}
		std::istringstream stream{std::string{text}};
		stream.imbue(std::locale::classic());
		stream >> value;
		if (stream.fail() || stream.peek() != std::char_traits<char>::eof()) {
			return std::nullopt;
		}
	}
#endif
	return value;
}

} // namespace xv
