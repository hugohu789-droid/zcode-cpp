// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.keyboard.KeyboardLayout.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "tables.h"

namespace zcode {

class KeyboardLayout {
public:
    explicit KeyboardLayout(const Tables& tables) : tables_(tables) {}

    // KeyboardLayout.get(key, location): the shapes of the key's letters at
    // that location, in field declaration order. Empty for unknown keys.
    std::vector<ShapeId> get(std::string_view key, Location location) const;

    // KeyboardLayout.getKeys()
    std::vector<std::string> keys() const;

private:
    const Tables& tables_;
};

}  // namespace zcode
