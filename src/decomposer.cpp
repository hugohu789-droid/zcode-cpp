// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.keyboard.Decomposer.
#include "decomposer.h"

#include "strings.h"

namespace zcode {

Decomposer::Decomposer(const std::vector<std::string>& keys) : nodes_(1) {
    for (const std::string& key : keys) {
        if (is_blank(key)) {
            continue;
        }
        std::size_t current = 0;
        for (std::size_t i = 0; i != key.size(); ++i) {
            char c = key[i];
            auto it = nodes_[current].children.find(c);
            if (it == nodes_[current].children.end()) {
                std::size_t child = nodes_.size();
                nodes_.emplace_back();  // may reallocate: re-index afterwards
                it = nodes_[current].children.emplace(c, child).first;
            }
            std::size_t next = it->second;
            if (i == key.size() - 1) {
                nodes_[next].is_end = true;
                nodes_[next].key = key;
            }
            current = next;
        }
    }
}

std::vector<std::vector<std::string>> Decomposer::decompose(std::string_view str) const {
    std::vector<std::vector<std::string>> cases;
    decompose({}, str, cases);
    return cases;
}

void Decomposer::decompose(const std::vector<std::string>& prefixes, std::string_view str,
                           std::vector<std::vector<std::string>>& cases) const {
    // Java recurses on str.substring(1) when nothing matches; that tail call
    // is a loop here so that long runs of unknown characters cannot exhaust
    // the stack. The result is identical.
    std::vector<std::size_t> matched;
    while (true) {
        if (is_blank(str)) {
            cases.push_back(prefixes);
            return;
        }
        matched = check(str);
        if (!matched.empty()) {
            break;
        }
        str.remove_prefix(1);
    }
    for (std::size_t node : matched) {
        std::vector<std::string> new_prefixes = prefixes;
        new_prefixes.push_back(nodes_[node].key);
        decompose(new_prefixes, str.substr(nodes_[node].key.size()), cases);
    }
}

std::vector<std::size_t> Decomposer::check(std::string_view str) const {
    std::vector<std::size_t> match_nodes;
    std::size_t current = 0;
    for (char c : str) {
        auto it = nodes_[current].children.find(c);
        if (it == nodes_[current].children.end()) {
            break;
        }
        current = it->second;
        if (nodes_[current].is_end) {
            match_nodes.push_back(current);
        }
    }
    return match_nodes;
}

}  // namespace zcode
