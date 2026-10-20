// SPDX-License-Identifier: GPL-2.0-or-later
//
// Benchmark for zcode-cpp. Inputs come from the golden files so the workloads
// are reproducible; reading them is outside the timed region.
//
//   zcode_bench [repetitions]   (testdata dir is fixed at build time)
//
// For every workload prints min and median wall time over the repetitions and
// a checksum (total candidates and code points) that must not change between
// versions of the engine.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "test_util.h"
#include "zcode/zcode.h"

namespace {

struct Checksum {
    unsigned long long candidates = 0;
    unsigned long long code_points = 0;
    void add(const std::vector<std::u32string>& zs) {
        candidates += zs.size();
        for (const auto& z : zs) code_points += z.size();
    }
};

void run(const char* name, int reps, const std::function<void(Checksum&)>& body) {
    std::vector<double> ms;
    Checksum sum;
    for (int r = 0; r < reps; ++r) {
        Checksum c;
        auto t0 = std::chrono::steady_clock::now();
        body(c);
        auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        sum = c;
    }
    std::sort(ms.begin(), ms.end());
    std::printf("%-22s min %9.2f ms  median %9.2f ms  candidates %llu  code_points %llu\n", name, ms.front(),
                ms[ms.size() / 2], sum.candidates, sum.code_points);
}

}  // namespace

int main(int argc, char** argv) {
    int reps = argc > 1 ? std::max(1, std::atoi(argv[1])) : 7;
    // startup: load the embedded tables, build the engine, first result
    auto t_start = std::chrono::steady_clock::now();
    auto engine = zcode::Engine::create();
    if (!engine) return 1;
    auto first = engine->shape("mongol", zcode::Mode::severe);
    std::printf("startup to first result  %.2f ms (process start not included; %zu candidate)\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count(),
                first.size());

    std::vector<std::vector<std::string>> key_lists;
    for (const auto& r : zcode::test::read_tsv("golden_splice.tsv")) {
        if (r[0] == "severe") key_lists.push_back(zcode::test::split(r[1], ' '));
    }
    std::vector<std::string> words;
    for (const auto& r : zcode::test::read_tsv("golden_e2e.tsv")) {
        if (r[0] == "severe") words.push_back(zcode::test::unescape_to_utf8(r[1]));
    }
    const std::vector<std::string> heavy = {"gggggg", "hghghg", "iiiiiiiiii", "hhhhhh", "hagalgagdahu", "gogugaga"};

    std::printf("%zu key lists, %zu words\n", key_lists.size(), words.size());
    run("splice_all_severe", reps, [&](Checksum& c) {
        for (const auto& k : key_lists) c.add(engine->splice(k, zcode::Mode::severe));
    });
    run("splice_all_fuzzy", reps, [&](Checksum& c) {
        for (const auto& k : key_lists) c.add(engine->splice(k, zcode::Mode::fuzzy));
    });
    // Re-shape every prefix, as an IME does on each keystroke.
    run("typing_severe", reps, [&](Checksum& c) {
        for (const auto& w : words)
            for (std::size_t n = 1; n <= w.size(); ++n) c.add(engine->shape(w.substr(0, n), zcode::Mode::severe));
    });
    run("typing_fuzzy", reps, [&](Checksum& c) {
        for (const auto& w : words)
            for (std::size_t n = 1; n <= w.size(); ++n) c.add(engine->shape(w.substr(0, n), zcode::Mode::fuzzy));
    });
    // Per-keystroke candidates on key lists: stateless splice of every prefix
    // versus one Session fed key by key.
    std::vector<std::vector<std::string>> typed;
    for (const auto& w : words) {
        auto d = engine->decompose(w);
        if (d.size() == 1 && !d[0].empty()) typed.push_back(d[0]);
    }
    for (zcode::Mode mode : {zcode::Mode::severe, zcode::Mode::fuzzy}) {
        const bool severe = mode == zcode::Mode::severe;
        run(severe ? "keys_stateless_severe" : "keys_stateless_fuzzy", reps, [&](Checksum& c) {
            for (const auto& k : typed)
                for (std::size_t n = 1; n <= k.size(); ++n)
                    c.add(engine->splice(std::span<const std::string>(k.data(), n), mode));
        });
        run(severe ? "keys_session_severe" : "keys_session_fuzzy", reps, [&](Checksum& c) {
            zcode::Session s(*engine);
            for (const auto& k : typed) {
                s.clear();
                for (const auto& key : k) {
                    s.push(key);
                    c.add(s.candidates(mode));
                }
            }
        });
    }
    run("heavy_fuzzy", reps, [&](Checksum& c) {
        for (const auto& w : heavy) c.add(engine->shape(w, zcode::Mode::fuzzy));
    });
    // Output options (not in the original): first 100 candidates only, and
    // counting without building candidates.
    zcode::Options cap100;
    cap100.max_candidates = 100;
    run("heavy_fuzzy_cap100", reps, [&](Checksum& c) {
        for (const auto& w : heavy) c.add(engine->shape(w, zcode::Mode::fuzzy, cap100));
    });
    zcode::Options dedupe;
    dedupe.deduplicate = true;
    run("splice_all_fuzzy_dedup", reps, [&](Checksum& c) {
        for (const auto& k : key_lists) c.add(engine->splice(k, zcode::Mode::fuzzy, dedupe));
    });
    run("count_heavy_fuzzy", reps, [&](Checksum& c) {
        for (const auto& w : heavy)
            for (const auto& k : engine->decompose(w)) c.candidates += engine->count(k, zcode::Mode::fuzzy);
    });
    return 0;
}
