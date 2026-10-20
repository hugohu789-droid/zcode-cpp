// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.LetterSplicer.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "keyboard_layout.h"
#include "letter_shape_sequence.h"
#include "splice_table.h"
#include "tables.h"

namespace zcode {

// Java builds every candidate as a full copy of its prefix plus one shape;
// here candidates share an arena of nodes and a candidate is the path from a
// node back to a root.
struct SpliceNode {
    static constexpr std::uint32_t no_parent = 0xFFFFFFFF;
    std::uint32_t parent;
    std::uint32_t length;  // code points of the path ending here
    ShapeId shape;
    Nature nature;  // LetterShapeSequence.getNature() of the path ending here
};

// LetterShapeSequence.toString() of the path ending at `node`.
ZString materialize(const std::vector<SpliceNode>& nodes, std::uint32_t node, const Tables& tables);

// The candidates of one splice.
struct SpliceResult {
    std::vector<SpliceNode> nodes;
    std::vector<std::uint32_t> candidates;  // final layer, in Java's output order

    std::size_t size() const { return candidates.size(); }
    ZString to_zstring(std::size_t i, const Tables& tables) const { return materialize(nodes, candidates[i], tables); }
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

    // The shapes the Java loop takes for the key at `index` of `count` keys.
    std::span<const ShapeId> shapes_for(std::string_view key, std::size_t index, std::size_t count,
                                        bool fuzzy) const;

    // One iteration of the Java loop: extends the layer `current` (node
    // indices into `nodes`) by `shapes` into `next`. An empty `shapes` keeps
    // the layer (Java's `continue`). `started` tells whether an earlier key
    // had shapes; if not, sequences start from the empty one. Returns the
    // new `started`. (Deviation from the original: see letter_splicer.cpp.)
    template <bool Fuzzy>
    bool step(std::vector<SpliceNode>& nodes, const std::vector<std::uint32_t>& current, bool started,
              std::span<const ShapeId> shapes, std::vector<std::uint32_t>& next) const;

    // splice(...).size() without building candidates: the same loop over
    // per-state counts, since the rules see only (last shape, nature).
    template <bool Fuzzy>
    std::uint64_t count(std::span<const std::string> latin_sequence) const;

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
