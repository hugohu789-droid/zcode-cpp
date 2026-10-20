// SPDX-License-Identifier: GPL-2.0-or-later
//
// The golden oracle: output of the Java original (tools/golden-java), which the
// port must reproduce exactly, in the same order.
#include <gtest/gtest.h>

#include "test_util.h"

namespace zcode {
namespace {

const Engine& engine() {
    static const Engine e = [] {
        std::string error;
        auto made = Engine::create(&error);
        if (!made) {
            ADD_FAILURE() << error;
            std::abort();
        }
        return std::move(*made);
    }();
    return e;
}

Mode mode_of(const std::string& s) { return s == "severe" ? Mode::severe : Mode::fuzzy; }

TEST(Golden, Splice) {
    auto rows = test::read_tsv("golden_splice.tsv");
    ASSERT_GT(rows.size(), 60000u);
    std::size_t failures = 0;
    for (const auto& r : rows) {
        std::vector<std::string> keys = test::split(r[1], ' ');
        std::vector<std::u32string> got = engine().splice(keys, mode_of(r[0]));
        if (std::to_string(got.size()) != r[2] || test::candidates(got) != r[3]) {
            if (++failures <= 20) {
                ADD_FAILURE() << r[0] << " [" << r[1] << "]: expected " << r[2] << " got " << got.size() << "\n  want "
                              << r[3].substr(0, 300) << "\n  got  " << test::candidates(got).substr(0, 300);
            }
        }
    }
    EXPECT_EQ(failures, 0u);
}

TEST(Golden, Decompose) {
    auto rows = test::read_tsv("golden_decompose.tsv");
    ASSERT_FALSE(rows.empty());
    for (const auto& r : rows) {
        std::string input = test::unescape_to_utf8(r[0]);
        auto cases = engine().decompose(input);
        std::string got;
        for (std::size_t i = 0; i < cases.size(); ++i) {
            if (i > 0) got += '|';
            for (std::size_t k = 0; k < cases[i].size(); ++k) {
                if (k > 0) got += ' ';
                got += cases[i][k];
            }
        }
        EXPECT_EQ(std::to_string(cases.size()), r[1]) << "[" << r[0] << "]";
        EXPECT_EQ(got, r[2]) << "[" << r[0] << "]";
    }
}

TEST(Golden, EndToEnd) {
    auto rows = test::read_tsv("golden_e2e.tsv");
    ASSERT_FALSE(rows.empty());
    for (const auto& r : rows) {
        std::string input = test::unescape_to_utf8(r[1]);
        std::vector<std::u32string> got = engine().shape(input, mode_of(r[0]));
        EXPECT_EQ(std::to_string(got.size()), r[2]) << r[0] << " [" << r[1] << "]";
        EXPECT_EQ(test::candidates(got), r[3]) << r[0] << " [" << r[1] << "]";
    }
}

TEST(Golden, EndToEndUtf8MatchesCodePoints) {
    for (const char* input : {"mongol", "Hara", "bagsi"}) {
        auto z = engine().shape(input, Mode::severe);
        auto u = engine().shape_utf8(input, Mode::severe);
        ASSERT_EQ(z.size(), u.size());
        for (std::size_t i = 0; i < z.size(); ++i) {
            EXPECT_EQ(to_utf8(z[i]), u[i]);
            EXPECT_EQ(u[i].size(), z[i].size() * 3);  // every ZCode code point is 3 bytes in UTF-8
        }
    }
}

TEST(Golden, BTailKeyFilter) {
    auto rows = test::read_tsv("golden_btail.tsv");
    ASSERT_FALSE(rows.empty());
    for (const auto& r : rows) {
        std::u32string z = r[0] == "-" ? std::u32string() : test::unhex(r[0]);
        EXPECT_EQ(engine().key_filter_accepts("b", z) ? "1" : "0", r[1]) << r[0];
        EXPECT_TRUE(engine().key_filter_accepts("a", z)) << r[0];  // no filter registered for "a-tail"
    }
}

}  // namespace
}  // namespace zcode
