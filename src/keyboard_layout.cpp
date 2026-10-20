// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.keyboard.KeyboardLayout. The Java version finds a
// letter's shapes by reflection (getDeclaredFields()); here they are the rows
// of data/shapes.tsv, already in that order.
#include "keyboard_layout.h"

namespace zcode {

std::vector<ShapeId> KeyboardLayout::get(std::string_view key, Location location) const {
    std::vector<ShapeId> out;
    for (const KeyEntry& entry : tables_.keys) {
        if (entry.key != key) {
            continue;
        }
        for (ShapeId id : tables_.letters[entry.letter].shapes) {
            if (tables_.shapes[id].location == location) {
                out.push_back(id);
            }
        }
    }
    return out;
}

std::vector<std::string> KeyboardLayout::keys() const {
    std::vector<std::string> out;
    out.reserve(tables_.keys.size());
    for (const KeyEntry& entry : tables_.keys) {
        out.push_back(entry.key);
    }
    return out;
}

}  // namespace zcode
