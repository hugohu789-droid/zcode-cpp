// SPDX-License-Identifier: GPL-2.0-or-later
//
// The precomputed SpliceTable must agree with the ported Verifier everywhere.
#include <gtest/gtest.h>

#include "splice_table.h"
#include "tables.h"
#include "verifier.h"

namespace zcode {
namespace {

TEST(SpliceTable, AgreesWithVerifierOnEveryState) {
    Tables t;
    std::string error;
    ASSERT_TRUE(Tables::parse_embedded(t, error)) << error;
    SpliceTable table(t);
    std::vector<FrontState> states = {FrontState{}};
    for (const LetterShape& last : t.shapes) {
        for (Nature n : {Nature::chagh, Nature::hundii, Nature::saarmag}) states.push_back(FrontState{false, last.id, n});
    }
    std::size_t accepted = 0;
    for (const FrontState& s : states) {
        for (const LetterShape& next : t.shapes) {
            ASSERT_EQ(table.can_splice<false>(s, next.id), can_severe_splicing(t, s, next));
            ASSERT_EQ(table.can_splice<true>(s, next.id), can_fuzzy_splicing(t, s, next));
            accepted += table.can_splice<false>(s, next.id);
        }
    }
    EXPECT_GT(accepted, 0u);
}

}  // namespace
}  // namespace zcode
