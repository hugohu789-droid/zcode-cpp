// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.keyboard.Decomposer.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zcode {

class Decomposer {
public:
    // Builds the key trie (Decomposer.build()); blank keys are ignored.
    explicit Decomposer(const std::vector<std::string>& keys);

    // Decomposer.decompose(str): every split of `str` into keys. Characters
    // that start no key are skipped; a blank remainder ends a split.
    std::vector<std::vector<std::string>> decompose(std::string_view str) const;

private:
    // Decomposer.Node. Children are a 256-entry index by byte (-1 = none)
    // instead of Java's HashMap<Character, Node>.
    struct Node {
        std::string key;
        bool is_end = false;
        std::array<std::int32_t, 256> children;
        Node() { children.fill(-1); }
    };

    void decompose(std::vector<std::string>& prefixes, std::string_view str,
                   std::vector<std::vector<std::string>>& cases) const;
    // Decomposer.check(): the key nodes along the longest trie path that
    // matches a prefix of str, shortest first. Appends to `out`.
    void check(std::string_view str, std::vector<std::int32_t>& out) const;

    std::int32_t child(std::int32_t node, char c) const {
        return nodes_[node].children[static_cast<unsigned char>(c)];
    }

    std::vector<Node> nodes_;  // nodes_[0] is the root
};

}  // namespace zcode
