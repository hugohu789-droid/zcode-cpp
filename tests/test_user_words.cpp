// SPDX-License-Identifier: GPL-2.0-or-later
//
// The user's own words and habits. Nothing here is ported: the original has
// neither, so the tests that matter are the ones pinning what the feature
// must not do -- change an answer when the store is empty -- and what the
// decay must do over time.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "zcode/dictionary.h"
#include "zcode/zcode.h"

namespace zcode {
namespace {

// A word of the compiled dictionary, and one that is not in it: `bicig` is
// a real word the original's vocabulary does not hold, which is exactly the
// case a user's own list exists for.
constexpr char32_t kInDictionary[] = {0x1873, 0x186A, 0x186A, 0x186A, 0x1894, 0x186B, 0};
constexpr char32_t kNotInDictionary[] = {0x1873, 0x186C, 0x18A6, 0x186C, 0x186C, 0x1868, 0};

class UserWordsTest : public ::testing::Test {
protected:
    UserWordsTest() {
        std::string error;
        auto created = Engine::create(&error);
        EXPECT_TRUE(created.has_value()) << error;
        engine_ = std::move(created);
        auto dictionary = Dictionary::create(&error);
        EXPECT_TRUE(dictionary.has_value()) << error;
        dictionary_ = std::move(dictionary);
    }

    Ime MakeIme() { return Ime(*engine_, *dictionary_); }

    static std::vector<ZString> Texts(const std::vector<SuggestWord>& words) {
        std::vector<ZString> out;
        out.reserve(words.size());
        for (const SuggestWord& w : words) out.push_back(w.str);
        return out;
    }

    static std::size_t PositionOf(const std::vector<SuggestWord>& words, const ZString& text) {
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (words[i].str == text) return i;
        }
        return words.size();
    }

    std::optional<Engine> engine_;
    std::optional<Dictionary> dictionary_;
};

// ---- the store itself ---------------------------------------------------

TEST_F(UserWordsTest, LearnCountsAndRemembersWhen) {
    UserWords store;
    const ZString word(kInDictionary);
    store.learn(word, 5);
    store.learn(word, 9);
    ASSERT_EQ(store.size(), 1U);
    const std::vector<UserWords::Entry> entries = store.entries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].word, word);
    EXPECT_EQ(entries[0].count, 2U);
    EXPECT_EQ(entries[0].tick, 9U);
}

// A host replays its store at startup; replaying it twice must not inflate
// a habit, or a restart would look like use.
TEST_F(UserWordsTest, RestoringTwiceIsNotTwoUses) {
    UserWords store;
    UserWords::Entry entry;
    entry.word = ZString(kInDictionary);
    entry.count = 7;
    entry.tick = 100;
    store.restore(entry);
    store.restore(entry);
    ASSERT_EQ(store.size(), 1U);
    EXPECT_EQ(store.entries()[0].count, 7U);
}

TEST_F(UserWordsTest, RestoreKeepsTheStrongerAndTheLater) {
    UserWords store;
    UserWords::Entry old_entry;
    old_entry.word = ZString(kInDictionary);
    old_entry.count = 9;
    old_entry.tick = 10;
    UserWords::Entry new_entry = old_entry;
    new_entry.count = 2;
    new_entry.tick = 90;
    store.restore(old_entry);
    store.restore(new_entry);
    EXPECT_EQ(store.entries()[0].count, 9U);
    EXPECT_EQ(store.entries()[0].tick, 90U);
}

TEST_F(UserWordsTest, ForgetAndClear) {
    UserWords store;
    store.learn(ZString(kInDictionary), 1);
    store.learn(ZString(kNotInDictionary), 1);
    EXPECT_EQ(store.size(), 2U);
    store.forget(ZString(kInDictionary));
    EXPECT_EQ(store.size(), 1U);
    store.clear();
    EXPECT_EQ(store.size(), 0U);
}

TEST_F(UserWordsTest, AWordNeverLearnedHasNoBonus) {
    UserWords store;
    EXPECT_EQ(store.bonus(ZString(kInDictionary), 0), 0.0F);
}

// The habit model: more uses is stronger, but with diminishing returns, and
// time pulls it back down.
TEST_F(UserWordsTest, TheBonusSaturatesInUseAndDecaysInTime) {
    UserWords store;
    const ZString word(kInDictionary);
    store.learn(word, 0);
    const float once = store.bonus(word, 0);
    store.learn(word, 0);
    const float twice = store.bonus(word, 0);
    for (int i = 0; i < 17; ++i) store.learn(word, 0);
    const float nineteen = store.bonus(word, 0);
    store.learn(word, 0);
    const float twenty = store.bonus(word, 0);

    EXPECT_GT(twice, once);
    EXPECT_GT(twenty, nineteen);
    EXPECT_LT(twenty, store.options().weight);  // never reaches the weight
    // Diminishing: what the twentieth use adds is a fraction of what the
    // second did, so a word used once still counts for something and a word
    // used hundreds of times does not drown everything else out.
    EXPECT_LT(twenty - nineteen, (twice - once) / 10.0F);

    // One half-life later, half as much.
    const float later = store.bonus(word, static_cast<std::uint64_t>(store.options().half_life));
    EXPECT_NEAR(later, twenty / 2.0F, 1e-4F);
    // Long enough and it is as good as gone.
    EXPECT_LT(store.bonus(word, 1000), 0.001F);
}

// A host's clock that jumps backwards (a timezone change, a restored
// backup) must not turn into an enormous bonus.
TEST_F(UserWordsTest, ATickInThePastIsNotABonusMultiplier) {
    UserWords store;
    const ZString word(kInDictionary);
    store.learn(word, 500);
    EXPECT_EQ(store.bonus(word, 100), store.bonus(word, 500));
}

TEST_F(UserWordsTest, MatchUsesTheDictionarysOwnRule) {
    UserWords store;
    const ZString word(kInDictionary);  // six code points
    store.learn(word, 0);

    // The query is a subsequence, and the length is inside [L, 2L).
    EXPECT_EQ(store.match(word).size(), 1U);
    EXPECT_EQ(store.match(word.substr(0, 4)).size(), 1U);
    // Too short: six is not less than 2*3.
    EXPECT_TRUE(store.match(word.substr(0, 3)).empty());
    // Not a subsequence.
    ZString scrambled = word;
    std::reverse(scrambled.begin(), scrambled.end());
    EXPECT_TRUE(store.match(scrambled).empty());
    // A different first code point: the filter the dictionary applies too.
    ZString other = word.substr(0, 4);
    other[0] = 0x1894;
    EXPECT_TRUE(store.match(other).empty());
}

// ---- what it does to an answer -----------------------------------------

// The invariant the whole comparison against the Java service rests on.
TEST_F(UserWordsTest, AnEmptyStoreChangesNothing) {
    Ime plain = MakeIme();
    Ime with_store = MakeIme();
    UserWords empty;
    with_store.set_user_words(&empty);
    with_store.set_now(1000);

    for (const char* latin : {"bagsi", "monggol", "ene", "sain", "bicig"}) {
        const std::vector<SuggestWord> a = plain.fuzzy_make_word(latin);
        const std::vector<SuggestWord> b = with_store.fuzzy_make_word(latin);
        ASSERT_EQ(a.size(), b.size()) << latin;
        for (std::size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(a[i].str, b[i].str) << latin << ' ' << i;
            EXPECT_EQ(a[i].score, b[i].score) << latin << ' ' << i;
            EXPECT_EQ(a[i].frequency, b[i].frequency) << latin << ' ' << i;
        }
    }
}

TEST_F(UserWordsTest, ALearnedWordRisesButTheListIsOtherwiseTheSame) {
    Ime plain = MakeIme();
    const std::vector<SuggestWord> before = plain.fuzzy_make_word("bagsi");
    ASSERT_GT(before.size(), 3U);
    // Something that is in the list but not already first.
    const ZString target = before[3].str;

    UserWords store;
    store.learn(target, 100);
    Ime learned = MakeIme();
    learned.set_user_words(&store);
    learned.set_now(100);
    const std::vector<SuggestWord> after = learned.fuzzy_make_word("bagsi");

    EXPECT_EQ(after.size(), before.size()) << "no word added or lost";
    EXPECT_EQ(after[0].str, target) << "the learned word leads";
    EXPECT_LT(PositionOf(after, target), PositionOf(before, target));

    // Every other word keeps its own score; only the order moved.
    for (const SuggestWord& w : before) {
        if (w.str == target) continue;
        const std::size_t at = PositionOf(after, w.str);
        ASSERT_LT(at, after.size());
        EXPECT_EQ(after[at].score, w.score);
    }
}

TEST_F(UserWordsTest, AWordOnlyTheUserHasIsOffered) {
    Ime plain = MakeIme();
    EXPECT_TRUE(plain.fuzzy_make_word("bicig").empty())
        << "the dictionary is not supposed to know this one";

    UserWords store;
    store.learn(ZString(kNotInDictionary), 50);
    Ime with_store = MakeIme();
    with_store.set_user_words(&store);
    with_store.set_now(50);

    const std::vector<SuggestWord> words = with_store.fuzzy_make_word("bicig");
    ASSERT_EQ(words.size(), 1U);
    EXPECT_EQ(words[0].str, ZString(kNotInDictionary));
    EXPECT_GT(words[0].score, 1.0F) << "above anything the dictionary can score";
}

TEST_F(UserWordsTest, AUserWordLeadsTheDictionarysOwn) {
    UserWords store;
    store.learn(ZString(kNotInDictionary), 10);
    Ime ime = MakeIme();
    ime.set_user_words(&store);
    ime.set_now(10);
    // "bici" matches both the user's word and dictionary words.
    const std::vector<SuggestWord> words = ime.fuzzy_make_word("bici");
    ASSERT_FALSE(words.empty());
    EXPECT_EQ(words[0].str, ZString(kNotInDictionary));
}

TEST_F(UserWordsTest, ForgettingPutsTheListBack) {
    Ime plain = MakeIme();
    const std::vector<SuggestWord> before = plain.fuzzy_make_word("bagsi");
    ASSERT_GT(before.size(), 3U);

    UserWords store;
    store.learn(before[3].str, 100);
    Ime ime = MakeIme();
    ime.set_user_words(&store);
    ime.set_now(100);
    ASSERT_NE(ime.fuzzy_make_word("bagsi")[0].str, before[0].str);

    store.forget(before[3].str);
    const std::vector<SuggestWord> after = ime.fuzzy_make_word("bagsi");
    ASSERT_EQ(after.size(), before.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        EXPECT_EQ(after[i].str, before[i].str) << i;
        EXPECT_EQ(after[i].score, before[i].score) << i;
    }
}

// A habit left alone long enough stops deciding the order.
TEST_F(UserWordsTest, AnOldHabitFades) {
    Ime plain = MakeIme();
    const std::vector<SuggestWord> before = plain.fuzzy_make_word("bagsi");
    ASSERT_GT(before.size(), 3U);
    const ZString target = before[3].str;

    UserWords store;
    store.learn(target, 0);
    Ime ime = MakeIme();
    ime.set_user_words(&store);

    ime.set_now(0);
    EXPECT_EQ(ime.fuzzy_make_word("bagsi")[0].str, target);
    ime.set_now(1000);  // many half-lives later
    EXPECT_EQ(ime.fuzzy_make_word("bagsi")[0].str, before[0].str);
}

TEST_F(UserWordsTest, WeightZeroTurnsTheHabitOffButKeepsTheWords) {
    UserWords::Options options;
    options.weight = 0;
    UserWords store(options);
    store.learn(ZString(kNotInDictionary), 10);

    Ime ime = MakeIme();
    ime.set_user_words(&store);
    ime.set_now(10);
    // The user's own word is still offered -- it is a word, not a ranking
    // tweak -- but nothing is lifted by habit.
    const std::vector<SuggestWord> words = ime.fuzzy_make_word("bicig");
    ASSERT_EQ(words.size(), 1U);
    EXPECT_EQ(words[0].score, 1.0F);
}

}  // namespace
}  // namespace zcode
