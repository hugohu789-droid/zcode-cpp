// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.LetterSplicer.
#pragma once

#include <span>
#include <string>
#include <vector>

#include "keyboard_layout.h"
#include "letter_shape_sequence.h"
#include "tables.h"

namespace zcode {

class LetterSplicer {
public:
    LetterSplicer(const Tables& tables, const KeyboardLayout& layout) : tables_(tables), layout_(layout) {}

    // LetterSplicer.fuzzy(latinSequence): first key HEAD, middle keys MIDDLE,
    // last key MIDDLE then TAIL; fuzzy rules.
    std::vector<LetterShapeSequence> fuzzy(std::span<const std::string> latin_sequence) const;

    // LetterSplicer.severe(latinSequence): first key HEAD, middle keys MIDDLE,
    // last key TAIL; severe rules.
    std::vector<LetterShapeSequence> severe(std::span<const std::string> latin_sequence) const;

private:
    template <bool Fuzzy>
    std::vector<LetterShapeSequence> splice(std::span<const std::string> latin_sequence) const;

    const Tables& tables_;
    const KeyboardLayout& layout_;
};

}  // namespace zcode
