// SPDX-License-Identifier: GPL-2.0-or-later
//
// Output options (not in the Java original). The defaults are covered by the
// golden tests; here each option is checked against the unrestricted output.
#include <gtest/gtest.h>

#include <algorithm>

#include "test_util.h"

namespace zcode {
namespace {

const Engine& engine() {
    static const Engine e = std::move(*Engine::create());
    return e;
}

Mode mode_of(const std::string& s) { return s == "severe" ? Mode::severe : Mode::fuzzy; }

TEST(Options, CountEqualsNumberOfCandidates) {
    for (const auto& r : test::read_tsv("golden_splice.tsv")) {
        std::vector<std::string> keys = test::split(r[1], ' ');
        ASSERT_EQ(std::to_string(engine().count(keys, mode_of(r[0]))), r[2]) << r[0] << " [" << r[1] << "]";
    }
    // inputs whose candidate lists are hashed in the golden files
    for (const auto& r : test::read_tsv("golden_e2e.tsv")) {
        auto cases = engine().decompose(test::unescape_to_utf8(r[1]));
        if (cases.size() != 1) continue;
        ASSERT_EQ(engine().count(cases[0], mode_of(r[0])), engine().splice(cases[0], mode_of(r[0])).size()) << r[1];
    }
}

TEST(Options, MaxCandidatesKeepsThePrefixInOriginalOrder) {
    for (const auto& r : test::read_tsv("golden_e2e.tsv")) {
        std::string input = test::unescape_to_utf8(r[1]);
        Mode mode = mode_of(r[0]);
        auto all = engine().shape(input, mode);
        for (std::size_t cap : {std::size_t{1}, std::size_t{2}, std::size_t{7}, std::size_t{100}}) {
            Options o;
            o.max_candidates = cap;
            auto some = engine().shape(input, mode, o);
            ASSERT_EQ(some.size(), std::min(cap, all.size())) << r[1];
            ASSERT_TRUE(std::equal(some.begin(), some.end(), all.begin())) << r[1];
            Session s(engine());
            auto keys = engine().decompose(input);
            if (keys.size() == 1) {
                for (const auto& k : keys[0]) s.push(k);
                ASSERT_EQ(s.candidates(mode, o), engine().splice(keys[0], mode, o)) << r[1];
            }
        }
    }
}

std::vector<std::u32string> stable_unique(const std::vector<std::u32string>& in) {
    std::vector<std::u32string> out;
    for (const auto& z : in) {
        if (std::find(out.begin(), out.end(), z) == out.end()) out.push_back(z);
    }
    return out;
}

TEST(Options, DeduplicateKeepsFirstOccurrences) {
    std::size_t inputs_with_duplicates = 0;
    for (const auto& r : test::read_tsv("golden_e2e.tsv")) {
        if (r[2].size() > 4) continue;  // keep the quadratic reference cheap (< 10000 candidates)
        std::string input = test::unescape_to_utf8(r[1]);
        Mode mode = mode_of(r[0]);
        auto all = engine().shape(input, mode);
        auto want = stable_unique(all);
        inputs_with_duplicates += want.size() != all.size();
        Options o;
        o.deduplicate = true;
        ASSERT_EQ(engine().shape(input, mode, o), want) << r[1];
        o.max_candidates = 3;
        want.resize(std::min<std::size_t>(want.size(), 3));
        ASSERT_EQ(engine().shape(input, mode, o), want) << r[1];
        auto keys = engine().decompose(input);
        if (keys.size() == 1) {
            Session s(engine());
            for (const auto& k : keys[0]) s.push(k);
            ASSERT_EQ(s.candidates(mode, o), want) << r[1];
        }
    }
    EXPECT_GT(inputs_with_duplicates, 0u);
}

}  // namespace
}  // namespace zcode
