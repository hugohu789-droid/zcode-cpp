// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.LetterSplicer. fuzzy() and severe()
// are two copies of one loop in Java; they differ only in the shapes taken for
// the last key and in the Verifier method, so they share one template here.
// The per-rejection logging and timing of the original are not ported.
// Verifier checks go through the precomputed SpliceTable, and candidates are
// paths in a shared arena instead of copied sequences (see SpliceResult).
#include "letter_splicer.h"

#include <algorithm>

namespace zcode {

ZString SpliceResult::to_zstring(std::size_t i, const Tables& tables) const {
    // One allocation of the exact size, filled from the end of the path.
    ZString s(nodes[candidates[i]].length, U'\0');
    std::size_t pos = s.size();
    for (std::uint32_t n = candidates[i]; n != no_parent; n = nodes[n].parent) {
        const ZString& part = tables.shapes[nodes[n].shape].shape;
        pos -= part.size();
        std::copy(part.begin(), part.end(), s.begin() + static_cast<std::ptrdiff_t>(pos));
    }
    return s;
}

LetterSplicer::LetterSplicer(const Tables& tables, const KeyboardLayout& layout, const SpliceTable& verifier)
    : tables_(tables), layout_(layout), verifier_(verifier) {
    for (const LetterShape& s : tables.shapes) {
        shape_nature_.push_back(nature_of(s.latin_key));
        shape_length_.push_back(static_cast<std::uint32_t>(s.shape.size()));
    }
}

template <bool Fuzzy>
SpliceResult LetterSplicer::splice(std::span<const std::string> latin_sequence) const {
    SpliceResult result;
    if (latin_sequence.empty()) {
        return result;
    }
    // LetterShapeSequence.setNature(): a non-SAARMAG shape nature replaces the sequence's.
    auto next_nature = [this](Nature current, ShapeId shape) {
        Nature n = shape_nature_[shape];
        return n == Nature::saarmag ? current : n;
    };
    std::vector<std::uint32_t>& complete_lss_list = result.candidates;  // the current layer
    std::vector<std::uint32_t> new_whole_lss_list;
    std::size_t i = 0;
    for (const std::string& key : latin_sequence) {
        std::span<const ShapeId> letter_shape_list;
        if (i == 0) {
            letter_shape_list = layout_.get(key, Location::head);
        } else if (i == latin_sequence.size() - 1) {
            letter_shape_list = Fuzzy ? layout_.get_middle_then_tail(key) : layout_.get(key, Location::tail);
        } else {
            letter_shape_list = layout_.get(key, Location::middle);
        }
        i++;

        if (letter_shape_list.empty()) {
            continue;
        }
        new_whole_lss_list.clear();
        new_whole_lss_list.reserve(std::max<std::size_t>(complete_lss_list.size(), 1) * letter_shape_list.size());

        // Note: when every candidate was rejected at an earlier key the list is
        // empty again, and splicing restarts from the empty sequence, exactly
        // as in Java.
        if (complete_lss_list.empty()) {
            for (ShapeId id : letter_shape_list) {
                if (verifier_.can_splice<Fuzzy>(FrontState{}, id)) {
                    new_whole_lss_list.push_back(static_cast<std::uint32_t>(result.nodes.size()));
                    result.nodes.push_back(
                        {SpliceResult::no_parent, shape_length_[id], id, next_nature(Nature::saarmag, id)});
                }
            }
        } else {
            for (ShapeId id : letter_shape_list) {
                for (std::uint32_t clss : complete_lss_list) {
                    const SpliceResult::Node c = result.nodes[clss];
                    if (verifier_.can_splice<Fuzzy>(FrontState{false, c.shape, c.nature}, id)) {
                        new_whole_lss_list.push_back(static_cast<std::uint32_t>(result.nodes.size()));
                        result.nodes.push_back({clss, c.length + shape_length_[id], id, next_nature(c.nature, id)});
                    }
                }
            }
        }
        complete_lss_list.swap(new_whole_lss_list);
    }
    return result;
}

SpliceResult LetterSplicer::fuzzy(std::span<const std::string> latin_sequence) const {
    return splice<true>(latin_sequence);
}

SpliceResult LetterSplicer::severe(std::span<const std::string> latin_sequence) const {
    return splice<false>(latin_sequence);
}

}  // namespace zcode
