// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.Verifier.
#pragma once

#include "letter_shape_sequence.h"
#include "tables.h"

namespace zcode {

// Verifier.canFuzzySplicing(lss, ls). Only lss.state() matters.
bool can_fuzzy_splicing(const Tables& tables, const FrontState& lss, const LetterShape& ls);

// Verifier.canSevereSplicing(lss, ls). Only lss.state() matters.
bool can_severe_splicing(const Tables& tables, const FrontState& lss, const LetterShape& ls);

// The splicer does not call these per candidate: SpliceTable evaluates them
// once for every (state, shape) pair (see splice_table.h).

}  // namespace zcode
