// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.input.method.service.InputMethodService
// (fuzzyMakeWord, severeMakeWord, makeWord) and the controller's view of
// them.

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "strings.h"
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

    bool negative_cache_enabled = true;
    mutable std::mutex mutex;
    mutable std::unordered_set<ZString, ZStringHash> negative;
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
    }
}

void Ime::clear_negative_cache() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->negative.clear();
}

std::vector<ZString> Ime::severe_make_word(std::string_view latin) const {
    // severeMakeWord(): the blank guard, decompose, splice each
    // decomposition, concatenate -- which is exactly Engine::shape().
    return impl_->engine->shape(latin, Mode::severe);
}

std::vector<SuggestWord> Ime::fuzzy_make_word(std::string_view latin) const {
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

    for (const std::vector<std::string>& keys : decompositions) {
        const std::vector<ZString> spliced = impl_->engine->splice(keys, Mode::fuzzy);
        if (spliced.empty()) {
            continue;
        }
        // KeyFilterFactory.get(lastKey + "-tail"): applied to the words
        // found from this decomposition, not to the spliced sequences.
        const std::string& last_key = keys.back();

        for (const ZString& sequence : spliced) {
            if (impl_->negative_cache_enabled) {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                if (impl_->negative.count(sequence) != 0) {
                    continue;
                }
            }
            std::vector<SuggestWord> part = impl_->dictionary->match(sequence);
            if (part.empty()) {
                if (impl_->negative_cache_enabled) {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    impl_->negative.insert(sequence);
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
