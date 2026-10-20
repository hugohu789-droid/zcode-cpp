// SPDX-License-Identifier: GPL-2.0-or-later
//
// data/*.tsv against the shape inventory and keyboard dumped from Java.
#include <gtest/gtest.h>

#include "keyboard_layout.h"
#include "tables.h"
#include "test_util.h"

namespace zcode {
namespace {

const char* location_name(Location l) {
    switch (l) {
        case Location::head: return "HEAD";
        case Location::middle: return "MIDDLE";
        case Location::tail: return "TAIL";
    }
    return "?";
}

std::string rule_name(const Tables& t, RuleId id) { return id == no_rule ? "-" : t.rules[id].name; }

Tables load() {
    Tables t;
    std::string error;
    EXPECT_TRUE(Tables::parse_embedded(t, error)) << error;
    return t;
}

TEST(Tables, ZCodeHas52Components) {
    Tables t = load();
    ASSERT_EQ(t.zcode_points.size(), 52u);
    for (CodePoint c : t.zcode_points) {
        EXPECT_TRUE(c >= 0x1865 && c <= 0x18AA) << std::hex << static_cast<unsigned>(c);
    }
}

TEST(Tables, ShapesMatchJava) {
    Tables t = load();
    auto rows = test::read_tsv("java_shapes.tsv");
    ASSERT_EQ(rows.size(), t.shapes.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const LetterShape& s = t.shapes[i];
        SCOPED_TRACE(r[0]);
        EXPECT_EQ(s.letter + "." + s.field, r[0]);
        EXPECT_EQ(s.latin_key, r[1]);
        EXPECT_EQ(location_name(s.location), r[2]);
        EXPECT_EQ(test::hex(s.shape), r[3]);
        EXPECT_EQ(rule_name(t, s.fuzzy_front), r[4]);
        EXPECT_EQ(rule_name(t, s.fuzzy_behind), r[5]);
        EXPECT_EQ(rule_name(t, s.severe_front), r[6]);
        EXPECT_EQ(rule_name(t, s.severe_behind), r[7]);
        // equals_first: the first shape LetterShape.equals() this one
        auto first = t.find_shape(r[8]);
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(t.shapes[*first].equals_class, s.equals_class);
        for (ShapeId j = 0; j < *first; ++j) {
            EXPECT_NE(t.shapes[j].equals_class, s.equals_class);
        }
    }
}

TEST(Tables, KeyboardMatchesJava) {
    Tables t = load();
    KeyboardLayout layout(t);
    auto rows = test::read_tsv("java_keyboard.tsv");
    ASSERT_EQ(rows.size(), t.keys.size() * 3);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        SCOPED_TRACE(r[0] + " " + r[2]);
        const KeyEntry& k = t.keys[i / 3];
        EXPECT_EQ(k.key, r[0]);
        EXPECT_EQ(t.letters[k.letter].name, r[1]);
        Location loc = static_cast<Location>(i % 3);
        EXPECT_EQ(location_name(loc), r[2]);
        std::string got;
        for (ShapeId id : layout.get(k.key, loc)) {
            if (!got.empty()) got += ' ';
            got += t.shapes[id].letter + "." + t.shapes[id].field;
        }
        EXPECT_EQ(got, r[3]);
    }
}

}  // namespace
}  // namespace zcode
