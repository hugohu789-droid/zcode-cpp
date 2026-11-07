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

// A user's own words, and how recently each was used.
//
// The division of labour is deliberate: **this engine ranks, the host
// stores.** The host owns the file, the database, the per-profile scoping,
// the privacy policy and any synchronisation; it replays what it has into a
// UserWords at startup, tells this class when the user commits a word, and
// reads the entries back out to persist them. Nothing here touches a disk or
// a clock, which is also what makes it testable.
//
// `tick` is the host's own clock, and the only thing this class assumes
// about it is that it does not go backwards. Days since an epoch is the
// natural unit -- then `half_life` is in days -- but a counter of commits
// works too, and so does a Unix timestamp if `half_life` is set to match.
//
// The habit model is Chewing's: a word's weight is how often it was chosen,
// damped so that the twentieth use is worth far less than the first, decayed
// by how long ago that was. Two words the user picks equally often are then
// ordered by which was used more recently, and a word picked once a year ago
// fades back to where the dictionary had it.
//
//     strength = count / (count + saturation)
//     decay    = 0.5 ^ ((now - tick) / half_life)
//     bonus    = weight * strength * decay
//
// The bonus is added to the score the dictionary gave, which is normalised
// to at most 1, so with the default weight a well-established habit outranks
// anything the dictionary alone can offer, and a single old use only nudges.
//
// **An empty UserWords changes nothing.** Every bonus is zero, no words are
// added, and the output is the original's to the last float -- which is what
// the comparison against the Java service depends on.
class UserWords {
public:
    struct Entry {
        ZString word;
        std::uint32_t count = 0;   // times the user committed it
        std::uint64_t tick = 0;    // the host's clock, when they last did
    };

    struct Options {
        // How much a fully established habit may add to a score of at most 1.
        float weight = 1.0F;
        // Ticks after which an unused word's bonus halves.
        float half_life = 30.0F;
        // Damping on the count: at `saturation` uses the strength is 0.5.
        float saturation = 2.0F;
    };

    UserWords();
    explicit UserWords(Options options);
    UserWords(UserWords&&) noexcept;
    UserWords& operator=(UserWords&&) noexcept;
    ~UserWords();

    const Options& options() const;
    void set_options(Options options);

    // Restores one entry, as the host read it back from its own store. A
    // word already present keeps the higher count and the later tick, so
    // replaying a store twice is harmless.
    void restore(const Entry& entry);
    // The user committed `word` at `tick`: one more use, as of then.
    void learn(const ZString& word, std::uint64_t tick);
    void forget(const ZString& word);
    void clear();

    std::size_t size() const;
    // Every entry, for the host to persist. Ordered by word, so a host
    // diffing two dumps sees only what changed.
    std::vector<Entry> entries() const;

    // The habit bonus for one word at `now`; 0 for a word never committed.
    float bonus(const ZString& word, std::uint64_t now) const;

    // The user's own words that match this spliced sequence, by the same
    // rule the dictionary uses: the query is a subsequence of the word, the
    // word's length is in [L, 2L), and the first code point matches.
    std::vector<ZString> match(std::u32string_view spliced) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The word list and the index over it.
//
// A Dictionary is immutable and may be shared by any number of threads. A
// user's own words and habits are not kept here: a host passes them in (see
// UserWords) so that storage, privacy and synchronisation stay the host's,
// and so that this class stays a pure function of its inputs.
class Dictionary {
public:
    // Which index to build over the words. Both find exactly the same
    // words with the same metrics; a test asserts it.
    enum class Index {
        // A trie: microseconds per lookup, about 900 bytes per word.
        // Right for the dictionary behind every keystroke.
        fast,
        // Buckets scanned by (length, first code point): milliseconds per
        // lookup, about 55 bytes per word. Right for a large pack that is
        // only consulted when the main dictionary has nothing to say --
        // 461,182 words cost 100 MB as a trie and 25 MB this way.
        compact,
    };

    // The 38,743 words compiled into the library.
    static std::optional<Dictionary> create(std::string* error = nullptr);
    // A word list in the format of data/dictionary.tsv: '#' comments, then
    // `word<TAB>frequency` lines. Used for the optional extension pack
    // (tools/extract_dictionary.py --extension) and for a host's own list.
    static std::optional<Dictionary> from_file(const std::string& path,
                                               Index index = Index::compact,
                                               std::string* error = nullptr);
    static std::optional<Dictionary> from_tsv(std::string_view tsv,
                                              Index index = Index::fast,
                                              std::string* error = nullptr);
    // An arbitrary word list, for tests and for extension packs.
    static Dictionary from_words(std::vector<Word> words, Index index = Index::fast);

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

    // How many words the index finds for one sequence, with no scoring and
    // no filtering. A diagnostic: it separates the cost of the search from
    // the cost of scoring what the search found, which for this workload are
    // two different problems with two different answers (bench/dict_profile).
    std::size_t index_hits(std::u32string_view spliced) const;

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

    // An optional second dictionary, consulted only when the first one
    // answers with nothing at all.
    //
    // It is a fallback and not a second source because of what that buys:
    // where the main dictionary has anything to say, the answer is the
    // original's, unchanged; where it has nothing, so has the original, so
    // filling in cannot contradict it. The one place it is visible is
    // make_word(), which merges severe results with fuzzy ones: an input
    // whose fuzzy half was empty but whose severe half was not now gains
    // words the original does not return. Turning the extension off restores
    // the original's answer everywhere.
    //
    // Measured on the prefixes of real words: 27.5% get nothing from the
    // main dictionary, and the 461,182-word pack rescues a third of those,
    // with 5.4 candidates on average. The pack must outlive the Ime.
    void set_extension(const Dictionary* extension);
    const Dictionary* extension() const;

    // The original caches spliced sequences that matched nothing, in MySQL,
    // so later requests skip them. It cannot change a result -- a sequence is
    // only cached after it came back empty -- so it is in memory here and can
    // be turned off. It is cleared whenever the dictionary set changes.
    void set_negative_cache_enabled(bool enabled);
    void clear_negative_cache();

    // The user's own words and habits (see UserWords), and the host's clock
    // reading to decay them against. Both are the host's: it owns the
    // storage, this owns the ranking.
    //
    // A word the user has committed gets its habit bonus added to the score
    // the dictionary gave it. A word only the user has is offered too, above
    // the dictionary's own, because it matched what was typed and the user
    // put it there. Null, or empty, leaves every answer exactly as it was.
    // The store must outlive the Ime.
    void set_user_words(const UserWords* user_words);
    const UserWords* user_words() const;
    void set_now(std::uint64_t tick);
    std::uint64_t now() const;

private:
    std::vector<SuggestWord> fuzzy_make_word_in(std::string_view latin,
                                                const Dictionary* dictionary) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace zcode
