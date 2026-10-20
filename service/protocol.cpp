// SPDX-License-Identifier: GPL-2.0-or-later

#include "protocol.h"

#include <charconv>
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
    explicit State(const Engine& e) : engine(e), session(e) {}
    const Engine& engine;
    Session session;
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

    if (command == "PUSH") {
        // One key per request: a key may be several characters ("ng"), so it
        // is taken verbatim rather than split further.
        if (rest.empty()) return Err("PUSH needs a key");
        state_->session.push(rest);
        return "OK";
    }

    if (command == "POP") {
        state_->session.pop();
        return "OK";
    }

    if (command == "CLEAR") {
        state_->session.clear();
        return "OK";
    }

    if (command == "KEYS") {
        return Ok(state_->session.keys());
    }

    if (command == "CAND" || command == "SHAPE") {
        std::string_view tail;
        const std::string_view mode_text = NextToken(rest, &tail);
        Mode mode{};
        if (!ParseMode(mode_text, &mode)) {
            return Err("mode must be severe or fuzzy");
        }

        if (command == "SHAPE") {
            if (tail.empty()) return Err("SHAPE needs a latin string");
            return Ok(ToUtf8(state_->engine.shape(tail, mode)));
        }

        // CAND: optional max and dedup, in that order.
        Options options;
        if (!tail.empty()) {
            std::string_view flags;
            const std::string_view max_text = NextToken(tail, &flags);
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
        return Ok(ToUtf8(state_->session.candidates(mode, options)));
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
