// SPDX-License-Identifier: GPL-2.0-or-later
//
// Line protocol for the stdio service. Kept separate from main() so it can be
// unit-tested without spawning a process.
//
// One request per line, one response per line, UTF-8, LF-terminated (a
// trailing CR is tolerated so Windows clients can use text mode). Fields are
// TAB-separated. ZCode output can never contain TAB or LF: its code points are
// U+1865..U+18AA, so TAB is unambiguous as a separator.
//
// Sessions
//   A host with several input contexts (one per window, or per input client)
//   needs one shaping session each, so session commands carry an id. Session
//   0 always exists and cannot be closed, so a single-context host -- and a
//   shell one-liner -- can ignore session management entirely.
//
// Requests
//   OPEN                             allocate a session, server picks the id
//   CLOSE <sid>                      destroy it (session 0 is rejected)
//   PUSH <sid> <key>                 append one layout key
//   POP <sid>                        drop the last key (no-op when empty)
//   CLEAR <sid>                      empty the session
//   KEYS <sid>                       the session's keys
//   CAND <sid> <mode> [max] [dedup]  candidates for the session's keys
//   SHAPE <mode> <latin>             stateless: shape a whole Latin string
//   DECOMPOSE <latin>                every way of splitting the input into keys
//   QUIT                             end the conversation
//
// <mode> is "severe" or "fuzzy". [max] is a candidate cap (0 = no cap) and
// [dedup] is 0 or 1; both default to the Options defaults. An unknown <sid>
// is an error and changes nothing.
//
// Responses
//   OK                         success, no payload
//   OK<TAB>item<TAB>item...    success with a payload
//   ERR<TAB><message>          the request was rejected; the session is
//                              unchanged, so a client may carry on
//
// DECOMPOSE returns one item per split, the keys of a split joined by spaces,
// because a split is itself a list.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "zcode/zcode.h"

namespace zcode::service {

class Protocol {
public:
    // `engine` must outlive the protocol.
    explicit Protocol(const Engine& engine);
    Protocol(Protocol&&) noexcept;
    Protocol& operator=(Protocol&&) noexcept;
    ~Protocol();

    // Handles one request line and returns the response line, without the
    // terminating LF. Sets *quit when the client asked to stop; the response
    // is still returned so the caller can write it before closing.
    std::string handle(std::string_view line, bool* quit = nullptr);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace zcode::service
