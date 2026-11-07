// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.approximate.matching.BurkhardKellerTree.
//
// This is the structure the original searches, reproduced so the fast index
// can be checked against it rather than only against the service over HTTP.
// It is not used in production: see trie.h for why a BK tree is the wrong
// shape for a tolerance this large.
//
// Its shape depends on insertion order, but its results do not: Levenshtein
// distance is a metric, so the search is exact and returns precisely the
// words within the tolerance, whatever order they went in.
#pragma once

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "dict/distance.h"
#include "zcode/dictionary.h"

namespace zcode::dict {

class BkTree {
public:
    // The original mounts a fixed dummy string as the root of every tree and
    // skips it when collecting results; the tree's shape depends on it, so
    // it is passed in rather than assumed.
    explicit BkTree(ZString root_string) { nodes_.push_back({std::move(root_string), kNoWord, {}}); }

    void add(std::uint32_t word_index, ZString text) {
        if (text.empty()) {
            return;  // Strings.isNotBlank() guard
        }
        std::uint32_t node = 0;
        while (true) {
            const int d = levenshtein(nodes_[node].text, text);
            if (d == 0) {
                return;  // the original drops a duplicate silently
            }
            const auto it = nodes_[node].children.find(d);
            if (it == nodes_[node].children.end()) {
                const auto id = static_cast<std::uint32_t>(nodes_.size());
                nodes_.push_back({std::move(text), word_index, {}});
                nodes_[node].children.emplace(d, id);
                return;
            }
            node = it->second;
        }
    }

    struct Hit {
        int distance;
        std::uint32_t word;
    };

    // matching(str, n), in the original's own visit order: a node before its
    // children, children in ascending distance.
    void match(std::u32string_view text, int n, std::vector<Hit>& out) const {
        visit(0, text, n, out);
    }

private:
    static constexpr std::uint32_t kNoWord = 0xFFFFFFFFu;

    struct Node {
        ZString text;
        std::uint32_t word;
        std::map<int, std::uint32_t> children;
    };

    void visit(std::uint32_t node, std::u32string_view text, int n, std::vector<Hit>& out) const {
        const int d = levenshtein(nodes_[node].text, text);
        if (d <= n && nodes_[node].word != kNoWord) {
            out.push_back({d, nodes_[node].word});
        }
        if (nodes_[node].children.empty()) {
            return;
        }
        for (int i = d - n > 0 ? d - n : 0; i <= d + n; ++i) {
            const auto it = nodes_[node].children.find(i);
            if (it != nodes_[node].children.end()) {
                visit(it->second, text, n, out);
            }
        }
    }

    std::vector<Node> nodes_;
};

}  // namespace zcode::dict
