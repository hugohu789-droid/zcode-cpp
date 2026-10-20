// SPDX-License-Identifier: GPL-2.0-or-later
//
// Output options (not in the Java original). The defaults are covered by the
// golden tests; here each option is checked against the unrestricted output.
#include <gtest/gtest.h>

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

}  // namespace
}  // namespace zcode
