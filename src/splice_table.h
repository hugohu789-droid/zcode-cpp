// SPDX-License-Identifier: GPL-2.0-or-later
//
// Verifier.canFuzzySplicing / canSevereSplicing, precomputed.
//
// Every front rule depends only on FrontState (empty?, last shape, nature) and
// every behind rule only on the shape about to be appended, so both Verifier
// methods are pure functions of (FrontState, next shape). The table evaluates
// the ported Verifier (verifier.cpp) once for every state and shape and
// answers by lookup afterwards.
#pragma once

#include <cstdint>
#include <vector>

#include "letter_shape_sequence.h"
#include "tables.h"

namespace zcode {

class SpliceTable {
public:
    explicit SpliceTable(const Tables& tables);

    template <bool Fuzzy>
    bool can_splice(const FrontState& lss, ShapeId next) const {
        return ok_[Fuzzy ? 1 : 0][state_index(lss) * shapes_ + next] != 0;
    }

private:
    std::size_t state_index(const FrontState& s) const {
        return s.empty ? 0 : 1 + static_cast<std::size_t>(s.last) * 3 + static_cast<std::size_t>(s.nature);
    }

    std::size_t shapes_ = 0;
    // [mode][state * shapes + next], mode 0 = severe, 1 = fuzzy;
    // states: 0 = empty, then 1 + last * 3 + nature
    std::vector<std::uint8_t> ok_[2];
};

}  // namespace zcode
