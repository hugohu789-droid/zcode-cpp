// SPDX-License-Identifier: GPL-2.0-or-later
//
// Session must equal stateless Engine::splice() after every push and pop.
// Engine::splice() itself is checked against the Java golden data.
#include <gtest/gtest.h>

#include "test_util.h"

namespace zcode {
namespace {

TEST(Session, MatchesSpliceAfterEveryPushAndPop) {
    auto engine = Engine::create();
    ASSERT_TRUE(engine.has_value());
    std::size_t checks = 0;
    for (const auto& r : test::read_tsv("golden_splice.tsv")) {
        if (r[0] != "severe") continue;  // each key list appears once per mode
        std::vector<std::string> keys = test::split(r[1], ' ');
        Session s(*engine);
        std::vector<std::string> prefix;
        auto check = [&] {
            ASSERT_EQ(s.keys(), prefix);
            for (Mode m : {Mode::fuzzy, Mode::severe}) {
                ASSERT_EQ(s.candidates(m), engine->splice(prefix, m)) << "[" << r[1] << "] prefix " << prefix.size();
                ++checks;
            }
        };
        check();
        for (const std::string& k : keys) {
            s.push(k);
            prefix.push_back(k);
            check();
        }
        while (!prefix.empty()) {
            s.pop();
            prefix.pop_back();
            check();
        }
        s.pop();  // no-op when empty
        check();
        // re-type after clear: frontiers must be rebuilt from scratch
        for (const std::string& k : keys) s.push(k);
        s.clear();
        for (const std::string& k : keys) {
            s.push(k);
            prefix.push_back(k);
        }
        check();
    }
    EXPECT_GT(checks, 200000u);
}

TEST(Session, MatchesGoldenFinalCandidates) {
    auto engine = Engine::create();
    ASSERT_TRUE(engine.has_value());
    for (const auto& r : test::read_tsv("golden_splice.tsv")) {
        Session s(*engine);
        for (const std::string& k : test::split(r[1], ' ')) s.push(k);
        auto got = s.candidates(r[0] == "severe" ? Mode::severe : Mode::fuzzy);
        ASSERT_EQ(test::candidates(got), r[3]) << r[0] << " [" << r[1] << "]";
    }
}

}  // namespace
}  // namespace zcode
