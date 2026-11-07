// SPDX-License-Identifier: GPL-2.0-or-later
//
// zcode-cpp: the east-mod/ime Mongolian letter-shaping engine, ported to C++20.
// Public API. Ported from the splicing half of
// com.zorigt.ime.input.method.service.InputMethodService (no dictionary, no
// network, no database).
//
// Encoding
// --------
// The engine's output is ZCode, a form-code (形码) encoding: every code point
// is one glyph component (tooth, belly, tail, ...), and a written letter is a
// run of one to three components. ZCode borrows code points U+1865..U+18AA of
// the Unicode Mongolian block; they are output exactly as the Java original
// produces them, with no conversion. Render them with a ZCode (Z52) font.
//
// Internally a ZCode string is a std::u32string of zcode::CodePoint
// (char32_t), one element per code point. All ZCode values are in the BMP, so
// this is also exactly one element per Java UTF-16 `char` of the original.
// Use zcode::to_utf8() at the boundary when UTF-8 is needed.
//
// Latin input is a byte string (UTF-8). Only the ASCII keys of the layout
// (a e i o u n b p h g m l s x t d q j y r w f k c z H R L Z C) are
// meaningful; every other byte is skipped, as the Java Decomposer skips every
// other UTF-16 unit. Case matters: H R L Z C are the galig letters.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zcode {

using CodePoint = char32_t;
using ZString = std::u32string;

enum class Mode {
    severe,  // LetterSplicer.severe(): full orthographic rules, last letter in final form
    fuzzy,   // LetterSplicer.fuzzy(): relaxed rules, last letter medial or final (prefix search)
};

// Output options. The defaults reproduce the Java original, apart from the
// two bug fixes listed in README.md ("Deviations from the Java original").
struct Options {
    // Return at most this many candidates: the first ones in the original
    // order. 0 = no limit (the original's behaviour). Not in the original.
    std::size_t max_candidates = 0;
    // Drop every candidate whose ZCode equals an earlier one, keeping the
    // first occurrence (stable). Duplicates arise when different shapes have
    // the same components (e.g. both G "double achvg" medials, or t/d heads).
    // max_candidates then counts distinct candidates. Not in the original.
    bool deduplicate = false;
};

class Engine {
public:
    // Builds an engine from the data tables embedded at build time. Returns
    // nullopt (and fills *error when given) only if the embedded tables are
    // inconsistent, which the test suite rules out.
    static std::optional<Engine> create(std::string* error = nullptr);

    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    ~Engine();

    // Latin input -> ZCode candidates, in the order the Java original yields
    // them. Blank input yields nothing; otherwise the input is decomposed into
    // key sequences and every sequence is spliced, results concatenated.
    std::vector<ZString> shape(std::string_view latin, Mode mode, const Options& options = {}) const;

    // Same as shape(), each candidate encoded as UTF-8.
    std::vector<std::string> shape_utf8(std::string_view latin, Mode mode, const Options& options = {}) const;

    // LetterSplicer.severe()/fuzzy() on an explicit key sequence. Unknown keys
    // and keys without a shape at their position are skipped, as in Java.
    std::vector<ZString> splice(std::span<const std::string> keys, Mode mode, const Options& options = {}) const;

    // The number of candidates splice(keys, mode) returns, computed without
    // building them (dynamic programming over splicing states); saturates at
    // UINT64_MAX. Useful to decide on max_candidates first.
    std::uint64_t count(std::span<const std::string> keys, Mode mode) const;

    // Decomposer.decompose(): every way of splitting the input into layout keys.
    std::vector<std::vector<std::string>> decompose(std::string_view latin) const;

    // KeyFilterFactory.get(last_key + "-tail"), then KeyFilter.accept(candidate);
    // true when no filter is registered for that key. The original applies this
    // to dictionary words in the fuzzy path.
    bool key_filter_accepts(std::string_view last_key, std::u32string_view candidate) const;

private:
    friend class Session;
    struct Impl;
    explicit Engine(std::unique_ptr<const Impl> impl);
    std::unique_ptr<const Impl> impl_;
};

// Incremental splicing for per-keystroke use (not in the Java original).
//
// candidates(mode) always equals engine.splice(keys(), mode), but a session
// caches the work for all keys but the last, so pushing one key costs one
// layer of splicing instead of re-splicing the whole sequence. Each mode is
// computed only when first asked for. The engine must outlive the session.
class Session {
public:
    explicit Session(const Engine& engine);
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;
    ~Session();

    void push(std::string_view key);  // append one layout key
    void pop();                       // remove the last key (backspace); no-op when empty
    void clear();

    const std::vector<std::string>& keys() const;

    // The candidates for keys(), in the same order as Engine::splice().
    std::vector<ZString> candidates(Mode mode, const Options& options = {});

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Encodes code points as UTF-8. Code points that are not Unicode scalar
// values are written as U+FFFD.
std::string to_utf8(std::u32string_view s);

// The inverse, for text arriving from outside. Malformed input is rejected
// rather than repaired: a host handing this engine a word to remember should
// be told its bytes were wrong, not have a U+FFFD stored under the user's
// name. Returns false and leaves `out` empty on bad input.
bool from_utf8(std::string_view text, std::u32string& out);

}  // namespace zcode
