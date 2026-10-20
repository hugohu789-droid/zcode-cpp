// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.LetterShapeSequence and
// com.zorigt.ime.letter.NatureUtil.
#pragma once

#include <string_view>
#include <vector>

#include "tables.h"

namespace zcode {

// NatureUtil.getNature(): a/o -> CHAGH, e/u -> HUNDII, anything else -> SAARMAG.
Nature nature_of(std::string_view latin_key);

class LetterShapeSequence {
public:
    // append(LetterShape): the sequence's nature becomes the shape's nature
    // unless that is SAARMAG (setNature()).
    void append(const LetterShape& ls);

    // append(LetterShapeSequence): re-appends every shape of `other`.
    void append(const LetterShapeSequence& other, const Tables& tables);

    Nature nature() const { return nature_; }
    bool empty() const { return shapes_.empty(); }
    std::size_t size() const { return shapes_.size(); }
    ShapeId last() const { return shapes_.back(); }  // getLast(); precondition: !empty()
    const std::vector<ShapeId>& shapes() const { return shapes_; }

    // toString(): the concatenated ZCode of all shapes.
    ZString to_zstring(const Tables& tables) const;

private:
    std::vector<ShapeId> shapes_;
    Nature nature_ = Nature::saarmag;
};

}  // namespace zcode
