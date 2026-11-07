// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.approximate.matching.ScoreMarker.
#pragma once

#include <cstdint>
#include <vector>

#include "dict/distance.h"
#include "zcode/dictionary.h"

namespace zcode::dict {

// Scores and filters the words one query matched.
//
// Two things about it decide how the search has to be written:
//
//   * `max_frequency` and `max_length` are taken over every word the search
//     found, *before* the JaroWinkler filter removes any. They scale every
//     score, so a word that never appears in the output still moves the
//     output's order. The index therefore cannot restrict itself to the
//     words that can survive the filter; it has to know the maxima over all
//     of them.
//   * everything is `float`. Widening to double changes the order where two
//     candidates are close, and the original's floats are what the service
//     returns.
class ScoreMarker {
public:
    explicit ScoreMarker(std::u32string_view query) : query_(query) {}

    void add(SuggestWord word) { words_.push_back(std::move(word)); }
    void reserve(std::size_t n) { words_.reserve(n); }

    // setMaxFrequency(): raises only.
    void observe_frequency(std::uint64_t frequency) {
        if (max_frequency_ < frequency) {
            max_frequency_ = frequency;
        }
    }
    // The original takes the length maximum inside markAndFilter, over the
    // same pre-filter set; the index reports it separately because it finds
    // some of those words without keeping them.
    void observe_length(int length) {
        if (max_length_ < static_cast<float>(length)) {
            max_length_ = static_cast<float>(length);
        }
    }

    // markAndFilter(): scores every word, drops the ones whose JaroWinkler
    // value is below 1 (which means "no common prefix", see distance.h).
    std::vector<SuggestWord> mark_and_filter() {
        const JaroWinkler jw;
        const float max_score = frequency_threshold_ +
                                jaro_winkler_threshold_ * jw.distance(query_, query_) +
                                levenshtein_score(static_cast<int>(query_.size()), 0, max_length_) *
                                    levenshtein_threshold_;

        std::vector<SuggestWord> kept;
        kept.reserve(words_.size());
        for (SuggestWord& w : words_) {
            w.levenshtein_score = levenshtein_score(w.length, w.levenshtein_d, max_length_);
            w.jaro_winkler_score = jw.distance(query_, w.str);
            if (w.jaro_winkler_score < 1) {
                continue;
            }
            w.frequency_score =
                static_cast<float>(w.frequency) / static_cast<float>(max_frequency_);
            w.score = w.frequency_score * frequency_threshold_ +
                      w.jaro_winkler_score * jaro_winkler_threshold_ +
                      w.levenshtein_score * levenshtein_threshold_;
            w.score = w.score / max_score;
            kept.push_back(std::move(w));
        }
        return kept;
    }

    std::uint64_t max_frequency() const { return max_frequency_; }
    float max_length() const { return max_length_; }

private:
    // computeLevenshteinScore()
    static float levenshtein_score(int length, int levenshtein_distance, float max_length) {
        return 1.0F - ((static_cast<float>(length) / 2.0F + static_cast<float>(levenshtein_distance)) /
                       (1.5F * max_length));
    }

    std::u32string_view query_;
    std::vector<SuggestWord> words_;
    std::uint64_t max_frequency_ = 0;
    float max_length_ = 0;

    float frequency_threshold_ = 0.15F;
    float levenshtein_threshold_ = 0.50F;
    float jaro_winkler_threshold_ = 1.0F;
};

}  // namespace zcode::dict
