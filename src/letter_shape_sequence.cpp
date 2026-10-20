// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.LetterShapeSequence and
// com.zorigt.ime.letter.NatureUtil.
#include "letter_shape_sequence.h"

#include "strings.h"

namespace zcode {

Nature nature_of(std::string_view latin_key) {
    if (is_blank(latin_key)) {
        return Nature::saarmag;
    }
    if (latin_key == "a" || latin_key == "o") {
        return Nature::chagh;
    }
    if (latin_key == "e" || latin_key == "u") {
        return Nature::hundii;
    }
    return Nature::saarmag;
}

void LetterShapeSequence::append(const LetterShape& ls) {
    Nature n = nature_of(ls.latin_key);
    if (n != nature_ && n != Nature::saarmag) {
        nature_ = n;
    }
    shapes_.push_back(ls.id);
}

void LetterShapeSequence::append(const LetterShapeSequence& other, const Tables& tables) {
    for (ShapeId id : other.shapes_) {
        append(tables.shapes[id]);
    }
}

ZString LetterShapeSequence::to_zstring(const Tables& tables) const {
    ZString s;
    for (ShapeId id : shapes_) {
        s += tables.shapes[id].shape;
    }
    return s;
}

}  // namespace zcode
