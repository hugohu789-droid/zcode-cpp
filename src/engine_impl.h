// SPDX-License-Identifier: GPL-2.0-or-later
//
// Engine::Impl, shared by engine.cpp and session.cpp.
#pragma once

#include <span>
#include <string>
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

    std::vector<ZString> splice(std::span<const std::string> keys, Mode mode) const {
        SpliceResult r = mode == Mode::severe ? splicer.severe(keys) : splicer.fuzzy(keys);
        std::vector<ZString> out;
        out.reserve(r.size());
        for (std::size_t i = 0; i < r.size(); ++i) {
            out.push_back(r.to_zstring(i, tables));
        }
        return out;
    }
};

}  // namespace zcode
