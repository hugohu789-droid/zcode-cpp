// SPDX-License-Identifier: GPL-2.0-or-later
//
// Loader for data/*.tsv. Ported from the static initialisers of
// com.zorigt.ime.letter.ZCode, Letters, base/*, FrontRuleHolder,
// BehindRuleHolder and KeyboardLayout (see tables.h for the mapping).
#include "tables.h"

#include <map>
#include <tuple>
#include <utility>

namespace zcode {
namespace {

using Row = std::vector<std::string_view>;

// Splits a TSV text into rows; skips blank lines and '#' comments, tolerates CRLF.
std::vector<Row> read_rows(std::string_view text) {
    std::vector<Row> rows;
    while (!text.empty()) {
        std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        Row row;
        while (true) {
            std::size_t tab = line.find('\t');
            row.push_back(line.substr(0, tab));
            if (tab == std::string_view::npos) {
                break;
            }
            line = line.substr(tab + 1);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<std::string_view> words(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') {
            ++i;
        }
        std::size_t j = i;
        while (j < s.size() && s[j] != ' ') {
            ++j;
        }
        if (j > i) {
            out.push_back(s.substr(i, j - i));
        }
        i = j;
    }
    return out;
}

std::optional<Location> parse_location(std::string_view s) {
    if (s == "HEAD") return Location::head;
    if (s == "MIDDLE") return Location::middle;
    if (s == "TAIL") return Location::tail;
    return std::nullopt;
}

std::optional<Nature> parse_nature(std::string_view s) {
    if (s == "CHAGH") return Nature::chagh;
    if (s == "HUNDII") return Nature::hundii;
    if (s == "SAARMAG") return Nature::saarmag;
    return std::nullopt;
}

std::optional<CodePoint> parse_code_point(std::string_view s) {
    if (s.size() < 3 || s.substr(0, 2) != "U+") {
        return std::nullopt;
    }
    CodePoint v = 0;
    for (char c : s.substr(2)) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return std::nullopt;
        v = v * 16 + static_cast<CodePoint>(d);
    }
    return v;
}

std::optional<RuleKind> parse_kind(std::string_view s) {
    static constexpr std::pair<std::string_view, RuleKind> kinds[] = {
        {"front_empty", RuleKind::front_empty},       {"front_not_empty", RuleKind::front_not_empty},
        {"last_in", RuleKind::last_in},               {"last_not_in", RuleKind::last_not_in},
        {"last_at_not_in", RuleKind::last_at_not_in}, {"nature_is", RuleKind::nature_is},
        {"nature_is_not", RuleKind::nature_is_not},   {"behind_empty", RuleKind::behind_empty},
        {"behind_not_empty", RuleKind::behind_not_empty}, {"next_in", RuleKind::next_in},
        {"next_key_in", RuleKind::next_key_in},       {"not", RuleKind::negation},
    };
    for (const auto& [name, kind] : kinds) {
        if (name == s) return kind;
    }
    return std::nullopt;
}

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

}  // namespace

std::optional<ShapeId> Tables::find_shape(std::string_view id) const {
    for (const LetterShape& s : shapes) {
        if (id.size() == s.letter.size() + 1 + s.field.size() && id.substr(0, s.letter.size()) == s.letter &&
            id[s.letter.size()] == '.' && id.substr(s.letter.size() + 1) == s.field) {
            return s.id;
        }
    }
    return std::nullopt;
}

std::optional<RuleId> Tables::find_rule(std::string_view name) const {
    for (std::size_t i = 0; i < rules.size(); ++i) {
        if (rules[i].name == name) return static_cast<RuleId>(i);
    }
    return std::nullopt;
}

bool Tables::parse(Tables& out, std::string_view zcode_tsv, std::string_view shapes_tsv,
                   std::string_view rules_tsv, std::string_view keyboard_tsv, std::string& error) {
    Tables t;

    // --- zcode.tsv: name, code point (ZCode.java)
    for (const Row& r : read_rows(zcode_tsv)) {
        auto cp = r.size() == 2 ? parse_code_point(r[1]) : std::nullopt;
        if (!cp) return fail(error, "zcode.tsv: bad row for '" + std::string(r[0]) + "'");
        t.zcode_names.emplace_back(r[0]);
        t.zcode_points.push_back(*cp);
    }
    auto component = [&t](std::string_view name) -> std::optional<CodePoint> {
        for (std::size_t i = 0; i < t.zcode_names.size(); ++i) {
            if (t.zcode_names[i] == name) return t.zcode_points[i];
        }
        return std::nullopt;
    };

    // --- shapes.tsv (base/* via Letters); rule names are resolved after rules.tsv
    std::vector<Row> shape_rows = read_rows(shapes_tsv);
    std::map<std::tuple<std::string, ZString, Location>, ShapeId> classes;
    for (const Row& r : shape_rows) {
        if (r.size() != 9) return fail(error, "shapes.tsv: expected 9 columns");
        LetterShape s;
        s.id = static_cast<ShapeId>(t.shapes.size());
        s.letter = r[0];
        s.field = r[1];
        s.latin_key = r[2];
        auto loc = parse_location(r[3]);
        if (!loc) return fail(error, "shapes.tsv: bad location for " + s.letter + "." + s.field);
        s.location = *loc;
        for (std::string_view w : words(r[4])) {
            auto cp = component(w);
            if (!cp) return fail(error, "shapes.tsv: unknown zcode component '" + std::string(w) + "'");
            s.shape.push_back(*cp);
        }
        if (s.shape.empty()) return fail(error, "shapes.tsv: empty shape for " + s.letter + "." + s.field);
        auto key = std::make_tuple(s.latin_key, s.shape, s.location);
        auto it = classes.find(key);
        if (it == classes.end()) {
            it = classes.emplace(key, static_cast<ShapeId>(classes.size())).first;
        }
        s.equals_class = it->second;

        if (t.letters.empty() || t.letters.back().name != s.letter) {
            for (const Letter& l : t.letters) {
                if (l.name == s.letter) return fail(error, "shapes.tsv: letter rows not contiguous: " + s.letter);
            }
            t.letters.push_back(Letter{s.letter, s.latin_key, {}});
        }
        if (t.letters.back().latin_key != s.latin_key) {
            return fail(error, "shapes.tsv: mixed latin keys in letter " + s.letter);
        }
        t.letters.back().shapes.push_back(s.id);
        t.shapes.push_back(std::move(s));
    }
    t.equals_classes = classes.size();

    // --- rules.tsv (front/*, behind/*), two passes so "not" can refer to any rule
    std::vector<Row> rule_rows = read_rows(rules_tsv);
    for (const Row& r : rule_rows) {
        if (r.size() != 5) return fail(error, "rules.tsv: expected 5 columns");
        Rule rule;
        rule.name = r[0];
        rule.java_class = r[4];
        if (r[1] != "front" && r[1] != "behind") return fail(error, "rules.tsv: bad side for " + rule.name);
        rule.front = r[1] == "front";
        auto kind = parse_kind(r[2]);
        if (!kind) return fail(error, "rules.tsv: bad kind for " + rule.name);
        rule.kind = *kind;
        if (t.find_rule(rule.name)) return fail(error, "rules.tsv: duplicate rule " + rule.name);
        t.rules.push_back(std::move(rule));
    }
    for (std::size_t i = 0; i < rule_rows.size(); ++i) {
        Rule& rule = t.rules[i];
        std::vector<std::string_view> args = words(rule_rows[i][3]);
        if (args.size() == 1 && args[0] == "-") {
            args.clear();
        }
        auto shape_set = [&](std::size_t from) -> bool {
            rule.members.assign(t.equals_classes, false);
            for (std::size_t a = from; a < args.size(); ++a) {
                auto id = t.find_shape(args[a]);
                if (!id) return fail(error, "rules.tsv: " + rule.name + ": unknown shape " + std::string(args[a]));
                rule.members[t.shapes[*id].equals_class] = true;
            }
            return true;
        };
        switch (rule.kind) {
            case RuleKind::front_empty:
            case RuleKind::front_not_empty:
            case RuleKind::behind_empty:
            case RuleKind::behind_not_empty:
                if (!args.empty()) return fail(error, "rules.tsv: " + rule.name + " takes no arguments");
                break;
            case RuleKind::last_in:
            case RuleKind::last_not_in:
            case RuleKind::next_in:
                if (!shape_set(0)) return false;
                break;
            case RuleKind::last_at_not_in: {
                auto loc = args.empty() ? std::nullopt : parse_location(args[0]);
                if (!loc) return fail(error, "rules.tsv: " + rule.name + ": bad location");
                rule.location = *loc;
                if (!shape_set(1)) return false;
                break;
            }
            case RuleKind::nature_is:
            case RuleKind::nature_is_not: {
                auto n = args.size() == 1 ? parse_nature(args[0]) : std::nullopt;
                if (!n) return fail(error, "rules.tsv: " + rule.name + ": bad nature");
                rule.nature = *n;
                break;
            }
            case RuleKind::next_key_in:
                for (std::string_view a : args) rule.keys.emplace_back(a);
                break;
            case RuleKind::negation: {
                auto inner = args.size() == 1 ? t.find_rule(args[0]) : std::nullopt;
                if (!inner || t.rules[*inner].front != rule.front || *inner == i) {
                    return fail(error, "rules.tsv: " + rule.name + ": bad negated rule");
                }
                rule.inner = *inner;
                break;
            }
        }
        bool front_kind = rule.kind == RuleKind::front_empty || rule.kind == RuleKind::front_not_empty ||
                          rule.kind == RuleKind::last_in || rule.kind == RuleKind::last_not_in ||
                          rule.kind == RuleKind::last_at_not_in || rule.kind == RuleKind::nature_is ||
                          rule.kind == RuleKind::nature_is_not;
        if (rule.kind != RuleKind::negation && front_kind != rule.front) {
            return fail(error, "rules.tsv: " + rule.name + ": kind does not match side");
        }
    }

    // --- resolve the four rule references of every shape
    for (std::size_t i = 0; i < shape_rows.size(); ++i) {
        LetterShape& s = t.shapes[i];
        RuleId* refs[] = {&s.fuzzy_front, &s.fuzzy_behind, &s.severe_front, &s.severe_behind};
        for (int c = 0; c < 4; ++c) {
            std::string_view name = shape_rows[i][5 + c];
            if (name == "-") continue;
            auto id = t.find_rule(name);
            bool want_front = c % 2 == 0;
            if (!id || t.rules[*id].front != want_front) {
                return fail(error, "shapes.tsv: " + s.letter + "." + s.field + ": bad rule " + std::string(name));
            }
            *refs[c] = *id;
        }
    }

    // --- keyboard.tsv (KeyboardLayout.KEYS, keyMap)
    for (const Row& r : read_rows(keyboard_tsv)) {
        if (r.size() != 2) return fail(error, "keyboard.tsv: expected 2 columns");
        std::optional<std::size_t> letter;
        for (std::size_t l = 0; l < t.letters.size(); ++l) {
            if (t.letters[l].name == r[1]) letter = l;
        }
        if (!letter) return fail(error, "keyboard.tsv: unknown letter " + std::string(r[1]));
        t.keys.push_back(KeyEntry{std::string(r[0]), *letter});
    }

    out = std::move(t);
    return true;
}

}  // namespace zcode
