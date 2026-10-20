// SPDX-License-Identifier: GPL-2.0-or-later
//
// Precomputed Verifier; see splice_table.h.
#include "splice_table.h"

#include "verifier.h"

namespace zcode {

SpliceTable::SpliceTable(const Tables& tables) : shapes_(tables.shapes.size()) {
    const std::size_t states = 1 + shapes_ * 3;
    std::vector<FrontState> all(states);
    for (const LetterShape& last : tables.shapes) {
        for (Nature n : {Nature::chagh, Nature::hundii, Nature::saarmag}) {
            FrontState s{false, last.id, n};
            all[state_index(s)] = s;
        }
    }
    for (int fuzzy = 0; fuzzy < 2; ++fuzzy) {
        ok_[fuzzy].assign(states * shapes_, 0);
        for (std::size_t st = 0; st < states; ++st) {
            for (const LetterShape& next : tables.shapes) {
                bool ok = fuzzy ? can_fuzzy_splicing(tables, all[st], next) : can_severe_splicing(tables, all[st], next);
                ok_[fuzzy][st * shapes_ + next.id] = ok ? 1 : 0;
            }
        }
    }
}

}  // namespace zcode
