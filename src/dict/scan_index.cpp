// SPDX-License-Identifier: GPL-2.0-or-later

#include "dict/scan_index.h"

#include <algorithm>
#include <numeric>

namespace zcode::dict {
namespace {

// Is `small` a subsequence of `big`? Greedy left to right is both correct
// and optimal for this: taking the earliest match of each code point never
// rules out a later one.
bool is_subsequence(std::u32string_view small, std::u32string_view big) {
    std::size_t i = 0;
    for (const char32_t c : big) {
        if (c == small[i] && ++i == small.size()) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::size_t ScanIndex::symbol_of(char32_t c) const {
    const auto it = std::lower_bound(symbols_.begin(), symbols_.end(), c);
    if (it == symbols_.end() || *it != c) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(it - symbols_.begin());
}

ScanIndex ScanIndex::build(const WordStore& words) {
    ScanIndex s;
    s.words_ = &words;

    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::u32string_view text = words.text(i);
        if (text.empty()) {
            continue;
        }
        s.symbols_.push_back(text.front());
        s.max_length_ = std::max(s.max_length_, text.size());
    }
    std::sort(s.symbols_.begin(), s.symbols_.end());
    s.symbols_.erase(std::unique(s.symbols_.begin(), s.symbols_.end()), s.symbols_.end());
    if (s.symbols_.empty()) {
        s.start_.assign(1, 0);
        return s;
    }

    const std::size_t buckets = (s.max_length_ + 1) * s.symbols_.size();
    std::vector<std::uint32_t> count(buckets + 1, 0);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::u32string_view text = words.text(i);
        if (text.empty()) {
            continue;
        }
        ++count[s.bucket_of(text.size(), s.symbol_of(text.front()))];
    }
    // Counting sort: the starts are the running total, then each word is
    // dropped into its bucket in word order, so a bucket stays sorted.
    s.start_.assign(buckets + 1, 0);
    std::uint32_t running = 0;
    for (std::size_t b = 0; b < buckets; ++b) {
        s.start_[b] = running;
        running += count[b];
    }
    s.start_[buckets] = running;
    s.order_.assign(running, 0);
    s.max_freq_.assign(buckets, 0);
    std::vector<std::uint32_t> cursor(s.start_.begin(), s.start_.end() - 1);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::u32string_view text = words.text(i);
        if (text.empty()) {
            continue;
        }
        const std::size_t b = s.bucket_of(text.size(), s.symbol_of(text.front()));
        s.order_[cursor[b]++] = static_cast<std::uint32_t>(i);
        s.max_freq_[b] = std::max(s.max_freq_[b], words.frequency(i));
    }
    return s;
}

void ScanIndex::search(std::u32string_view query, Trie::Result& out) const {
    out.words.clear();
    out.max_length = 0;
    out.max_frequency = 0;

    const std::size_t len = query.size();
    if (len == 0 || symbols_.empty() || words_ == nullptr) {
        return;
    }
    const std::size_t head = symbol_of(query[0]);
    const std::size_t window_end = std::min(2 * len, max_length_ + 1);

    const WordStore& words = *words_;
    for (std::size_t length = len; length < window_end; ++length) {
        for (std::size_t symbol = 0; symbol < symbols_.size(); ++symbol) {
            const bool collect = symbol == head;
            const std::size_t b = bucket_of(length, symbol);
            if (!collect) {
                // Nothing here can reach the output -- only a word sharing
                // the query's first code point survives the JaroWinkler
                // filter -- so the only reason to look is to raise one of
                // the two maxima.
                if (static_cast<int>(length) <= out.max_length &&
                    max_freq_[b] <= out.max_frequency) {
                    continue;
                }
            }
            for (std::uint32_t k = start_[b]; k < start_[b + 1]; ++k) {
                const std::uint32_t index = order_[k];
                if (!is_subsequence(query, words.text(index))) {
                    continue;
                }
                out.max_length = std::max(out.max_length, static_cast<int>(length));
                out.max_frequency = std::max(out.max_frequency, words.frequency(index));
                if (collect) {
                    out.words.push_back(index);
                }
            }
        }
    }
}

}  // namespace zcode::dict
