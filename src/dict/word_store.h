// SPDX-License-Identifier: GPL-2.0-or-later
//
// The word list, laid out as one arena rather than one string per word.
//
// Not a micro-optimisation: with a std::u32string each, the 461,182-word
// extension pack cost 46 MB in allocator overhead and string objects before
// any index existed, and 461,182 allocations to load. Concatenating the
// words and keeping offsets costs 4 bytes per code point plus 12 bytes per
// word, in three allocations.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "zcode/dictionary.h"

namespace zcode::dict {

class WordStore {
public:
    // `words` must be sorted by text and free of duplicates.
    void assign(const std::vector<Word>& words) {
        std::size_t points = 0;
        for (const Word& w : words) {
            points += w.str.size();
        }
        arena_.clear();
        arena_.reserve(points);
        offset_.clear();
        offset_.reserve(words.size() + 1);
        frequency_.clear();
        frequency_.reserve(words.size());
        for (const Word& w : words) {
            offset_.push_back(static_cast<std::uint32_t>(arena_.size()));
            arena_.append(w.str);
            frequency_.push_back(w.frequency);
        }
        offset_.push_back(static_cast<std::uint32_t>(arena_.size()));
    }

    std::size_t size() const { return frequency_.size(); }

    std::u32string_view text(std::size_t i) const {
        return std::u32string_view(arena_).substr(offset_[i], offset_[i + 1] - offset_[i]);
    }

    std::size_t length(std::size_t i) const { return offset_[i + 1] - offset_[i]; }

    std::uint64_t frequency(std::size_t i) const { return frequency_[i]; }

private:
    ZString arena_;
    std::vector<std::uint32_t> offset_;  // size() + 1 entries
    std::vector<std::uint64_t> frequency_;
};

}  // namespace zcode::dict
