// SPDX-License-Identifier: GPL-2.0-or-later
//
// zcode-cpp: the dictionary half of the east-mod/ime input method, ported to
// C++20. Public API. Ported from
// com.zorigt.ime.input.method.service.InputMethodService and the
// com.zorigt.ime.approximate.matching package.
//
// What this is
// ------------
// zcode/zcode.h turns Latin keys into ZCode letter shapes. That is only half
// of an input method: the shaping engine will happily produce dozens of
// well-formed sequences that are not words. This half takes those sequences
// and looks them up in a dictionary of real words.
//
// The original keeps the dictionary in MySQL and loads it into
// Burkhard-Keller trees at startup, one per word length, holding every word
// with `frequency > 44` -- 38,743 of the table's 499,925 rows. Those 38,743
// words are compiled into this library (data/dictionary.tsv); there is no
// database and nothing to install.
//
// What the search actually computes
// ---------------------------------
// A tree holds words of one length K, the query has length L, and the search
// tolerance is K-L. Edit distance is at least the length difference, so a hit
// has distance exactly K-L, which can only be K-L pure insertions. So:
//
//     hit  <=>  the query is a subsequence of the word, and L <= K < 2L
//
// and the JaroWinkler filter that follows (`score < 1` is dropped) reduces to
// "the first code point must match, and Jaro must reach 0.7". Both were
// checked against the running original over its whole vocabulary. This is
// why the index here is not a BK tree even though the original's is: the
// results are identical and a BK tree is the wrong shape for a tolerance
// this large.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "zcode/zcode.h"

namespace zcode {

// SuggestWord. `str` is a dictionary word, not a spliced sequence.
struct SuggestWord {
    ZString str;
    int length = 0;
    std::uint64_t frequency = 0;
    // The edit distance to the query, which for a hit is always
    // length - query length.
    int levenshtein_d = 0;

    // The parts of the ranking, kept separately because the original's
    // /ime/fuzzy/score endpoint reports them.
    float jaro_winkler_score = 0;
    float frequency_score = 0;
    float levenshtein_score = 0;
    float score = 0;

    // SuggestWord.toString(), for that endpoint.
    std::string to_string() const;
};

// One word of the dictionary.
struct Word {
    ZString str;
    std::uint64_t frequency = 0;
};

// The word list and the index over it.
//
// A Dictionary is immutable and may be shared by any number of threads. A
// user's own words and habits are not kept here: a host passes them in (see
// UserWords) so that storage, privacy and synchronisation stay the host's,
// and so that this class stays a pure function of its inputs.
class Dictionary {
public:
    // The 38,743 words compiled into the library.
    static std::optional<Dictionary> create(std::string* error = nullptr);
    // An arbitrary word list, for tests and for extension packs.
    static Dictionary from_words(std::vector<Word> words);

    Dictionary(Dictionary&&) noexcept;
    Dictionary& operator=(Dictionary&&) noexcept;
    ~Dictionary();

    std::size_t size() const;

    // InputMethodService.matchInBkMaps(): the words matching one spliced
    // sequence, scored and filtered, in the original's own order.
    std::vector<SuggestWord> match(std::u32string_view spliced) const;

    // The same, with a reference implementation that really is a BK tree per
    // length, as the original has. Only tests use it: it exists so the fast
    // index can be checked against the structure it replaced, and it is
    // thousands of times slower.
    std::vector<SuggestWord> match_bk_reference(std::u32string_view spliced) const;

private:
    struct Impl;
    explicit Dictionary(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// The service layer: the three methods behind the original's HTTP endpoints.
//
// The Engine and Dictionary must outlive the Ime.
class Ime {
public:
    Ime(const Engine& engine, const Dictionary& dictionary);
    Ime(Ime&&) noexcept;
    Ime& operator=(Ime&&) noexcept;
    ~Ime();

    // severeMakeWord(): decompose + LetterSplicer.severe, no dictionary.
    // Identical to Engine::shape(latin, Mode::severe).
    std::vector<ZString> severe_make_word(std::string_view latin) const;

    // fuzzyMakeWord(): splice loosely, look each sequence up, merge by word,
    // keep the better score, apply the last key's filter, sort by score.
    std::vector<SuggestWord> fuzzy_make_word(std::string_view latin) const;

    // makeWord(): severe and fuzzy merged, the original's way round.
    std::vector<ZString> make_word(std::string_view latin) const;

    // The original caches spliced sequences that matched nothing, in MySQL,
    // so later requests skip them. It cannot change a result -- a sequence is
    // only cached after it came back empty -- so it is in memory here and can
    // be turned off. It is cleared whenever the dictionary set changes.
    void set_negative_cache_enabled(bool enabled);
    void clear_negative_cache();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace zcode
