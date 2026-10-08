#pragma once

// text into the protocol's fixed-size string fields. engine-agnostic.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace halfcraft
{
	/// utf-8 text into a NUL-terminated field, cut at a character boundary when it doesn't fit.
	template <std::size_t N>
	void copy_utf8(char (&field)[N], std::string_view text)
	{
		std::size_t bytes = std::min(text.size(), N - 1);
		while (bytes > 0 && bytes < text.size() && (static_cast<unsigned char>(text[bytes]) & 0xC0) == 0x80) {
			--bytes;  // text[bytes] continues a character: drop the start of it too
		}
		text.copy(field, bytes);
		field[bytes] = '\0';
	}
}
