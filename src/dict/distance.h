// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.approximate.matching.LevenshteinDistance and
// com.zorigt.ime.approximate.matching.JaroWinklerDistance.
//
// Both operate on ZCode, whose code points are all in the BMP, so one
// char32_t here is one Java `char` and every length matches the original's.
//
// Arithmetic is `float`, not double, because the original's is: the scores
// these feed are compared against each other, and widening would change the
// order in the places where two candidates are close.
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "zcode/zcode.h"

namespace zcode::dict {

// LevenshteinDistance.getDistance(). Plain unit-cost edit distance; the
// original's commented-out normalisation is left out because it is left out
// there too.
inline int levenshtein(std::u32string_view a, std::u32string_view b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    if (n == 0 || m == 0) {
        // The original returns max(n, m) unless both are empty.
        return n == m ? 0 : static_cast<int>(std::max(n, m));
    }
    // Two rows instead of the original's full (n+1)x(m+1) matrix: the
    // recurrence only ever reads the previous row, and this is called once
    // per dictionary word per query.
    std::vector<int> prev(m + 1);
    std::vector<int> cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) {
        prev[j] = static_cast<int>(j);
    }
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min(std::min(prev[j], cur[j - 1]) + 1, prev[j - 1] + cost);
        }
        prev.swap(cur);
    }
    return prev[m];
}

// The same, but gives up as soon as every value in a row exceeds `limit`.
// Not in the original; only used where the caller would discard anything
// larger anyway, and it returns limit+1 then, which is never a valid hit.
inline int levenshtein_capped(std::u32string_view a, std::u32string_view b, int limit) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    if (n == 0 || m == 0) {
        return n == m ? 0 : static_cast<int>(std::max(n, m));
    }
    std::vector<int> prev(m + 1);
    std::vector<int> cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) {
        prev[j] = static_cast<int>(j);
    }
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        int row_min = cur[0];
        for (std::size_t j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min(std::min(prev[j], cur[j - 1]) + 1, prev[j - 1] + cost);
            row_min = std::min(row_min, cur[j]);
        }
        if (row_min > limit) {
            return limit + 1;
        }
        prev.swap(cur);
    }
    return prev[m];
}

// JaroWinklerDistance.getDistance().
//
// This is *not* Jaro-Winkler. The original has been modified in three ways
// and the modifications are what the service's filter depends on, so they
// are reproduced exactly:
//
//  1. `range` is the longer string's whole length, not the usual
//     max/2 - 1, so a character may match at any distance.
//  2. When there is a common prefix the result is pushed above 1 (1 + ...,
//     or 2 + ... when the prefix covers all but the last character). The
//     caller's filter is `score < 1 -> drop`, so what this really encodes is
//     "the first code point must match, and Jaro must reach the threshold".
//  3. The standard Winkler prefix bonus, capped at 4 characters, is
//     commented out there; the uncapped mtp[2]/mtp[3] term replaces it.
//
// Returns the same float the original does, including values above 1.
class JaroWinkler {
public:
    float distance(std::u32string_view s1, std::u32string_view s2) const {
        Parts mtp;
        matches(s1, s2, mtp);
        const float m = static_cast<float>(mtp.matches);
        if (m == 0.0F) {
            return 0.0F;
        }
        const float j = (m / static_cast<float>(s1.size()) + m / static_cast<float>(s2.size()) +
                         (m - static_cast<float>(mtp.transpositions)) / m) /
                        3.0F;
        if (j < threshold_ || mtp.prefix <= 0) {
            return j;
        }
        const float bonus =
            static_cast<float>(mtp.prefix) / static_cast<float>(mtp.min_length) * (1.0F - j);
        const std::size_t almost_all = std::max<std::size_t>(s1.empty() ? 0 : s1.size() - 1, 2);
        if (static_cast<std::size_t>(mtp.prefix) >= almost_all) {
            return 2.0F + j + bonus;
        }
        return 1.0F + j + bonus;
    }

    float threshold() const { return threshold_; }
    void set_threshold(float t) { threshold_ = t; }

private:
    struct Parts {
        int matches = 0;
        int transpositions = 0;
        int prefix = 0;
        int min_length = 0;
    };

    static void matches(std::u32string_view s1, std::u32string_view s2, Parts& out) {
        std::u32string_view max = s1.size() > s2.size() ? s1 : s2;
        std::u32string_view min = s1.size() > s2.size() ? s2 : s1;

        // range == max.length(): see note 1 above.
        const std::size_t range = max.size();
        std::vector<int> match_indexes(min.size(), -1);
        std::vector<char> match_flags(max.size(), 0);
        int matched = 0;

        for (std::size_t mi = 0; mi < min.size(); ++mi) {
            const char32_t c1 = min[mi];
            const std::size_t from = mi > range ? mi - range : 0;
            const std::size_t to = std::min(mi + range + 1, max.size());
            for (std::size_t k = from; k < to; ++k) {
                if (!match_flags[k] && c1 == max[k]) {
                    match_indexes[mi] = static_cast<int>(k);
                    match_flags[k] = 1;
                    ++matched;
                    break;
                }
            }
        }

        // The matched characters of each string, each in its own order; a
        // position where the two disagree is half a transposition.
        std::u32string ms1;
        std::u32string ms2;
        ms1.reserve(static_cast<std::size_t>(matched));
        ms2.reserve(static_cast<std::size_t>(matched));
        for (std::size_t i = 0; i < min.size(); ++i) {
            if (match_indexes[i] != -1) {
                ms1.push_back(min[i]);
            }
        }
        for (std::size_t i = 0; i < max.size(); ++i) {
            if (match_flags[i]) {
                ms2.push_back(max[i]);
            }
        }
        int transpositions = 0;
        for (std::size_t i = 0; i < ms1.size(); ++i) {
            if (ms1[i] != ms2[i]) {
                ++transpositions;
            }
        }

        // The common prefix is measured on s1/s2 in their given order, not on
        // min/max, but bounded by min's length -- as in the original.
        int prefix = 0;
        for (std::size_t i = 0; i < min.size() && s1[i] == s2[i]; ++i) {
            ++prefix;
        }

        out.matches = matched;
        out.transpositions = transpositions / 2;
        out.prefix = prefix;
        out.min_length = static_cast<int>(min.size());
    }

    float threshold_ = 0.7F;
};

}  // namespace zcode::dict
