// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.filter.KeyFilter, KeyFilterFactory and
// BTailKeyFilter.
#pragma once

#include <string_view>

#include "tables.h"

namespace zcode {

// KeyFilter
class KeyFilter {
public:
    virtual ~KeyFilter() = default;
    virtual std::string_view key() const = 0;
    virtual bool accept(std::u32string_view str) const = 0;
};

// BTailKeyFilter: rejects a word whose only b_banbvr_baga / b_banbvr_ih is its
// last code point. Java: !Pattern.matches("[^XY]+[XY]$", str).
class BTailKeyFilter final : public KeyFilter {
public:
    explicit BTailKeyFilter(const Tables& tables);
    std::string_view key() const override { return "b-tail"; }
    bool accept(std::u32string_view str) const override;

private:
    CodePoint b_banbvr_baga_ = 0;
    CodePoint b_banbvr_ih_ = 0;
};

// KeyFilterFactory
class KeyFilterFactory {
public:
    explicit KeyFilterFactory(const Tables& tables) : b_tail_(tables) {}

    // KeyFilterFactory.get(key): nullptr when no filter is registered.
    const KeyFilter* get(std::string_view key) const { return key == b_tail_.key() ? &b_tail_ : nullptr; }

private:
    BTailKeyFilter b_tail_;
};

}  // namespace zcode
