// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.Verifier.
#pragma once

#include "letter_shape_sequence.h"
#include "tables.h"

namespace zcode {

// Verifier.canFuzzySplicing(lss, ls)
bool can_fuzzy_splicing(const Tables& tables, const LetterShapeSequence& lss, const LetterShape& ls);

// Verifier.canSevereSplicing(lss, ls)
bool can_severe_splicing(const Tables& tables, const LetterShapeSequence& lss, const LetterShape& ls);

}  // namespace zcode
