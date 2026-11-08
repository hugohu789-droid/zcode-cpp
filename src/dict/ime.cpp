// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.input.method.service.InputMethodService
// (fuzzyMakeWord, severeMakeWord, makeWord) and the controller's view of
// them.

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common_strings.h"
#include "zcode/dictionary.h"
#include "zcode/zcode.h"

namespace zcode {
namespace {

struct ZStringHash {
    std::size_t operator()(const ZString& s) const noexcept {
        return std::hash<std::u32string_view>{}(s);
    }
};

}  // namespace

struct Ime::Impl {
    const Engine* engine;
    const Dictionary* dictionary;
    const Dictionary* extension = nullptr;

    const UserWords* user_words = nullptr;
    std::uint64_t now = 0;

    bool negative_cache_enabled = true;
    mutable std::mutex mutex;
    // One set per dictionary: a sequence the main dictionary does not know
    // may well be in the extension pack.
    mutable std::unordered_set<ZString, ZStringHash> negative;
    mutable std::unordered_set<ZString, ZStringHash> negative_extension;

    std::unordered_set<ZString, ZStringHash>& cache_for(const Dictionary* which) const {
        return which == extension ? negative_extension : negative;
    }
};

Ime::Ime(const Engine& engine, const Dictionary& dictionary) : impl_(std::make_unique<Impl>()) {
    impl_->engine = &engine;
    impl_->dictionary = &dictionary;
}
Ime::Ime(Ime&&) noexcept = default;
Ime& Ime::operator=(Ime&&) noexcept = default;
Ime::~Ime() = default;

void Ime::set_negative_cache_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->negative_cache_enabled = enabled;
    if (!enabled) {
        impl_->negative.clear();
        impl_->negative_extension.clear();
    }
}

void Ime::clear_negative_cache() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->negative.clear();
    impl_->negative_extension.clear();
}

void Ime::set_extension(const Dictionary* extension) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->extension = extension;
    // The cache records "this dictionary knows nothing about that
    // sequence". Changing which dictionaries there are makes that stale.
    impl_->negative_extension.clear();
}

const Dictionary* Ime::extension() const { return impl_->extension; }

void Ime::set_user_words(const UserWords* user_words) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->user_words = user_words;
}

const UserWords* Ime::user_words() const { return impl_->user_words; }

void Ime::set_now(std::uint64_t tick) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->now = tick;
}

std::uint64_t Ime::now() const { return impl_->now; }

std::vector<ZString> Ime::severe_make_word(std::string_view latin) const {
    // severeMakeWord(): the blank guard, decompose, splice each
    // decomposition, concatenate -- which is exactly Engine::shape().
    return impl_->engine->shape(latin, Mode::severe);
}

std::vector<SuggestWord> Ime::fuzzy_make_word(std::string_view latin) const {
    std::vector<SuggestWord> words = fuzzy_make_word_in(latin, impl_->dictionary);
    if (words.empty() && impl_->extension != nullptr) {
        // Nothing at all from the main dictionary -- which is also what the
        // original would have answered -- so there is nothing to contradict.
        words = fuzzy_make_word_in(latin, impl_->extension);
    }
    return words;
}

std::vector<SuggestWord> Ime::fuzzy_make_word_in(std::string_view latin,
                                                 const Dictionary* dictionary) const {
    if (is_blank(latin)) {
        return {};
    }
    const std::vector<std::vector<std::string>> decompositions = impl_->engine->decompose(latin);
    if (decompositions.empty()) {
        return {};
    }

    // The original collects into a HashMap keyed by the word, so a word
    // found through several spliced sequences appears once, with the best
    // score. Insertion order is kept here so that the tie-break below has
    // something stable to fall back on; the original's order at that point
    // is its HashMap's bucket order, which is not something worth
    // reproducing (see README).
    std::unordered_map<ZString, std::size_t, ZStringHash> index;
    std::vector<SuggestWord> merged;

    // Reused across decompositions; cleared for each, because the tail
    // filter below belongs to the decomposition and not to the sequence.
    std::unordered_set<ZString, ZStringHash> already_looked_up;

    // The user's own words that matched, kept aside until the dictionary's
    // own merge is done: a word in both is the dictionary's word with a
    // bonus, not a second entry.
    const UserWords* user = impl_->user_words;
    const std::uint64_t now = impl_->now;
    std::unordered_map<ZString, SuggestWord, ZStringHash> user_hits;

    for (const std::vector<std::string>& keys : decompositions) {
        const std::vector<ZString> spliced = impl_->engine->splice(keys, Mode::fuzzy);
        if (spliced.empty()) {
            continue;
        }
        // KeyFilterFactory.get(lastKey + "-tail"): applied to the words
        // found from this decomposition, not to the spliced sequences.
        const std::string& last_key = keys.back();

        // The splicer enumerates a product of per-key shape choices, and
        // different choices can spell the same code points: `gggggg` yields
        // 16,875 sequences of which 5,376 are distinct, `iiiiiiiiii` 2,048 of
        // which 56 are. Looking one up twice cannot change the answer -- the
        // merge below keeps the best-scoring entry per word, and a repeat
        // produces exactly what the first produced -- so the repeats are
        // pure cost. Skipping them keeps the order of the survivors, which
        // is what the merge's "the later of two equal scores wins" depends
        // on.
        already_looked_up.clear();
        already_looked_up.reserve(spliced.size());

        for (const ZString& sequence : spliced) {
            if (!already_looked_up.insert(sequence).second) {
                continue;
            }
            if (user != nullptr) {
                for (const ZString& word : user->match(sequence)) {
                    if (!impl_->engine->key_filter_accepts(last_key, word)) {
                        continue;
                    }
                    const int distance =
                        static_cast<int>(word.size()) - static_cast<int>(sequence.size());
                    SuggestWord& slot = user_hits[word];
                    if (!slot.str.empty() && slot.levenshtein_d <= distance) {
                        continue;  // a closer sighting of the same word wins
                    }
                    slot.str = word;
                    slot.length = static_cast<int>(word.size());
                    slot.levenshtein_d = distance;
                }
            }
            if (impl_->negative_cache_enabled) {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                if (impl_->cache_for(dictionary).count(sequence) != 0) {
                    continue;
                }
            }
            std::vector<SuggestWord> part = dictionary->match(sequence);
            if (part.empty()) {
                if (impl_->negative_cache_enabled) {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    impl_->cache_for(dictionary).insert(sequence);
                }
                continue;
            }
            for (SuggestWord& sw : part) {
                const auto it = index.find(sw.str);
                if (it != index.end() && merged[it->second].score > sw.score) {
                    continue;
                }
                if (!impl_->engine->key_filter_accepts(last_key, sw.str)) {
                    continue;
                }
                if (it != index.end()) {
                    merged[it->second] = std::move(sw);
                } else {
                    index.emplace(sw.str, merged.size());
                    merged.push_back(std::move(sw));
                }
            }
        }
    }

    // The user's words and habits, which the original has neither of. Both
    // are no-ops when the host supplied no store, which is what keeps every
    // answer above identical to the original's.
    if (user != nullptr) {
        // A word the user has chosen before keeps its place in the
        // dictionary's ranking and is lifted by how established the habit
        // is. The dictionary's score is normalised to at most 1, so with the
        // default weight a settled habit outranks anything the dictionary
        // alone can offer, and one old use only nudges.
        for (SuggestWord& w : merged) {
            w.score += user->bonus(w.str, now);
        }
        // A word only the user has is offered above the dictionary's own: it
        // matched what was typed by the same rule, and the user put it
        // there. Its metrics are not the dictionary's -- there is no
        // frequency to compare and no maximum to normalise against -- so the
        // score is 1 (what a perfect dictionary match scores) plus the
        // bonus, and the metric fields stay zero rather than carrying a
        // number that would look like the dictionary's and mean something
        // else.
        for (auto& [word, hit] : user_hits) {
            if (index.find(word) != index.end()) {
                continue;  // already in the list, already given its bonus
            }
            hit.score = 1.0F + user->bonus(word, now);
            merged.push_back(std::move(hit));
        }
    }

    // The original sorts by score descending with a stable sort over its
    // HashMap's iteration order, so candidates that share a score come out
    // in an order that depends on Java's hashing. Ties are broken here by
    // frequency and then by code point: a rule that can be stated, and that
    // does not change between runs, platforms or library versions.
    std::sort(merged.begin(), merged.end(), [](const SuggestWord& a, const SuggestWord& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.frequency != b.frequency) {
            return a.frequency > b.frequency;
        }
        return a.str < b.str;
    });
    return merged;
}

std::vector<ZString> Ime::make_word(std::string_view latin) const {
    // makeWord(). The original's merge is unusual enough to be worth
    // spelling out: it keeps the severe results that the fuzzy list also
    // found, in severe order, removing each from the fuzzy list as it goes,
    // and appends what is left of the fuzzy list. If nothing was in both, it
    // appends the whole fuzzy list and then the whole severe list.
    if (is_blank(latin)) {
        return {};
    }
    const std::vector<ZString> severe = severe_make_word(latin);
    std::vector<ZString> fuzzy;
    for (const SuggestWord& sw : fuzzy_make_word(latin)) {
        fuzzy.push_back(sw.str);
    }

    std::vector<ZString> words;
    if (fuzzy.empty()) {
        words = severe;
        return words;
    }
    if (severe.empty()) {
        return fuzzy;
    }
    for (const ZString& s : severe) {
        const auto it = std::find(fuzzy.begin(), fuzzy.end(), s);
        if (it != fuzzy.end()) {
            words.push_back(s);
            fuzzy.erase(it);
        }
    }
    if (words.empty()) {
        words = fuzzy;
        words.insert(words.end(), severe.begin(), severe.end());
        return words;
    }
    words.insert(words.end(), fuzzy.begin(), fuzzy.end());
    return words;
}

}  // namespace zcode
