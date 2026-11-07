// SPDX-License-Identifier: GPL-2.0-or-later

#include "protocol.h"

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace zcode::service {
namespace {

constexpr char kTab = '\t';

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

// Splits off the first space-separated token, leaving the rest (trimmed) in
// `rest`. Returns an empty view when `text` is empty.
std::string_view NextToken(std::string_view text, std::string_view* rest) {
    const std::size_t space = text.find(' ');
    if (space == std::string_view::npos) {
        *rest = std::string_view();
        return text;
    }
    *rest = Trim(text.substr(space + 1));
    return text.substr(0, space);
}

bool ParseMode(std::string_view text, Mode* mode) {
    if (text == "severe") {
        *mode = Mode::severe;
        return true;
    }
    if (text == "fuzzy") {
        *mode = Mode::fuzzy;
        return true;
    }
    return false;
}

// What a session query asks for. `severe` and `fuzzy` are the splicer's own
// modes and answer with letter shapes; the other three go through the
// dictionary and answer with words, so they need one loaded.
enum class QueryMode { severe, fuzzy, word, score, make, full };

bool ParseQueryMode(std::string_view text, QueryMode* out) {
    if (text == "severe") {
        *out = QueryMode::severe;
        return true;
    }
    if (text == "fuzzy") {
        *out = QueryMode::fuzzy;
        return true;
    }
    if (text == "word") {
        *out = QueryMode::word;
        return true;
    }
    if (text == "score") {
        *out = QueryMode::score;
        return true;
    }
    if (text == "make") {
        *out = QueryMode::make;
        return true;
    }
    if (text == "full") {
        *out = QueryMode::full;
        return true;
    }
    return false;
}

bool NeedsDictionary(QueryMode mode) {
    return mode == QueryMode::word || mode == QueryMode::score ||
           mode == QueryMode::make || mode == QueryMode::full;
}

bool ParseUnsigned(std::string_view text, std::size_t* out) {
    if (text.empty()) return false;
    std::size_t value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) return false;
    *out = value;
    return true;
}

// A field carrying a control character would break the line framing, so a
// request carrying one is rejected rather than quietly stripped: stripping
// would turn an injection attempt into a valid-looking request.
bool HasControlChar(std::string_view text) {
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x20) return true;
    }
    return false;
}

// "OK" plus one field per item.
std::string Ok(const std::vector<std::string>& items) {
    std::string response = "OK";
    for (const std::string& item : items) {
        response += kTab;
        response += item;
    }
    return response;
}

std::string Err(std::string_view message) {
    std::string response = "ERR";
    response += kTab;
    response.append(message);
    return response;
}

std::vector<std::string> ToUtf8(const std::vector<ZString>& candidates) {
    std::vector<std::string> items;
    items.reserve(candidates.size());
    for (const ZString& candidate : candidates) {
        items.push_back(to_utf8(candidate));
    }
    return items;
}

// The words a dictionary mode answers with, already formatted. `score`
// carries every metric behind the ranking, which is what a host needs to rank
// these against candidates from its own sources instead of trusting the
// order; the other two carry the word alone.
// `no_words` reports whether the dictionary itself came up empty, which is
// not the same as an empty answer: "full" falls back on the splicer's shapes
// and "make" merges them in, so both can answer while the dictionary had
// nothing. A host loading an optional extension pack needs the former.
// "make" cannot tell, because the original's merge hides it, so it reports
// only the answer being empty.
std::vector<std::string> DictionaryItems(const Ime& ime, QueryMode mode,
                                         std::string_view latin, bool* no_words) {
    if (mode == QueryMode::make) {
        std::vector<std::string> made = ToUtf8(ime.make_word(latin));
        *no_words = made.empty();
        return made;
    }
    const std::vector<SuggestWord> words = ime.fuzzy_make_word(latin);
    *no_words = words.empty();
    std::vector<std::string> items;
    items.reserve(words.size());
    for (const SuggestWord& word : words) {
        const bool scored = mode == QueryMode::score || mode == QueryMode::full;
        items.push_back(scored ? word.to_string() : to_utf8(word.str));
    }
    if (mode != QueryMode::full) return items;

    // "full" is "score" that is never empty. The dictionary holds 38,743
    // words and a user types words that are not among them, so a mode that
    // answers with dictionary words alone leaves the candidate window blank
    // and nothing to commit. The splicer's own severe shapes -- the exact
    // reading of what was typed -- are appended for that case, which is also
    // the order makeWord() uses when the two lists do not overlap.
    //
    // They are written in the same shape as a scored item, with the metrics
    // zeroed, so the host has one scale to rank by and the shapes sort below
    // every real word. A shape that *is* a dictionary word is already in the
    // list above, with its real score.
    static constexpr char kNoMetrics[] =
        ",frequency:0,frequencyScore:0.0,levenshteinD:0,levenshteinScore:0.0,"
        "jaroWinklerScore:0.0,score:0.0";
    std::unordered_set<std::string> seen;
    seen.reserve(words.size() * 2);
    for (const SuggestWord& word : words) seen.insert(to_utf8(word.str));
    for (const ZString& shape : ime.severe_make_word(latin)) {
        std::string text = to_utf8(shape);
        if (!seen.insert(text).second) continue;
        text += kNoMetrics;
        items.push_back(std::move(text));
    }
    return items;
}

// Applies the same Options the shaping path applies, so a host gets the same
// cap and the same dedup whichever mode it asks for. The shaping path caps
// before building the list; a dictionary mode cannot, because the ranking
// needs every match before it knows which ones are the best, so here the cap
// is a truncation of the ranked list.
std::vector<std::string> Limit(std::vector<std::string> items, const Options& options) {
    if (options.deduplicate) {
        std::unordered_set<std::string> seen;
        std::vector<std::string> unique;
        unique.reserve(items.size());
        for (std::string& item : items) {
            if (seen.insert(item).second) unique.push_back(std::move(item));
        }
        items = std::move(unique);
    }
    if (options.max_candidates != 0 && items.size() > options.max_candidates) {
        items.resize(options.max_candidates);
    }
    return items;
}

// The Latin string the host typed, recovered from the session's keys. The
// dictionary takes the input, not the splicer's cached layers: it decomposes
// it again itself, which is why a host may push keys in any grouping.
std::string Joined(const std::vector<std::string>& keys) {
    std::string latin;
    for (const std::string& key : keys) latin += key;
    return latin;
}

}  // namespace

struct Protocol::State {
    State(const Engine& e, const Dictionary* d) : engine(e), dictionary(d) {
        sessions.emplace(0, Session(e));  // Session 0 always exists.
        if (d != nullptr) ime.emplace(e, *d);
    }
    const Engine& engine;
    const Dictionary* dictionary;
    std::optional<Ime> ime;
    std::map<std::uint64_t, Session> sessions;
    std::uint64_t next_id = 1;

    // Null when `id` names no live session.
    Session* find(std::uint64_t id) {
        const auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : &it->second;
    }
};

Protocol::Protocol(const Engine& engine, const Dictionary* dictionary)
    : state_(std::make_unique<State>(engine, dictionary)) {}
Protocol::Protocol(Protocol&&) noexcept = default;
Protocol& Protocol::operator=(Protocol&&) noexcept = default;
Protocol::~Protocol() = default;

bool Protocol::has_dictionary() const { return state_->ime.has_value(); }

Ime& Protocol::ime() { return *state_->ime; }

std::string Protocol::handle(std::string_view line, bool* quit, bool* dictionary_miss) {
    if (quit != nullptr) *quit = false;
    if (dictionary_miss != nullptr) *dictionary_miss = false;

    std::string_view rest;
    const std::string_view command = NextToken(Trim(line), &rest);
    if (command.empty()) return Err("empty request");

    if (command == "QUIT") {
        if (quit != nullptr) *quit = true;
        return "OK";
    }

    if (command == "HELLO") {
        // Name and protocol version, so a host can refuse an engine it does
        // not understand instead of failing later on a real request.
        return Ok({"paiza-engine", "1"});
    }

    if (command == "OPEN") {
        const std::uint64_t id = state_->next_id++;
        state_->sessions.emplace(id, Session(state_->engine));
        return Ok({std::to_string(id)});
    }

    if (command == "SHAPE") {
        std::string_view tail;
        const std::string_view mode_text = NextToken(rest, &tail);
        Mode mode{};
        if (!ParseMode(mode_text, &mode)) {
            return Err("mode must be severe or fuzzy");
        }
        if (tail.empty()) return Err("SHAPE needs a latin string");
        return Ok(ToUtf8(state_->engine.shape(tail, mode)));
    }

    // Everything below is session-scoped and takes an id first.
    if (command == "CLOSE" || command == "PUSH" || command == "POP" ||
        command == "CLEAR" || command == "KEYS" || command == "CAND" ||
        command == "TYPE" || command == "UNTYPE") {
        std::string_view tail;
        const std::string_view id_text = NextToken(rest, &tail);
        std::size_t id = 0;
        if (!ParseUnsigned(id_text, &id)) {
            return Err("expected a session id");
        }
        Session* session = state_->find(id);
        if (session == nullptr) return Err("unknown session");

        if (command == "CLOSE") {
            // Session 0 is the implicit one; closing it would leave a
            // single-context client with nothing to talk to.
            if (id == 0) return Err("cannot close session 0");
            state_->sessions.erase(id);
            return "OK";
        }

        if (command == "PUSH") {
            std::string_view extra;
            const std::string_view key = NextToken(tail, &extra);
            if (key.empty()) return Err("PUSH needs a key");
            if (!extra.empty()) return Err("PUSH takes one key");
            if (HasControlChar(key)) return Err("key has a control character");
            session->push(key);
            return "OK";
        }

        if (command == "POP") {
            session->pop();
            return "OK";
        }

        if (command == "CLEAR") {
            session->clear();
            return "OK";
        }

        if (command == "KEYS") {
            return Ok(session->keys());
        }

        // TYPE pushes a key first, UNTYPE pops one; both then fall through
        // to the CAND path, so a keystroke costs one round trip rather than
        // a mutation followed by a query.
        // The mutation is deferred until every field has been validated, so
        // a rejected TYPE or UNTYPE leaves the session untouched like every
        // other rejected request.
        std::string_view args_source = tail;
        std::string_view pending_key;
        if (command == "TYPE") {
            std::string_view after_key;
            pending_key = NextToken(tail, &after_key);
            if (pending_key.empty()) return Err("TYPE needs a key");
            if (HasControlChar(pending_key)) {
                return Err("key has a control character");
            }
            args_source = after_key;
        }

        std::string_view args;
        const std::string_view mode_text = NextToken(args_source, &args);
        QueryMode query{};
        if (!ParseQueryMode(mode_text, &query)) {
            return Err("mode must be severe, fuzzy, word, score or make");
        }
        // Checked here, before the key is pushed: a dictionary mode asked of
        // a build without one is a rejected request, and a rejected request
        // leaves the session untouched like every other.
        if (NeedsDictionary(query) && !state_->ime.has_value()) {
            return Err("this engine was built without a dictionary");
        }

        // Optional max and dedup, in that order.
        Options options;
        if (!args.empty()) {
            std::string_view flags;
            const std::string_view max_text = NextToken(args, &flags);
            if (!ParseUnsigned(max_text, &options.max_candidates)) {
                return Err("max must be a non-negative integer");
            }
            if (!flags.empty()) {
                std::size_t dedup = 0;
                if (!ParseUnsigned(flags, &dedup) || dedup > 1) {
                    return Err("dedup must be 0 or 1");
                }
                options.deduplicate = dedup == 1;
            }
        }
        if (command == "TYPE") {
            session->push(pending_key);
        } else if (command == "UNTYPE") {
            session->pop();
        }
        if (!NeedsDictionary(query)) {
            const Mode mode = query == QueryMode::severe ? Mode::severe : Mode::fuzzy;
            return Ok(ToUtf8(session->candidates(mode, options)));
        }
        const std::string latin = Joined(session->keys());
        if (latin.empty()) return Ok({});
        bool no_words = false;
        std::vector<std::string> items =
            Limit(DictionaryItems(*state_->ime, query, latin, &no_words), options);
        if (no_words && dictionary_miss != nullptr) *dictionary_miss = true;
        return Ok(items);
    }

    if (command == "DECOMPOSE") {
        if (rest.empty()) return Err("DECOMPOSE needs a latin string");
        std::vector<std::string> items;
        for (const std::vector<std::string>& split :
             state_->engine.decompose(rest)) {
            std::string joined;
            for (const std::string& key : split) {
                if (!joined.empty()) joined += ' ';
                joined += key;
            }
            items.push_back(std::move(joined));
        }
        return Ok(items);
    }

    if (command == "DICT") {
        if (state_->dictionary == nullptr) return Ok({"none"});
        std::vector<std::string> items{"words", std::to_string(state_->dictionary->size())};
        const Dictionary* extension = state_->ime->extension();
        items.emplace_back("extension");
        items.push_back(extension == nullptr ? "none" : std::to_string(extension->size()));
        return Ok(items);
    }

    if (command == "LOOKUP") {
        if (!state_->ime.has_value()) {
            return Err("this engine was built without a dictionary");
        }
        std::string_view tail;
        const std::string_view what = NextToken(rest, &tail);
        if (tail.empty()) return Err("LOOKUP needs a latin string");
        if (what == "severe") {
            // No dictionary is consulted, so an empty answer is not a miss.
            return Ok(ToUtf8(state_->ime->severe_make_word(tail)));
        }
        QueryMode mode{};
        if (what == "fuzzy") {
            mode = QueryMode::word;
        } else if (what == "make" || what == "score") {
            mode = what == "make" ? QueryMode::make : QueryMode::score;
        } else {
            return Err("LOOKUP takes make, fuzzy, score or severe");
        }
        bool no_words = false;
        std::vector<std::string> items =
            DictionaryItems(*state_->ime, mode, tail, &no_words);
        if (no_words && dictionary_miss != nullptr) *dictionary_miss = true;
        return Ok(items);
    }

    return Err("unknown command");
}

}  // namespace zcode::service
