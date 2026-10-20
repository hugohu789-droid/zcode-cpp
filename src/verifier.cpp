// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.Verifier.
#include "verifier.h"

#include "rules.h"

namespace zcode {
namespace {

// Both Java methods have the same body over a different pair of rules.
bool can_splicing(const Tables& tables, const FrontState& lss, const LetterShape& ls, RuleId front,
                  RuleId LetterShape::*behind) {
    if (front != no_rule && !match_front(tables, front, lss)) {
        return false;
    }
    if (!lss.empty) {
        RuleId last_behind = tables.shapes[lss.last].*behind;
        return last_behind == no_rule || match_behind(tables, last_behind, &ls);
    }
    return true;
}

}  // namespace

bool can_fuzzy_splicing(const Tables& tables, const FrontState& lss, const LetterShape& ls) {
    return can_splicing(tables, lss, ls, ls.fuzzy_front, &LetterShape::fuzzy_behind);
}

bool can_severe_splicing(const Tables& tables, const FrontState& lss, const LetterShape& ls) {
    return can_splicing(tables, lss, ls, ls.severe_front, &LetterShape::severe_behind);
}

}  // namespace zcode
