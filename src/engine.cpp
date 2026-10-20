// SPDX-License-Identifier: GPL-2.0-or-later
//
// Public API. Ported from the splicing half of
// com.zorigt.ime.input.method.service.InputMethodService (severeMakeWord and
// the candidate generation of fuzzyMakeWord); the BK-tree dictionary lookup,
// black-sequence cache and thread pool are deliberately not ported.
#include "zcode/zcode.h"

#include <utility>

#include "decomposer.h"
#include "key_filter.h"
#include "keyboard_layout.h"
#include "letter_splicer.h"
#include "splice_table.h"
#include "strings.h"
#include "tables.h"

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

std::optional<Engine> Engine::create(std::string* error) {
    Tables tables;
    std::string message;
    if (!Tables::parse_embedded(tables, message)) {
        if (error) *error = std::move(message);
        return std::nullopt;
    }
    return Engine(std::make_unique<const Impl>(std::move(tables)));
}

Engine::Engine(std::unique_ptr<const Impl> impl) : impl_(std::move(impl)) {}
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;
Engine::~Engine() = default;

std::vector<ZString> Engine::shape(std::string_view latin, Mode mode) const {
    std::vector<ZString> out;
    if (is_blank(latin)) {
        return out;
    }
    for (const std::vector<std::string>& keys : impl_->decomposer.decompose(latin)) {
        for (ZString& z : impl_->splice(keys, mode)) {
            out.push_back(std::move(z));
        }
    }
    return out;
}

std::vector<std::string> Engine::shape_utf8(std::string_view latin, Mode mode) const {
    std::vector<std::string> out;
    for (const ZString& z : shape(latin, mode)) {
        out.push_back(to_utf8(z));
    }
    return out;
}

std::vector<ZString> Engine::splice(std::span<const std::string> keys, Mode mode) const {
    return impl_->splice(keys, mode);
}

std::vector<std::vector<std::string>> Engine::decompose(std::string_view latin) const {
    return impl_->decomposer.decompose(latin);
}

bool Engine::key_filter_accepts(std::string_view last_key, std::u32string_view candidate) const {
    std::string key(last_key);
    key += "-tail";
    const KeyFilter* filter = impl_->filters.get(key);
    return filter == nullptr || filter->accept(candidate);
}

std::string to_utf8(std::u32string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (char32_t c : s) {
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            c = 0xFFFD;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

}  // namespace zcode
