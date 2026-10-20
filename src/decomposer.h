// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.keyboard.Decomposer.
#pragma once

#include <map>
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
    // Decomposer.Node; children are indices into nodes_.
    struct Node {
        std::string key;
        bool is_end = false;
        std::map<char, std::size_t> children;
    };

    void decompose(const std::vector<std::string>& prefixes, std::string_view str,
                   std::vector<std::vector<std::string>>& cases) const;
    std::vector<std::size_t> check(std::string_view str) const;

    std::vector<Node> nodes_;  // nodes_[0] is the root
};

}  // namespace zcode
