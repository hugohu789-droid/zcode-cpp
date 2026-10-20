// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.filter.BTailKeyFilter.
#include "key_filter.h"

namespace zcode {

BTailKeyFilter::BTailKeyFilter(const Tables& tables) {
    for (std::size_t i = 0; i < tables.zcode_names.size(); ++i) {
        if (tables.zcode_names[i] == "b_banbvr_baga") b_banbvr_baga_ = tables.zcode_points[i];
        if (tables.zcode_names[i] == "b_banbvr_ih") b_banbvr_ih_ = tables.zcode_points[i];
    }
}

bool BTailKeyFilter::accept(std::u32string_view str) const {
    // Pattern.matches() must consume the whole input, so "[^XY]+[XY]$" holds
    // exactly when the string is at least two units long, ends in X or Y, and
    // contains no other X or Y. ([^XY] also matches line terminators.)
    auto is_b = [this](CodePoint c) { return c == b_banbvr_baga_ || c == b_banbvr_ih_; };
    if (str.size() < 2 || !is_b(str.back())) {
        return true;
    }
    for (std::size_t i = 0; i + 1 < str.size(); ++i) {
        if (is_b(str[i])) {
            return true;
        }
    }
    return false;
}

}  // namespace zcode
