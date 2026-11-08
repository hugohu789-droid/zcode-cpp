// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.common.Strings (only what the engine uses).
//
// Named common_strings.h and not strings.h: the test target puts `src` on
// the include path for the white-box tests, and POSIX has a <strings.h>.
// A file called strings.h here therefore answered GoogleTest's
// `#include <strings.h>`, which hid strcasecmp and stopped the suite
// compiling on Linux at all. Windows never showed it -- there is no POSIX
// <strings.h> there, so GoogleTest takes another branch and never asks.
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
