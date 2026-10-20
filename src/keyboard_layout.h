// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.keyboard.KeyboardLayout.
#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "tables.h"

namespace zcode {

class KeyboardLayout {
public:
    explicit KeyboardLayout(const Tables& tables);

    // KeyboardLayout.get(key, location): the shapes of the key's letters at
    // that location, in field declaration order. Empty for unknown keys.
    std::span<const ShapeId> get(std::string_view key, Location location) const {
        const Entry* e = find(key);
        return e ? std::span<const ShapeId>(e->at[static_cast<int>(location)]) : std::span<const ShapeId>();
    }

    // get(key, MIDDLE) followed by get(key, TAIL): what fuzzy() takes for the
    // last key.
    std::span<const ShapeId> get_middle_then_tail(std::string_view key) const {
        const Entry* e = find(key);
        return e ? std::span<const ShapeId>(e->middle_then_tail) : std::span<const ShapeId>();
    }

    // KeyboardLayout.getKeys()
    std::vector<std::string> keys() const;

private:
    // Precomputed per key, instead of Java's per-call reflection.
    struct Entry {
        std::string key;
        std::array<std::vector<ShapeId>, 3> at;  // indexed by Location
        std::vector<ShapeId> middle_then_tail;
    };

    const Entry* find(std::string_view key) const {
        if (key.size() == 1 && static_cast<unsigned char>(key[0]) < 128) {
            int i = single_char_[static_cast<unsigned char>(key[0])];
            return i < 0 ? nullptr : &entries_[i];
        }
        for (const Entry& e : entries_) {
            if (e.key == key) return &e;
        }
        return nullptr;
    }

    std::vector<Entry> entries_;
    std::array<int, 128> single_char_{};
};

}  // namespace zcode
