// SPDX-License-Identifier: GPL-2.0-or-later
//
// Where a slow dictionary keystroke spends its time.
//
// A keystroke in a dictionary mode decomposes the input, splices each
// decomposition into candidate sequences, and looks every sequence up. Two
// inputs that both take over 100 ms do so for different reasons -- one
// produces thousands of sequences, the other produces few sequences but
// hundreds of matches each -- and merging the lookups only helps the first.
// This splits the time so the right thing gets optimised.
//
//   zcode_dict_profile [latin ...]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "zcode/dictionary.h"
#include "zcode/zcode.h"

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    std::string error;
    auto engine = zcode::Engine::create(&error);
    if (!engine.has_value()) {
        std::fprintf(stderr, "engine: %s\n", error.c_str());
        return 1;
    }
    auto dictionary = zcode::Dictionary::create(&error);
    if (!dictionary.has_value()) {
        std::fprintf(stderr, "dictionary: %s\n", error.c_str());
        return 1;
    }

    std::vector<std::string> inputs;
    for (int i = 1; i < argc; ++i) inputs.emplace_back(argv[i]);
    if (inputs.empty()) {
        inputs = {"bagsi", "monggol", "aga", "uhe", "hghghg", "gggggg",
                  "surghaghuli", "iiiiiiiiii"};
    }

    std::printf("%-14s %7s %8s %8s %9s %9s %9s %9s\n", "input", "splits",
                "seqs", "distinct", "splice ms", "search ms", "score ms",
                "total ms");
    for (const std::string& latin : inputs) {
        // Splicing.
        auto t0 = Clock::now();
        const auto splits = engine->decompose(latin);
        std::vector<std::u32string> sequences;
        for (const auto& keys : splits) {
            for (auto& s : engine->splice(keys, zcode::Mode::fuzzy)) {
                sequences.push_back(std::move(s));
            }
        }
        const double splice_ms = ms_since(t0);

        // How many of those sequences are distinct. The splicer enumerates a
        // product of per-key shape choices and different choices can spell
        // the same code points, so a keystroke can look the same sequence up
        // many times over.
        std::vector<std::u32string> distinct = sequences;
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());

        // Searching, with no scoring at all: the index alone.
        zcode::Ime probe(*engine, *dictionary);
        probe.set_negative_cache_enabled(false);
        t0 = Clock::now();
        unsigned long long hits = 0;
        for (const std::u32string& sequence : sequences) {
            hits += dictionary->index_hits(sequence);
        }
        const double search_ms = ms_since(t0);

        // The whole thing, through the same path a keystroke takes.
        t0 = Clock::now();
        const auto words = probe.fuzzy_make_word(latin);
        const double total_ms = ms_since(t0);

        std::printf("%-14s %7zu %8zu %8zu %9.1f %9.1f %9.1f %9.1f   %llu hits -> %zu words\n",
                    latin.c_str(), splits.size(), sequences.size(),
                    distinct.size(), splice_ms, search_ms,
                    total_ms - search_ms - splice_ms, total_ms, hits,
                    words.size());
    }
    return 0;
}
