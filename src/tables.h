// SPDX-License-Identifier: GPL-2.0-or-later
//
// The declarative half of the engine, loaded from data/*.tsv.
//
// Ported from:
//   com.zorigt.ime.letter.ZCode                       -> data/zcode.tsv
//   com.zorigt.ime.letter.Letter, Letters, base/*      -> data/shapes.tsv
//   com.zorigt.ime.letter.LetterShape,
//     UnmodifiableLetterShape                         -> LetterShape (immutable after load)
//   com.zorigt.ime.letter.splice.FrontRuleHolder,
//     BehindRuleHolder, front/*, behind/*             -> data/rules.tsv, Rule
//   com.zorigt.ime.keyboard.KeyboardLayout KEYS/keyMap -> data/keyboard.tsv
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "types.h"

namespace zcode {

// LetterShape: (latinKey, shape, location) plus four rule references.
// equals_class implements LetterShape.equals(): two shapes are equal when
// latin_key, zcode and location are all equal, regardless of their rules.
struct LetterShape {
    ShapeId id = 0;
    std::string letter;  // Letters field name, e.g. "GALIG_H"
    std::string field;   // shape field name, e.g. "galigHHead"
    std::string latin_key;
    Location location = Location::head;
    ZString shape;
    RuleId fuzzy_front = no_rule;
    RuleId fuzzy_behind = no_rule;
    RuleId severe_front = no_rule;
    RuleId severe_behind = no_rule;
    ShapeId equals_class = 0;
};

// Letter: a latin key and its shapes in field declaration order.
struct Letter {
    std::string name;
    std::string latin_key;
    std::vector<ShapeId> shapes;
};

enum class RuleKind {
    front_empty,
    front_not_empty,
    last_in,
    last_not_in,
    last_at_not_in,
    nature_is,
    nature_is_not,
    behind_empty,
    behind_not_empty,
    next_in,
    next_key_in,
    negation,  // "not" in rules.tsv
};

struct Rule {
    std::string name;
    std::string java_class;
    bool front = true;
    RuleKind kind = RuleKind::front_empty;
    std::vector<bool> members;      // indexed by LetterShape::equals_class
    std::vector<std::string> keys;  // next_key_in
    Location location = Location::head;
    Nature nature = Nature::saarmag;
    RuleId inner = no_rule;  // negation
};

struct KeyEntry {
    std::string key;
    std::size_t letter = 0;  // index into Tables::letters
};

struct Tables {
    std::vector<std::string> zcode_names;  // index-aligned with zcode_points
    std::vector<CodePoint> zcode_points;
    std::vector<Letter> letters;
    std::vector<LetterShape> shapes;
    std::vector<Rule> rules;
    std::vector<KeyEntry> keys;  // KeyboardLayout.KEYS order
    std::size_t equals_classes = 0;

    std::optional<ShapeId> find_shape(std::string_view letter_dot_field) const;
    std::optional<RuleId> find_rule(std::string_view name) const;

    // Parses the four tables. Returns false and sets error on malformed input.
    static bool parse(Tables& out, std::string_view zcode_tsv, std::string_view shapes_tsv,
                      std::string_view rules_tsv, std::string_view keyboard_tsv, std::string& error);

    // The tables compiled into the library from data/*.tsv.
    static bool parse_embedded(Tables& out, std::string& error);
};

}  // namespace zcode
