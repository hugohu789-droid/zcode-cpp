// SPDX-License-Identifier: GPL-2.0-or-later

#include "dict/trie.h"

#include <algorithm>
#include <cstddef>
#include <queue>

namespace zcode::dict {
namespace {

constexpr std::uint32_t kNoWord = 0xFFFFFFFFu;

}  // namespace

std::uint64_t Trie::symbol_mask(char32_t c) const {
    const auto it = std::lower_bound(symbols_.begin(), symbols_.end(), c);
    if (it == symbols_.end() || *it != c) {
        return 0;  // not in the dictionary at all
    }
    // Folded into 64 bits. A collision makes two code points share a bit,
    // which can only make a prune fail to fire; it can never hide a word.
    return std::uint64_t{1} << (static_cast<std::size_t>(it - symbols_.begin()) % kSymbolBits);
}

Trie Trie::build(const WordStore& words) {
    Trie t;
    t.words_ = &words;

    for (std::size_t i = 0; i < words.size(); ++i) {
        for (char32_t c : words.text(i)) {
            t.symbols_.push_back(c);
        }
    }
    std::sort(t.symbols_.begin(), t.symbols_.end());
    t.symbols_.erase(std::unique(t.symbols_.begin(), t.symbols_.end()), t.symbols_.end());

    // Breadth-first construction. A node is the set of words sharing a
    // prefix, held as a range of the sorted list; its children are the
    // distinct next code points, which are already grouped because the list
    // is sorted. Numbering the nodes in the order they come off the queue is
    // what makes every node's children one contiguous block.
    struct Pending {
        std::uint32_t begin;
        std::uint32_t end;
        std::uint32_t depth;
    };
    std::queue<Pending> queue;
    queue.push({0, static_cast<std::uint32_t>(words.size()), 0});

    t.ch_.push_back(0);
    t.word_.push_back(kNoWord);
    t.first_child_.push_back(1);

    while (!queue.empty()) {
        const Pending p = queue.front();
        queue.pop();
        const std::uint32_t self = static_cast<std::uint32_t>(t.first_child_.size()) - 1;

        std::uint32_t i = p.begin;
        if (i < p.end && words.length(i) == p.depth) {
            t.word_[self] = i;
            ++i;
        }

        // Each run of equal code points at this depth becomes one child.
        while (i < p.end) {
            const char32_t c = words.text(i)[p.depth];
            std::uint32_t j = i;
            while (j < p.end && words.text(j)[p.depth] == c) {
                ++j;
            }
            t.ch_.push_back(c);
            t.word_.push_back(kNoWord);
            queue.push({i, j, p.depth + 1});
            i = j;
        }
        // The next node's children start where this node's children end.
        t.first_child_.push_back(static_cast<std::uint32_t>(t.ch_.size()));
    }

    // Subtree aggregates. Children always have a higher id than their
    // parent, so one pass backwards computes every child before its parent.
    const std::size_t n = t.ch_.size();
    t.max_len_.assign(n, 0);
    t.max_freq_.assign(n, 0);
    t.charset_.assign(n, 0);
    for (std::size_t v = n; v-- > 0;) {
        if (t.word_[v] != kNoWord) {
            t.max_len_[v] =
                static_cast<std::uint8_t>(std::min<std::size_t>(words.length(t.word_[v]), 255));
            t.max_freq_[v] = words.frequency(t.word_[v]);
        }
        for (std::uint32_t c = t.first_child_[v]; c < t.first_child_[v + 1]; ++c) {
            t.max_len_[v] = std::max(t.max_len_[v], t.max_len_[c]);
            t.max_freq_[v] = std::max(t.max_freq_[v], t.max_freq_[c]);
            t.charset_[v] |= t.charset_[c] | t.symbol_mask(t.ch_[c]);
        }
    }
    return t;
}

void Trie::search(std::u32string_view query, Result& out) const {
    out.words.clear();
    out.max_length = 0;
    out.max_frequency = 0;

    const std::size_t len = query.size();
    if (len == 0 || ch_.empty()) {
        return;
    }
    const std::size_t window_end = 2 * len;  // word lengths are [len, window_end)

    // needed[p] is every code point still to be matched from position p on.
    // A subtree whose character set is missing one of them cannot contain a
    // hit, and that is the prune that does most of the work.
    std::vector<std::uint64_t> needed(len + 1, 0);
    for (std::size_t p = len; p-- > 0;) {
        const std::uint64_t bit = symbol_mask(query[p]);
        if (bit == 0) {
            return;  // a code point no word contains: nothing can match
        }
        needed[p] = needed[p + 1] | bit;
    }

    const WordStore& words = *words_;

    // `collect` walks the one root subtree whose words can survive the
    // JaroWinkler filter and keeps them; `bound` walks the rest, which can
    // still raise the two maxima ScoreMarker needs, and gives up on a
    // subtree as soon as it cannot.
    struct Walk {
        const Trie& t;
        const WordStore& words;
        std::u32string_view query;
        const std::vector<std::uint64_t>& needed;
        std::size_t len;
        std::size_t window_end;
        Result& out;

        void visit(std::uint32_t v, std::size_t depth, std::size_t matched, bool collect) {
            if (t.word_[v] != kNoWord && matched == len && depth >= len && depth < window_end) {
                out.max_length = std::max(out.max_length, static_cast<int>(depth));
                out.max_frequency =
                    std::max(out.max_frequency, words.frequency(t.word_[v]));
                if (collect) {
                    out.words.push_back(t.word_[v]);
                }
            }
            if (depth + 1 >= window_end) {
                return;  // anything below is longer than the window allows
            }
            for (std::uint32_t c = t.first_child_[v]; c < t.first_child_[v + 1]; ++c) {
                const std::size_t next =
                    matched < len && t.ch_[c] == query[matched] ? matched + 1 : matched;
                // Every query code point still unmatched costs at least one
                // more code point of the word.
                const std::size_t shortest = depth + 1 + (len - next);
                if (t.max_len_[c] < shortest) {
                    continue;
                }
                if ((needed[next] & ~t.charset_[c]) != 0) {
                    continue;
                }
                if (!collect) {
                    // Nothing here can be reported, so the only reason to
                    // descend is to raise a maximum.
                    const int reachable_len =
                        static_cast<int>(std::min<std::size_t>(t.max_len_[c], window_end - 1));
                    if (reachable_len <= out.max_length && t.max_freq_[c] <= out.max_frequency) {
                        continue;
                    }
                }
                visit(c, depth + 1, next, collect);
            }
        }
    };

    Walk walk{*this, words, query, needed, len, window_end, out};

    // The subtree sharing the query's first code point first: it is the only
    // one that yields output, and the high-frequency words it finds are what
    // make the bound prune the others.
    const std::uint32_t root_begin = first_child_[0];
    const std::uint32_t root_end = first_child_[1];
    std::uint32_t head = kNoWord;
    for (std::uint32_t c = root_begin; c < root_end; ++c) {
        if (ch_[c] == query[0]) {
            head = c;
            break;
        }
    }
    if (head != kNoWord) {
        walk.visit(head, 1, 1, true);
    }
    for (std::uint32_t c = root_begin; c < root_end; ++c) {
        if (c == head) {
            continue;
        }
        const std::size_t shortest = 1 + len;  // nothing matched by this edge
        if (max_len_[c] < shortest) {
            continue;
        }
        if ((needed[0] & ~charset_[c]) != 0) {
            continue;
        }
        const int reachable_len = static_cast<int>(std::min<std::size_t>(max_len_[c], window_end - 1));
        if (reachable_len <= out.max_length && max_freq_[c] <= out.max_frequency) {
            continue;
        }
        walk.visit(c, 1, 0, false);
    }
}

}  // namespace zcode::dict
