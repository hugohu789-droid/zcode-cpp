// SPDX-License-Identifier: GPL-2.0-or-later

#include "protocol.h"

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
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

std::string Protocol::handle(std::string_view line, bool* quit) {
    if (quit != nullptr) *quit = false;

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
        Mode mode{};
        if (!ParseMode(mode_text, &mode)) {
            return Err("mode must be severe or fuzzy");
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
        return Ok(ToUtf8(session->candidates(mode, options)));
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
        return Ok({"words", std::to_string(state_->dictionary->size())});
    }

    if (command == "LOOKUP") {
        if (!state_->ime.has_value()) {
            return Err("this engine was built without a dictionary");
        }
        std::string_view tail;
        const std::string_view what = NextToken(rest, &tail);
        if (tail.empty()) return Err("LOOKUP needs a latin string");
        if (what == "severe") {
            return Ok(ToUtf8(state_->ime->severe_make_word(tail)));
        }
        if (what == "make") {
            return Ok(ToUtf8(state_->ime->make_word(tail)));
        }
        if (what == "fuzzy" || what == "score") {
            const std::vector<SuggestWord> words = state_->ime->fuzzy_make_word(tail);
            std::vector<std::string> items;
            items.reserve(words.size());
            for (const SuggestWord& word : words) {
                items.push_back(what == "score" ? word.to_string() : to_utf8(word.str));
            }
            return Ok(items);
        }
        return Err("LOOKUP takes make, fuzzy, score or severe");
    }

    return Err("unknown command");
}

}  // namespace zcode::service
