// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.LetterSplicer.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "keyboard_layout.h"
#include "letter_shape_sequence.h"
#include "splice_table.h"
#include "tables.h"

namespace zcode {

// The candidates of one splice. Java builds every candidate as a full copy of
// its prefix plus one shape; here all candidates share one arena of
// {parent, shape} nodes and a candidate is a path back to a root.
struct SpliceResult {
    static constexpr std::uint32_t no_parent = 0xFFFFFFFF;
    struct Node {
        std::uint32_t parent;
        std::uint32_t length;  // code points of the path ending here
        ShapeId shape;
        Nature nature;  // LetterShapeSequence.getNature() of the path ending here
    };
    std::vector<Node> nodes;
    std::vector<std::uint32_t> candidates;  // final layer, in Java's output order

    std::size_t size() const { return candidates.size(); }
    // LetterShapeSequence.toString() of candidate i.
    ZString to_zstring(std::size_t i, const Tables& tables) const;
};

class LetterSplicer {
public:
    LetterSplicer(const Tables& tables, const KeyboardLayout& layout, const SpliceTable& verifier);

    // LetterSplicer.fuzzy(latinSequence): first key HEAD, middle keys MIDDLE,
    // last key MIDDLE then TAIL; fuzzy rules.
    SpliceResult fuzzy(std::span<const std::string> latin_sequence) const;

    // LetterSplicer.severe(latinSequence): first key HEAD, middle keys MIDDLE,
    // last key TAIL; severe rules.
    SpliceResult severe(std::span<const std::string> latin_sequence) const;

private:
    template <bool Fuzzy>
    SpliceResult splice(std::span<const std::string> latin_sequence) const;

    const Tables& tables_;
    const KeyboardLayout& layout_;
    const SpliceTable& verifier_;
    std::vector<Nature> shape_nature_;         // NatureUtil.getNature(latinKey) per shape
    std::vector<std::uint32_t> shape_length_;  // code points per shape
};

}  // namespace zcode
