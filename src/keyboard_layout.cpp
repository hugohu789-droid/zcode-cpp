// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.keyboard.KeyboardLayout. The Java version finds a
// letter's shapes by reflection (getDeclaredFields()) on every call; here they
// are the rows of data/shapes.tsv, already in that order, grouped once per key
// and location when the layout is built.
#include "keyboard_layout.h"

namespace zcode {

KeyboardLayout::KeyboardLayout(const Tables& tables) {
    single_char_.fill(-1);
    for (const KeyEntry& k : tables.keys) {
        Entry* e = nullptr;
        for (Entry& existing : entries_) {
            if (existing.key == k.key) e = &existing;
        }
        if (!e) {
            entries_.push_back(Entry{k.key, {}, {}});
            e = &entries_.back();
        }
        for (ShapeId id : tables.letters[k.letter].shapes) {
            e->at[static_cast<int>(tables.shapes[id].location)].push_back(id);
        }
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        Entry& e = entries_[i];
        e.middle_then_tail = e.at[static_cast<int>(Location::middle)];
        e.middle_then_tail.insert(e.middle_then_tail.end(), e.at[static_cast<int>(Location::tail)].begin(),
                                  e.at[static_cast<int>(Location::tail)].end());
        if (e.key.size() == 1 && static_cast<unsigned char>(e.key[0]) < 128) {
            single_char_[static_cast<unsigned char>(e.key[0])] = static_cast<int>(i);
        }
    }
}

std::vector<std::string> KeyboardLayout::keys() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const Entry& e : entries_) {
        out.push_back(e.key);
    }
    return out;
}

}  // namespace zcode
