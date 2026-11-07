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
//   LOOKUP <what> <latin>            dictionary words, not raw letter shapes
//   DICT                             what dictionary is loaded
//   QUIT                             end the conversation
//
// <mode> selects what a session query answers with. The first two are the
// splicer's own modes and answer with letter shapes; the other three go
// through the dictionary and answer with words, so they are rejected when
// this build has none:
//
//   severe  letter shapes, the last letter final (the word is finished)
//   fuzzy   letter shapes, the last letter medial or final (still typing)
//   word    dictionary words, best score first
//   score   the same words, each with every metric behind its ranking
//   make    severe shapes and dictionary words merged, as /ime/{key} does
//   full    "score", then the severe shapes it does not already contain,
//           written as scored items with the metrics zeroed -- so the answer
//           carries every metric and is never empty
//
// A host picks one and stays with it; which one is the host's policy, not the
// engine's. An interactive host wants "full": the dictionary holds 38,743
// words, so the modes that answer with words alone leave the candidate
// window blank whenever someone types one that is not among them. The dictionary modes take the session's keys joined back into the
// Latin string and decompose it again themselves, so a host may push keys in
// any grouping.
//
// [max] is a candidate cap (0 = no cap) and [dedup] is 0 or 1; both default
// to the Options defaults and both apply in every mode. `max` matters most
// in "score": a few hundred words each carrying six metrics is a response of
// well over 100 KB, which a host is right to treat as a protocol error.
// An unknown <sid> is an error and changes nothing.
//
// LOOKUP is the stateless form of the three dictionary modes above, named
// after the original's HTTP endpoints so the two can be compared answer for
// answer. It takes no cap, so prefer CAND/TYPE for a host.
//
// LOOKUP's <what> selects one of the three methods the original service
// exposes over HTTP, and the names match those endpoints:
//
//   make    /ime/{key}              severe and fuzzy merged
//   fuzzy   /ime/fuzzy/{key}        dictionary words, best score first
//   score   /ime/fuzzy/score/{key}  the same, each with its metrics
//   severe  /ime/severe/{key}       no dictionary; same as SHAPE severe
//
// LOOKUP is rejected when the service was started without a dictionary.
// A host that does not know whether this build has one should ask DICT,
// which answers with the word count and the index in use.
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

#include "zcode/dictionary.h"
#include "zcode/zcode.h"

namespace zcode::service {

class Protocol {
public:
    // `engine` and `dictionary` must outlive the protocol. A null dictionary
    // leaves the shaping commands working and LOOKUP rejected.
    explicit Protocol(const Engine& engine, const Dictionary* dictionary = nullptr);
    Protocol(Protocol&&) noexcept;
    Protocol& operator=(Protocol&&) noexcept;
    ~Protocol();

    // Handles one request line and returns the response line, without the
    // terminating LF. Sets *quit when the client asked to stop; the response
    // is still returned so the caller can write it before closing.
    //
    // Sets *dictionary_miss when the request did consult the dictionary and
    // it had nothing to offer. That is the moment an optional extension pack
    // is worth reading, and it is reported here rather than guessed from the
    // request line, because which modes consult the dictionary is this
    // layer's business. Note that `make` merges the splicer's own shapes in,
    // so its answer is rarely empty and rarely reports a miss.
    std::string handle(std::string_view line, bool* quit = nullptr,
                       bool* dictionary_miss = nullptr);

    // The service layer, so a host can attach an extension pack to it.
    // Throws nothing; the reference is valid only when a dictionary was
    // given to the constructor, which has_dictionary() reports.
    bool has_dictionary() const;
    Ime& ime();

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace zcode::service
