// SPDX-License-Identifier: GPL-2.0-or-later
//
// A second index over the same words, trading lookup time for memory.
//
// The trie in trie.h answers in microseconds and costs about 900 bytes per
// word, which is right for the dictionary behind every keystroke. It is the
// wrong trade for the optional extension pack: 461,182 words came to 100 MB
// resident, for a fallback that only runs when the main dictionary has
// nothing to say. Trimming the pack does not help -- the words that rescue
// an empty result are spread evenly across the frequency range, and taking
// only frequency >= 2 halves the pack but loses a fifth of its value.
//
// This holds the words bucketed by (length, first code point) and scans the
// buckets a query's length window can reach, which costs one permutation
// index -- 4 bytes per word -- and milliseconds per lookup. It finds exactly
// what the trie finds, including the two maxima ScoreMarker takes over the
// words the filter will later discard, so the two are interchangeable and a
// test asserts they agree.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "dict/trie.h"
#include "dict/word_store.h"

namespace zcode::dict {

class ScanIndex {
public:
    static ScanIndex build(const WordStore& words);

    void search(std::u32string_view query, Trie::Result& out) const;

private:
    // Buckets are (length, first symbol) pairs, numbered length * symbols +
    // symbol, so the words of one length are contiguous.
    std::size_t bucket_of(std::size_t length, std::size_t symbol) const {
        return length * symbols_.size() + symbol;
    }
    // The dense id of a code point, or npos when no word contains it.
    std::size_t symbol_of(char32_t c) const;

    const WordStore* words_ = nullptr;
    std::vector<char32_t> symbols_;        // distinct first code points, ascending
    std::vector<std::uint32_t> order_;     // word indices, grouped by bucket
    std::vector<std::uint32_t> start_;     // bucket -> first entry in order_
    std::vector<std::uint64_t> max_freq_;  // bucket -> highest frequency in it
    std::size_t max_length_ = 0;
};

}  // namespace zcode::dict
