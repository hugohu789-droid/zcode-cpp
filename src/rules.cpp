// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.front.* and .behind.*; see rules.h.
#include "rules.h"

#include <algorithm>

namespace zcode {

bool match_front(const Tables& tables, RuleId id, const LetterShapeSequence& lss) {
    const Rule& rule = tables.rules[id];
    switch (rule.kind) {
        case RuleKind::front_empty:  // FrontEmpty
            return lss.empty();
        case RuleKind::front_not_empty:  // FrontNotEmpty
            return !lss.empty();
        case RuleKind::last_in:  // FrontLikeTail*, FrontLikeMiddleII, FrontIsMiddleSD
            return !lss.empty() && rule.members[tables.shapes[lss.last()].equals_class];
        case RuleKind::last_not_in:  // FrontLikeMiddle{A,E,I,O}, FrontLikeTailBanbvrBaga(U), FrontIsNotMiddleSD
            return !lss.empty() && !rule.members[tables.shapes[lss.last()].equals_class];
        case RuleKind::last_at_not_in: {  // FrontLikeMiddleGedes(Silbi)U, FrontTailGedesSuul1U
            if (lss.empty()) return false;
            const LetterShape& last = tables.shapes[lss.last()];
            return last.location == rule.location && !rule.members[last.equals_class];
        }
        case RuleKind::nature_is:  // FrontIsChigaSequence
            return !lss.empty() && lss.nature() == rule.nature;
        case RuleKind::nature_is_not:  // FrontIsNotChigaSequence
            return !lss.empty() && lss.nature() != rule.nature;
        case RuleKind::negation:  // FrontLikeTailHvmsI
            return !lss.empty() && !match_front(tables, rule.inner, lss);
        default:
            return false;  // behind kinds never reach here (rejected by the loader)
    }
}

bool match_behind(const Tables& tables, RuleId id, const LetterShape* next) {
    const Rule& rule = tables.rules[id];
    switch (rule.kind) {
        case RuleKind::behind_empty:  // BehindEmpty
            return next == nullptr;
        case RuleKind::behind_not_empty:  // BehindNotEmpty
            return next != nullptr;
        case RuleKind::next_in:  // BehindIs{A,AE}Chachilga, BehindLike*
            return next != nullptr && rule.members[next->equals_class];
        case RuleKind::next_key_in:  // BehindIsEhshig, BehindIsChingaEhshig
            return next != nullptr &&
                   std::find(rule.keys.begin(), rule.keys.end(), next->latin_key) != rule.keys.end();
        case RuleKind::negation:  // BehindIsNotEhshig
            return next != nullptr && !match_behind(tables, rule.inner, next);
        default:
            return false;  // front kinds never reach here (rejected by the loader)
    }
}

}  // namespace zcode
