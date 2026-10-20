// SPDX-License-Identifier: GPL-2.0-or-later
//
// Evaluation of the table-driven splicing rules.
// Ported from com.zorigt.ime.letter.splice.SplicingAtFrontRule /
// SplicingAtBehindRule and every class in letter/splice/front/* and
// letter/splice/behind/* (each is one row of data/rules.tsv; the row names the
// Java class).
#pragma once

#include "letter_shape_sequence.h"
#include "tables.h"

namespace zcode {

// SplicingAtFrontRule.match(lss)
bool match_front(const Tables& tables, RuleId rule, const LetterShapeSequence& lss);

// SplicingAtBehindRule.match(ls); next == nullptr stands for a null argument.
bool match_behind(const Tables& tables, RuleId rule, const LetterShape* next);

}  // namespace zcode
