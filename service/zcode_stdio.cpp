// SPDX-License-Identifier: GPL-2.0-or-later
//
// zcode_stdio: the engine as a line-oriented service on stdin/stdout.
//
// This exists so a host can use the engine from another process. That keeps
// this GPL-2.0-or-later code out of a host's own linkage, and makes the engine
// usable from a host written in any language. The protocol is in protocol.h
// and is deliberately simple enough to drive from a shell:
//
//   printf 'SHAPE fuzzy sain\nQUIT\n' | zcode_stdio
//   printf 'LOOKUP fuzzy bagsi\nQUIT\n' | zcode_stdio
//
// Usage
//   zcode_stdio [--extension PATH] [--preload-extension]
//
// --extension names the optional pack of the 461,182 words the original
// never loads (tools/extract_dictionary.py --extension). It is consulted
// only when the compiled-in dictionary answers with nothing, and by default
// it is not even read until that first happens: loading it costs a few
// hundred milliseconds, which is worth skipping for a session that never
// needs it. That first empty answer is returned empty -- the load happens on
// another thread and the pack is in place for the next keystroke, rather
// than making the user wait mid-word. --preload-extension reads it at
// startup instead, for a host that would rather pay once up front.
//
// Exit status: 0 on clean end of input or QUIT, 1 when the engine's embedded
// tables fail to load (which the test suite rules out).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "protocol.h"
#include "zcode/dictionary.h"
#include "zcode/zcode.h"

namespace {

struct Options {
    std::string extension_path;
    bool preload = false;
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--extension" && i + 1 < argc) {
            out.extension_path = argv[++i];
        } else if (arg == "--preload-extension") {
            out.preload = true;
        } else {
            std::fprintf(stderr,
                         "zcode_stdio: unknown argument %s\n"
                         "usage: zcode_stdio [--extension PATH] "
                         "[--preload-extension]\n",
                         arg.c_str());
            return false;
        }
    }
    return true;
}

// Loads the extension pack once, on a thread of its own, and hands it to the
// Ime when it is ready. The pack is never unloaded, so the pointer the Ime
// gets stays valid for the life of the process.
class ExtensionLoader {
public:
    ExtensionLoader(std::string path, zcode::Ime& ime) : path_(std::move(path)), ime_(ime) {}

    ~ExtensionLoader() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void start() {
        if (path_.empty() || started_.exchange(true)) {
            return;
        }
        thread_ = std::thread([this] {
            const auto t0 = std::chrono::steady_clock::now();
            std::string error;
            // Index::compact: a fallback consulted only when the main
            // dictionary is empty does not need microsecond lookups, and
            // the trie would cost 100 MB for this many words.
            auto loaded = zcode::Dictionary::from_file(
                path_, zcode::Dictionary::Index::compact, &error);
            if (!loaded.has_value()) {
                std::fprintf(stderr, "zcode_stdio: extension pack not loaded: %s\n",
                             error.c_str());
                return;
            }
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                    .count();
            std::fprintf(stderr, "zcode_stdio: extension pack: %zu words in %.0f ms\n",
                         loaded->size(), ms);
            dictionary_ = std::make_unique<zcode::Dictionary>(std::move(*loaded));
            ime_.set_extension(dictionary_.get());
        });
    }

    void wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    std::string path_;
    zcode::Ime& ime_;
    std::atomic<bool> started_{false};
    std::thread thread_;
    std::unique_ptr<zcode::Dictionary> dictionary_;
};

// "OK" with no payload, in answer to a dictionary lookup, is the signal that
// the compiled-in dictionary had nothing -- which is when the extension pack
// is worth reading.
bool empty_lookup(std::string_view request, std::string_view response) {
    if (response != "OK") {
        return false;
    }
    return request.rfind("LOOKUP", 0) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return 2;
    }

    std::string error;
    std::optional<zcode::Engine> engine = zcode::Engine::create(&error);
    if (!engine.has_value()) {
        std::fprintf(stderr, "zcode_stdio: %s\n",
                     error.empty() ? "engine tables failed to load" : error.c_str());
        return 1;
    }

    // The dictionary is part of this build, so a failure to load it is a
    // build problem; say so and carry on with shaping only, rather than
    // refusing to start and taking the working half down with it.
    std::optional<zcode::Dictionary> dictionary = zcode::Dictionary::create(&error);
    if (!dictionary.has_value()) {
        std::fprintf(stderr, "zcode_stdio: dictionary not loaded: %s\n",
                     error.empty() ? "unknown reason" : error.c_str());
    }

    // Unbuffered-enough for a request/response conversation: a client that
    // writes a line and waits must see the response without sending more.
    std::ios::sync_with_stdio(false);

    zcode::service::Protocol protocol(*engine,
                                      dictionary.has_value() ? &dictionary.value() : nullptr);
    std::unique_ptr<ExtensionLoader> extension;
    if (!options.extension_path.empty() && dictionary.has_value()) {
        extension = std::make_unique<ExtensionLoader>(options.extension_path,
                                                      protocol.ime());
        if (options.preload) {
            extension->start();
            extension->wait();
        }
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        bool quit = false;
        const std::string response = protocol.handle(line, &quit);
        std::cout << response << '\n' << std::flush;
        if (extension != nullptr && empty_lookup(line, response)) {
            extension->start();
        }
        if (quit) break;
    }
    return 0;
}
