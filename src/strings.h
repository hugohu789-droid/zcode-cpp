// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.common.Strings (only what the engine uses).
#pragma once

#include <string_view>

namespace zcode {

// Strings.isBlank(): String.trim() strips every UTF-16 unit <= U+0020, so a
// string is blank when all of its units are <= U+0020. On UTF-8 bytes the same
// test gives the same answer: bytes of multi-byte sequences are >= 0x80.
inline bool is_blank(std::string_view s) {
    for (char c : s) {
        if (static_cast<unsigned char>(c) > 0x20) {
            return false;
        }
    }
    return true;
}

}  // namespace zcode
