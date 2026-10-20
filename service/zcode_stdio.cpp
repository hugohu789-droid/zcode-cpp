// SPDX-License-Identifier: GPL-2.0-or-later
//
// zcode_stdio: the shaping engine as a line-oriented service on stdin/stdout.
//
// This exists so a host can use the engine from another process. That keeps
// this GPL-2.0-or-later code out of a host's own linkage, and makes the engine
// usable from a host written in any language. The protocol is in protocol.h
// and is deliberately simple enough to drive from a shell:
//
//   printf 'SHAPE fuzzy sain\nQUIT\n' | zcode_stdio
//
// Exit status: 0 on clean end of input or QUIT, 1 when the engine's embedded
// tables fail to load (which the test suite rules out).

#include <cstdio>
#include <iostream>
#include <string>

#include "protocol.h"
#include "zcode/zcode.h"

int main() {
    std::string error;
    std::optional<zcode::Engine> engine = zcode::Engine::create(&error);
    if (!engine.has_value()) {
        std::fprintf(stderr, "zcode_stdio: %s\n",
                     error.empty() ? "engine tables failed to load"
                                   : error.c_str());
        return 1;
    }

    // Unbuffered-enough for a request/response conversation: a client that
    // writes a line and waits must see the response without sending more.
    std::ios::sync_with_stdio(false);

    zcode::service::Protocol protocol(*engine);
    std::string line;
    while (std::getline(std::cin, line)) {
        bool quit = false;
        const std::string response = protocol.handle(line, &quit);
        std::cout << response << '\n' << std::flush;
        if (quit) break;
    }
    return 0;
}
