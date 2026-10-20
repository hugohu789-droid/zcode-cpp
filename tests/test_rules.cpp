// SPDX-License-Identifier: GPL-2.0-or-later
//
// Every rule of data/rules.tsv against the truth tables dumped from the Java
// rule classes.
#include <gtest/gtest.h>

#include "letter_shape_sequence.h"
#include "rules.h"
#include "tables.h"
#include "test_util.h"

namespace zcode {
namespace {

const char* nature_name(Nature n) {
    switch (n) {
        case Nature::chagh: return "CHAGH";
        case Nature::hundii: return "HUNDII";
        case Nature::saarmag: return "SAARMAG";
    }
    return "?";
}

Tables load() {
    Tables t;
    std::string error;
    EXPECT_TRUE(Tables::parse_embedded(t, error)) << error;
    return t;
}

TEST(Rules, FrontMatchesJava) {
    Tables t = load();
    auto rows = test::read_tsv("java_rules_front.tsv");
    ASSERT_FALSE(rows.empty());
    const ShapeId a_head = *t.find_shape("A.aHead");
    const ShapeId e_head = *t.find_shape("E.eHead");
    std::size_t checked = 0;
    for (const auto& r : rows) {
        SCOPED_TRACE(r[0] + " " + r[1] + " " + r[2]);
        auto rule = t.find_rule(r[0]);
        ASSERT_TRUE(rule.has_value());
        ASSERT_TRUE(t.rules[*rule].front);
        // Rebuild the state the same way GoldenDump did: [prefix] + last.
        LetterShapeSequence lss;
        bool built = r[1] == "EMPTY";
        if (!built) {
            auto last = t.find_shape(r[1]);
            ASSERT_TRUE(last.has_value());
            for (int prefix : {-1, static_cast<int>(a_head), static_cast<int>(e_head)}) {
                LetterShapeSequence candidate;
                if (prefix >= 0) candidate.append(t.shapes[prefix]);
                candidate.append(t.shapes[*last]);
                if (nature_name(candidate.nature()) == r[2]) {
                    lss = candidate;
                    built = true;
                    break;
                }
            }
        }
        ASSERT_TRUE(built);
        EXPECT_EQ(nature_name(lss.nature()), r[2]);
        EXPECT_EQ(match_front(t, *rule, lss) ? "1" : "0", r[3]);
        ++checked;
    }
    EXPECT_EQ(checked, rows.size());
}

TEST(Rules, BehindMatchesJava) {
    Tables t = load();
    auto rows = test::read_tsv("java_rules_behind.tsv");
    ASSERT_FALSE(rows.empty());
    for (const auto& r : rows) {
        SCOPED_TRACE(r[0] + " " + r[1]);
        auto rule = t.find_rule(r[0]);
        ASSERT_TRUE(rule.has_value());
        ASSERT_FALSE(t.rules[*rule].front);
        auto next = t.find_shape(r[1]);
        ASSERT_TRUE(next.has_value());
        EXPECT_EQ(match_behind(t, *rule, &t.shapes[*next]) ? "1" : "0", r[2]);
    }
}

TEST(Rules, EveryRuleIsCovered) {
    Tables t = load();
    std::vector<bool> seen(t.rules.size(), false);
    for (const auto& r : test::read_tsv("java_rules_front.tsv")) seen[*t.find_rule(r[0])] = true;
    for (const auto& r : test::read_tsv("java_rules_behind.tsv")) seen[*t.find_rule(r[0])] = true;
    for (std::size_t i = 0; i < seen.size(); ++i) {
        EXPECT_TRUE(seen[i]) << t.rules[i].name;
    }
}

}  // namespace
}  // namespace zcode
