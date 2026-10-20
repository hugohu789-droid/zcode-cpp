// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.LetterSplicer. fuzzy() and severe()
// are two copies of one loop in Java; they differ only in the shapes taken for
// the last key and in the Verifier method, so they share one template here.
// The per-rejection logging and timing of the original are not ported.
#include "letter_splicer.h"

#include "verifier.h"

namespace zcode {

template <bool Fuzzy>
std::vector<LetterShapeSequence> LetterSplicer::splice(std::span<const std::string> latin_sequence) const {
    if (latin_sequence.empty()) {
        return {};
    }
    std::vector<LetterShapeSequence> complete_lss_list;
    std::size_t i = 0;
    for (const std::string& key : latin_sequence) {
        std::vector<ShapeId> letter_shape_list;
        auto add = [&](Location loc) {
            for (ShapeId id : layout_.get(key, loc)) {
                letter_shape_list.push_back(id);
            }
        };
        if (i == 0) {
            add(Location::head);
        } else if (i == latin_sequence.size() - 1) {
            if (Fuzzy) {
                add(Location::middle);
            }
            add(Location::tail);
        } else {
            add(Location::middle);
        }
        i++;

        if (letter_shape_list.empty()) {
            continue;
        }
        std::size_t size = (complete_lss_list.empty() ? 1 : complete_lss_list.size()) * letter_shape_list.size();
        std::vector<LetterShapeSequence> new_whole_lss_list;
        new_whole_lss_list.reserve(size);

        // Note: when every candidate was rejected at an earlier key the list is
        // empty again, and splicing restarts from the empty sequence, exactly
        // as in Java.
        if (complete_lss_list.empty()) {
            const LetterShapeSequence empty_lss;
            for (ShapeId id : letter_shape_list) {
                const LetterShape& letter_shape = tables_.shapes[id];
                bool ok = Fuzzy ? can_fuzzy_splicing(tables_, empty_lss, letter_shape)
                                : can_severe_splicing(tables_, empty_lss, letter_shape);
                if (ok) {
                    LetterShapeSequence lss;
                    lss.append(letter_shape);
                    new_whole_lss_list.push_back(std::move(lss));
                }
            }
        } else {
            for (ShapeId id : letter_shape_list) {
                const LetterShape& letter_shape = tables_.shapes[id];
                for (const LetterShapeSequence& clss : complete_lss_list) {
                    bool ok = Fuzzy ? can_fuzzy_splicing(tables_, clss, letter_shape)
                                    : can_severe_splicing(tables_, clss, letter_shape);
                    if (ok) {
                        LetterShapeSequence lss;
                        lss.append(clss, tables_);
                        lss.append(letter_shape);
                        new_whole_lss_list.push_back(std::move(lss));
                    }
                }
            }
        }
        complete_lss_list = std::move(new_whole_lss_list);
    }
    return complete_lss_list;
}

std::vector<LetterShapeSequence> LetterSplicer::fuzzy(std::span<const std::string> latin_sequence) const {
    return splice<true>(latin_sequence);
}

std::vector<LetterShapeSequence> LetterSplicer::severe(std::span<const std::string> latin_sequence) const {
    return splice<false>(latin_sequence);
}

}  // namespace zcode
