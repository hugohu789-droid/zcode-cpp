// SPDX-License-Identifier: GPL-2.0-or-later
//
// Engine::Impl, shared by engine.cpp and session.cpp.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "decomposer.h"
#include "key_filter.h"
#include "keyboard_layout.h"
#include "letter_splicer.h"
#include "splice_table.h"
#include "tables.h"
#include "zcode/zcode.h"

namespace zcode {

struct Engine::Impl {
    explicit Impl(Tables t)
        : tables(std::move(t)),
          layout(tables),
          decomposer(layout.keys()),
          verifier(tables),
          splicer(tables, layout, verifier),
          filters(tables) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    Tables tables;
    KeyboardLayout layout;
    Decomposer decomposer;
    SpliceTable verifier;
    LetterSplicer splicer;
    KeyFilterFactory filters;

    // What `out` already contains, kept only when deduplicating (MSVC's
    // unordered_set allocates even when default-constructed, so the default
    // path must not create one).
    using Seen = std::optional<std::unordered_set<ZString>>;
    static Seen make_seen(const Options& options) {
        return options.deduplicate ? Seen(std::in_place) : Seen();
    }

    // Materialises `layer` into `out` in order, honouring Options. `seen` is
    // shared across the calls that fill one `out`. Returns false once out is
    // full.
    bool collect(const std::vector<SpliceNode>& nodes, const std::vector<std::uint32_t>& layer,
                 const Options& options, std::vector<ZString>& out, Seen& seen) const {
        std::size_t room = layer.size();
        if (options.max_candidates != 0) {
            room = std::min(room, options.max_candidates > out.size() ? options.max_candidates - out.size() : 0);
        }
        out.reserve(out.size() + room);
        for (std::uint32_t node : layer) {
            if (options.max_candidates != 0 && out.size() >= options.max_candidates) {
                return false;
            }
            ZString z = materialize(nodes, node, tables);
            if (seen && !seen->insert(z).second) {
                continue;
            }
            out.push_back(std::move(z));
        }
        return options.max_candidates == 0 || out.size() < options.max_candidates;
    }

    std::vector<ZString> splice(std::span<const std::string> keys, Mode mode, const Options& options) const {
        SpliceResult r = mode == Mode::severe ? splicer.severe(keys) : splicer.fuzzy(keys);
        std::vector<ZString> out;
        Seen seen = make_seen(options);
        collect(r.nodes, r.candidates, options, out, seen);
        return out;
    }
};

}  // namespace zcode
