// SPDX-License-Identifier: GPL-2.0-or-later
//
// The index the dictionary search runs on. Not in the Java original, which
// uses one Burkhard-Keller tree per word length (ported in bk_tree.h as the
// test oracle).
//
// Why not a BK tree
// -----------------
// A BK tree searched with tolerance n must descend into every child whose
// distance lies in [d-n, d+n]. Here n is the word length minus the query
// length, which runs up to L-1 -- 9 or 10 for ordinary input -- so almost
// the whole tree is visited. That is why the original needs a seven-thread
// pool and still takes 50-100 ms per request.
//
// The same reasoning rules out the usual replacements. SymSpell precomputes
// the dictionary's deletions, which is why its own benchmarks stop at
// distance 3: a 20-code-point word at distance 9 has 431,909 deletion
// variants. A Levenshtein automaton over an FST prunes by how few trie nodes
// it can reach, and at distance 10 it can reach nearly all of them.
//
// What makes this tractable is that the search is not really edit distance.
// Because every word in the length-K bucket has length exactly K, a hit at
// distance K-L can only be K-L insertions, so:
//
//     hit  <=>  query is a subsequence of word, and L <= |word| < 2L
//
// Testing a subsequence greedily left to right gives one well-defined state
// per trie path -- how many query code points have been consumed -- so one
// depth-first walk answers it for the whole dictionary at once.
//
// Layout
// ------
// Nodes are numbered breadth-first, which makes each node's children a
// contiguous block of ids, and the blocks appear in parent order. So the
// children of node i are exactly the nodes [first_child[i], first_child[i+1])
// and no edge list is needed.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "dict/word_store.h"
#include "zcode/dictionary.h"

namespace zcode::dict {

class Trie {
public:
    // `words` must be sorted and free of duplicates, and must outlive
    // the trie.
    static Trie build(const WordStore& words);

    struct Result {
        // Indices into the word list, for the hits that can survive the
        // JaroWinkler filter -- those sharing the query's first code point.
        std::vector<std::uint32_t> words;
        // Taken over *every* hit, including the ones not listed above:
        // ScoreMarker needs the maxima over the unfiltered set.
        int max_length = 0;
        std::uint64_t max_frequency = 0;
    };

    void search(std::u32string_view query, Result& out) const;

    std::size_t node_count() const { return ch_.size(); }

private:
    // Dense symbol ids, so a subtree's character set fits in one word. More
    // symbols than bits is handled by folding, which can only cost a missed
    // prune, never a missed word.
    static constexpr int kSymbolBits = 64;

    std::uint64_t symbol_mask(char32_t c) const;

    // Per node, indexed by node id.
    std::vector<std::uint32_t> first_child_;  // size n+1
    std::vector<char32_t> ch_;                // the code point on the edge into this node
    std::vector<std::uint32_t> word_;         // index into the word list, or npos
    std::vector<std::uint8_t> max_len_;       // longest word in this subtree, capped
    std::vector<std::uint64_t> max_freq_;     // highest frequency in this subtree
    std::vector<std::uint64_t> charset_;      // code points occurring in this subtree

    const WordStore* words_ = nullptr;
    std::vector<char32_t> symbols_;  // distinct code points, ascending
};

}  // namespace zcode::dict
