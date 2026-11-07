// SPDX-License-Identifier: GPL-2.0-or-later
//
// The dictionary half, against the Java original.
//
// testdata/golden_dict.tsv is what the running east-mod/ime service answered
// (tools/golden_dict.py). It cannot be produced offline like the other golden
// files -- the original keeps its dictionary in MySQL -- so these tests
// replay a capture rather than regenerating one.
//
// The port matches that capture exactly, with one declared exception: words
// that score the same come out in a different order. The original's order
// there is its HashMap's bucket order; this port sorts ties by frequency and
// then by code point, which is a rule that can be written down. So the
// assertions below are "same words, same metrics, same sequence of scores",
// and additionally "same order" wherever the original's own order was not a
// tie. Anything weaker would let a real regression through; anything stronger
// would be asserting Java's hash function.

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "dict/java_float.h"
#include "test_util.h"
#include "zcode/dictionary.h"
#include "zcode/zcode.h"

namespace zcode {
namespace {

const Engine& engine() {
    static const Engine* e = [] {
        std::string error;
        auto created = Engine::create(&error);
        EXPECT_TRUE(created.has_value()) << error;
        return new Engine(std::move(*created));
    }();
    return *e;
}

const Dictionary& dictionary() {
    static const Dictionary* d = [] {
        std::string error;
        auto created = Dictionary::create(&error);
        EXPECT_TRUE(created.has_value()) << error;
        return new Dictionary(std::move(*created));
    }();
    return *d;
}

// One row of golden_dict.tsv.
struct Golden {
    std::string kind;
    std::string input;  // UTF-8
    std::vector<std::string> items;
};

const std::vector<Golden>& golden() {
    static const std::vector<Golden>* rows = [] {
        auto* out = new std::vector<Golden>();
        for (const test::Row& r : test::read_tsv("golden_dict.tsv")) {
            if (r.size() < 4) {
                continue;
            }
            Golden g;
            g.kind = r[0];
            g.input = test::unescape_to_utf8(r[1]);
            if (!r[3].empty()) {
                g.items = test::split(r[3], ' ');
            }
            out->push_back(std::move(g));
        }
        return out;
    }();
    return *rows;
}

// A `score` item: "HEX|frequency|frequencyScore|levenshteinD|levenshteinScore
// |jaroWinklerScore|score".
struct Scored {
    std::string word_hex;
    std::vector<std::string> metrics;
};

Scored parse_scored(const std::string& item) {
    std::vector<std::string> parts = test::split(item, '|');
    Scored s;
    s.word_hex = parts.empty() ? "" : parts[0];
    s.metrics.assign(parts.begin() + (parts.empty() ? 0 : 1), parts.end());
    return s;
}

std::vector<std::string> metrics_of(const SuggestWord& w) {
    return {std::to_string(w.frequency),
            dict::java_float_to_string(w.frequency_score),
            std::to_string(w.levenshtein_d),
            dict::java_float_to_string(w.levenshtein_score),
            dict::java_float_to_string(w.jaro_winkler_score),
            dict::java_float_to_string(w.score)};
}

std::vector<std::string> hex_all(const std::vector<ZString>& words) {
    std::vector<std::string> out;
    out.reserve(words.size());
    for (const ZString& w : words) {
        out.push_back(test::hex(w));
    }
    return out;
}

}  // namespace

TEST(Dictionary, EmbeddedWordListLoads) {
    EXPECT_EQ(dictionary().size(), 38743u);
}

// The structural claim the index rests on: the original's BK tree and the
// trie here find the same words with the same metrics. Order is not compared
// -- a match's internal order cannot reach the output, because fuzzyMakeWord
// merges every match into a map keyed by the word -- but everything else is.
TEST(Dictionary, TrieAgreesWithTheBkTreeReference) {
    int compared = 0;
    for (const test::Row& r : test::read_tsv("golden_e2e.tsv")) {
        if (r.size() < 4 || r[0] != "fuzzy" || r[3].empty() || r[3][0] == 'H') {
            continue;
        }
        // Every spliced sequence the original would have looked up.
        for (const std::string& item : test::split(r[3], ' ')) {
            if (item.empty()) {
                continue;
            }
            const ZString sequence = test::unhex(item);
            auto fast = dictionary().match(sequence);
            auto slow = dictionary().match_bk_reference(sequence);
            const auto by_word = [](const SuggestWord& a, const SuggestWord& b) {
                return a.str < b.str;
            };
            std::sort(fast.begin(), fast.end(), by_word);
            std::sort(slow.begin(), slow.end(), by_word);
            ASSERT_EQ(fast.size(), slow.size()) << "for " << item;
            for (std::size_t i = 0; i < fast.size(); ++i) {
                ASSERT_EQ(fast[i].str, slow[i].str) << "for " << item;
                ASSERT_EQ(fast[i].frequency, slow[i].frequency);
                ASSERT_EQ(fast[i].levenshtein_d, slow[i].levenshtein_d);
                ASSERT_EQ(fast[i].score, slow[i].score) << "for " << item;
            }
            ++compared;
            if (compared >= 400) {
                return;  // enough: this is the slow reference path
            }
        }
    }
    EXPECT_GT(compared, 0);
}

// /ime/severe/{key} uses no dictionary: it is decompose + splice, which is
// exactly Engine::shape(). That identity is what this asserts.
//
// It deliberately does not assert equality with the capture. The capture
// comes from the *unmodified* original, while this port carries the two
// fixes listed in README under "Deviations from the Java original", so the
// two differ wherever a fix applies. The splicer itself is pinned against
// the patched original over the whole golden vocabulary in test_golden.cpp,
// so re-pinning it here against an unpatched capture would only encode the
// bugs. What is checked instead is that the fixes stay rare and known: a
// jump in this count means something changed in the splicer.
TEST(Dictionary, SevereIsExactlyTheSplicer) {
    const Ime ime(engine(), dictionary());
    int checked = 0;
    int differs_from_unpatched = 0;
    std::vector<std::string> examples;
    for (const Golden& g : golden()) {
        if (g.kind != "severe") {
            continue;
        }
        const std::vector<ZString> mine = ime.severe_make_word(g.input);
        ASSERT_EQ(mine, engine().shape(g.input, Mode::severe)) << "for input " << g.input;
        if (hex_all(mine) != g.items) {
            ++differs_from_unpatched;
            if (examples.size() < 10) {
                examples.push_back(g.input);
            }
        }
        ++checked;
    }
    EXPECT_GT(checked, 1000);
    std::string joined;
    for (const std::string& e : examples) {
        joined += " " + e;
    }
    EXPECT_LE(differs_from_unpatched, 30)
        << differs_from_unpatched << " of " << checked
        << " inputs differ from the unpatched original, which is more than the"
           " two documented fixes should account for. Examples:" << joined;
}

// /ime/fuzzy/score/{key}: the words, every metric behind the ranking, and
// the sequence of scores.
TEST(Dictionary, FuzzyScoresMatchTheOriginal) {
    const Ime ime(engine(), dictionary());
    int checked = 0;
    int same_order = 0;
    int reordered_ties = 0;
    for (const Golden& g : golden()) {
        if (g.kind != "score") {
            continue;
        }
        const std::vector<SuggestWord> mine = ime.fuzzy_make_word(g.input);
        ASSERT_EQ(mine.size(), g.items.size()) << "for input " << g.input;

        std::map<std::string, std::vector<std::string>> theirs;
        std::vector<std::string> their_scores;
        for (const std::string& item : g.items) {
            const Scored s = parse_scored(item);
            theirs[s.word_hex] = s.metrics;
            their_scores.push_back(s.metrics.back());
        }

        std::vector<std::string> my_scores;
        std::vector<std::string> my_order;
        for (const SuggestWord& w : mine) {
            const std::string key = test::hex(w.str);
            my_order.push_back(key);
            const auto it = theirs.find(key);
            ASSERT_NE(it, theirs.end())
                << "the port found " << key << " for " << g.input
                << ", the original did not";
            EXPECT_EQ(metrics_of(w), it->second) << "for " << key << " of " << g.input;
            my_scores.push_back(dict::java_float_to_string(w.score));
        }
        // Same scores in the same places: the two lists are the same sort of
        // the same data, so any difference left is inside a tie group.
        EXPECT_EQ(my_scores, their_scores) << "for input " << g.input;

        std::vector<std::string> their_order;
        for (const std::string& item : g.items) {
            their_order.push_back(parse_scored(item).word_hex);
        }
        if (my_order == their_order) {
            ++same_order;
        } else {
            ++reordered_ties;
            // Only words sharing a score may have moved.
            for (std::size_t i = 0; i < my_order.size(); ++i) {
                if (my_order[i] != their_order[i]) {
                    EXPECT_EQ(my_scores[i], their_scores[i])
                        << "position " << i << " of " << g.input
                        << " differs at a point where the scores differ too";
                }
            }
        }
        ++checked;
    }
    EXPECT_GT(checked, 1000);
    // A guard against the tie rule quietly starting to disagree everywhere:
    // the capture had 532 of 605 whole-word inputs matching order for order.
    EXPECT_GT(same_order, reordered_ties);
}

// /ime/{key}: severe and fuzzy merged. Its order comes from the fuzzy list,
// so it can only be compared exactly where the fuzzy order matched.
TEST(Dictionary, MakeWordMatchesTheOriginal) {
    const Ime ime(engine(), dictionary());
    std::map<std::string, std::vector<std::string>> fuzzy_order;
    for (const Golden& g : golden()) {
        if (g.kind != "score") {
            continue;
        }
        std::vector<std::string> order;
        for (const std::string& item : g.items) {
            order.push_back(parse_scored(item).word_hex);
        }
        fuzzy_order[g.input] = std::move(order);
    }

    // What the unmodified original answered for severe, so the inputs the
    // two documented fixes touch can be told apart from real differences.
    std::map<std::string, std::vector<std::string>> their_severe;
    for (const Golden& g : golden()) {
        if (g.kind == "severe") {
            their_severe[g.input] = g.items;
        }
    }

    int exact = 0;
    int set_only = 0;
    int patched = 0;
    for (const Golden& g : golden()) {
        if (g.kind != "make") {
            continue;
        }
        std::vector<std::string> mine = hex_all(ime.make_word(g.input));
        std::vector<std::string> theirs = g.items;

        const std::vector<std::string> my_severe = hex_all(ime.severe_make_word(g.input));
        if (my_severe != their_severe[g.input]) {
            // A fix changed the severe half, so the merge must differ too.
            // Every difference has to be a severe word on one side or the
            // other; a difference among the dictionary words would not be
            // explained by the fix and is a real failure.
            ++patched;
            std::vector<std::string> only_mine;
            std::vector<std::string> only_theirs;
            std::vector<std::string> a = mine;
            std::vector<std::string> b = theirs;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                                std::back_inserter(only_mine));
            std::set_difference(b.begin(), b.end(), a.begin(), a.end(),
                                std::back_inserter(only_theirs));
            for (const std::string& w : only_mine) {
                EXPECT_NE(std::find(my_severe.begin(), my_severe.end(), w), my_severe.end())
                    << w << " appears for " << g.input
                    << " and is not one of the port's severe results";
            }
            const std::vector<std::string>& ts = their_severe[g.input];
            for (const std::string& w : only_theirs) {
                EXPECT_NE(std::find(ts.begin(), ts.end(), w), ts.end())
                    << w << " is missing for " << g.input
                    << " and is not one of the original's severe results";
            }
            continue;
        }

        std::vector<std::string> my_fuzzy;
        for (const SuggestWord& w : ime.fuzzy_make_word(g.input)) {
            my_fuzzy.push_back(test::hex(w.str));
        }
        if (my_fuzzy == fuzzy_order[g.input]) {
            EXPECT_EQ(mine, theirs) << "for input " << g.input;
            ++exact;
        } else {
            std::sort(mine.begin(), mine.end());
            std::sort(theirs.begin(), theirs.end());
            EXPECT_EQ(mine, theirs) << "for input " << g.input;
            ++set_only;
        }
    }
    EXPECT_GT(exact, 1000);
    EXPECT_GT(exact, set_only);
    EXPECT_LE(patched, 30);
}

TEST(Dictionary, JavaFloatFormatting) {
    // Float.toString() always writes a fractional digit and switches to
    // scientific notation outside [1e-3, 1e7).
    EXPECT_EQ(dict::java_float_to_string(1.0F), "1.0");
    EXPECT_EQ(dict::java_float_to_string(3.0F), "3.0");
    EXPECT_EQ(dict::java_float_to_string(0.0F), "0.0");
    EXPECT_EQ(dict::java_float_to_string(-0.0F), "-0.0");
    EXPECT_EQ(dict::java_float_to_string(0.5F), "0.5");
    EXPECT_EQ(dict::java_float_to_string(0.79587686F), "0.79587686");
    EXPECT_EQ(dict::java_float_to_string(100.0F), "100.0");
    EXPECT_EQ(dict::java_float_to_string(1.0E7F), "1.0E7");
    EXPECT_EQ(dict::java_float_to_string(0.001F), "0.001");
    EXPECT_EQ(dict::java_float_to_string(1.0E-4F), "1.0E-4");
    EXPECT_EQ(dict::java_float_to_string(-2.5F), "-2.5");
}

TEST(Dictionary, BlankAndUnknownInput) {
    const Ime ime(engine(), dictionary());
    EXPECT_TRUE(ime.fuzzy_make_word("").empty());
    EXPECT_TRUE(ime.fuzzy_make_word("   ").empty());
    EXPECT_TRUE(ime.make_word("").empty());
    // Bytes outside the layout are skipped, as the Decomposer skips them.
    EXPECT_EQ(ime.fuzzy_make_word("1b2a3g4s5i").size(), ime.fuzzy_make_word("bagsi").size());
}

TEST(Dictionary, NegativeCacheDoesNotChangeResults) {
    Ime cached(engine(), dictionary());
    Ime uncached(engine(), dictionary());
    uncached.set_negative_cache_enabled(false);
    for (const char* input : {"bagsi", "mongol", "bicig", "sain", "b", "ba", "bag"}) {
        // Twice, so the cache is warm on the second call.
        cached.fuzzy_make_word(input);
        const std::vector<SuggestWord> warm = cached.fuzzy_make_word(input);
        const std::vector<SuggestWord> plain = uncached.fuzzy_make_word(input);
        ASSERT_EQ(warm.size(), plain.size()) << input;
        for (std::size_t i = 0; i < warm.size(); ++i) {
            EXPECT_EQ(warm[i].str, plain[i].str) << input << " at " << i;
            EXPECT_EQ(warm[i].score, plain[i].score);
        }
    }
}

}  // namespace zcode
