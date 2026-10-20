// SPDX-License-Identifier: GPL-2.0-or-later

#include "protocol.h"

#include <charconv>
#include <cstdint>
#include <map>
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
    explicit State(const Engine& e) : engine(e) {
        sessions.emplace(0, Session(e));  // Session 0 always exists.
    }
    const Engine& engine;
    std::map<std::uint64_t, Session> sessions;
    std::uint64_t next_id = 1;

    // Null when `id` names no live session.
    Session* find(std::uint64_t id) {
        const auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : &it->second;
    }
};

Protocol::Protocol(const Engine& engine)
    : state_(std::make_unique<State>(engine)) {}
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
        command == "CLEAR" || command == "KEYS" || command == "CAND") {
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
            // One key per request: a key may be several characters ("ng"), so
            // it is taken verbatim rather than split further.
            if (tail.empty()) return Err("PUSH needs a key");
            session->push(tail);
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

        // CAND
        std::string_view args;
        const std::string_view mode_text = NextToken(tail, &args);
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

    return Err("unknown command");
}

}  // namespace zcode::service
