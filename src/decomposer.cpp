// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.keyboard.Decomposer.
#include "decomposer.h"

#include "common_strings.h"

namespace zcode {

Decomposer::Decomposer(const std::vector<std::string>& keys) : nodes_(1) {
    for (const std::string& key : keys) {
        if (is_blank(key)) {
            continue;
        }
        std::int32_t current = 0;
        for (std::size_t i = 0; i != key.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(key[i]);
            if (nodes_[current].children[c] < 0) {
                std::int32_t created = static_cast<std::int32_t>(nodes_.size());
                nodes_.emplace_back();
                nodes_[current].children[c] = created;
            }
            std::int32_t next = nodes_[current].children[c];
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
    std::vector<std::string> prefixes;
    decompose(prefixes, str, cases);
    return cases;
}

void Decomposer::decompose(std::vector<std::string>& prefixes, std::string_view str,
                           std::vector<std::vector<std::string>>& cases) const {
    // Java recurses on str.substring(1) when nothing matches; that tail call
    // is a loop here so that long runs of unknown characters cannot exhaust
    // the stack. Java copies the prefix list for every match; here one list is
    // extended and restored around each recursive call, which visits the same
    // splits in the same order.
    std::vector<std::int32_t> matched;
    while (true) {
        if (is_blank(str)) {
            cases.push_back(prefixes);
            return;
        }
        check(str, matched);
        if (!matched.empty()) {
            break;
        }
        str.remove_prefix(1);
    }
    for (std::int32_t node : matched) {
        prefixes.push_back(nodes_[node].key);
        decompose(prefixes, str.substr(nodes_[node].key.size()), cases);
        prefixes.pop_back();
    }
}

void Decomposer::check(std::string_view str, std::vector<std::int32_t>& out) const {
    std::int32_t current = 0;
    for (char c : str) {
        current = child(current, c);
        if (current < 0) {
            break;
        }
        if (nodes_[current].is_end) {
            out.push_back(current);
        }
    }
}

}  // namespace zcode
