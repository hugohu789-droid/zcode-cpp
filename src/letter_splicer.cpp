// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.splice.LetterSplicer. fuzzy() and severe()
// are two copies of one loop in Java; they differ only in the shapes taken for
// the last key and in the Verifier method, so they share one template here.
// The per-rejection logging and timing of the original are not ported.
// Verifier checks go through the precomputed SpliceTable, and candidates are
// paths in a shared arena instead of copied sequences (see SpliceNode). One
// loop iteration is step(), which Session (session.cpp) also uses.
#include "letter_splicer.h"

#include <algorithm>

namespace zcode {

ZString materialize(const std::vector<SpliceNode>& nodes, std::uint32_t node, const Tables& tables) {
    // One allocation of the exact size, filled from the end of the path.
    ZString s(nodes[node].length, U'\0');
    std::size_t pos = s.size();
    for (std::uint32_t n = node; n != SpliceNode::no_parent; n = nodes[n].parent) {
        const ZString& part = tables.shapes[nodes[n].shape].shape;
        pos -= part.size();
        std::copy(part.begin(), part.end(), s.begin() + static_cast<std::ptrdiff_t>(pos));
    }
    return s;
}

LetterSplicer::LetterSplicer(const Tables& tables, const KeyboardLayout& layout, const SpliceTable& verifier)
    : tables_(tables), layout_(layout), verifier_(verifier) {
    for (const LetterShape& s : tables.shapes) {
        shape_nature_.push_back(nature_of(s.latin_key));
        shape_length_.push_back(static_cast<std::uint32_t>(s.shape.size()));
    }
}

std::span<const ShapeId> LetterSplicer::shapes_for(std::string_view key, std::size_t index, std::size_t count,
                                                   bool fuzzy) const {
    if (index == 0) {
        return layout_.get(key, Location::head);
    }
    if (index == count - 1) {
        return fuzzy ? layout_.get_middle_then_tail(key) : layout_.get(key, Location::tail);
    }
    return layout_.get(key, Location::middle);
}

template <bool Fuzzy>
bool LetterSplicer::step(std::vector<SpliceNode>& nodes, const std::vector<std::uint32_t>& current, bool started,
                         std::span<const ShapeId> shapes, std::vector<std::uint32_t>& next) const {
    next.clear();
    if (shapes.empty()) {
        next = current;  // Java: `continue`, the layer is kept
        return started;
    }
    next.reserve(std::max<std::size_t>(current.size(), 1) * shapes.size());
    // LetterShapeSequence.setNature(): a non-SAARMAG shape nature replaces the sequence's.
    auto next_nature = [this](Nature n, ShapeId shape) {
        Nature s = shape_nature_[shape];
        return s == Nature::saarmag ? n : s;
    };
    // Only the first key that has shapes starts sequences. Deviation from the
    // Java original (bug fix): Java restarts whenever the layer is empty, also
    // after every candidate was rejected, and then returns fragments of the
    // word's tail; here a rejected layer stays empty.
    if (!started) {
        for (ShapeId id : shapes) {
            if (verifier_.can_splice<Fuzzy>(FrontState{}, id)) {
                next.push_back(static_cast<std::uint32_t>(nodes.size()));
                nodes.push_back({SpliceNode::no_parent, shape_length_[id], id, next_nature(Nature::saarmag, id)});
            }
        }
        return true;
    }
    for (ShapeId id : shapes) {
        for (std::uint32_t clss : current) {
            const SpliceNode c = nodes[clss];
            if (verifier_.can_splice<Fuzzy>(FrontState{false, c.shape, c.nature}, id)) {
                next.push_back(static_cast<std::uint32_t>(nodes.size()));
                nodes.push_back({clss, c.length + shape_length_[id], id, next_nature(c.nature, id)});
            }
        }
    }
    return true;
}

template bool LetterSplicer::step<true>(std::vector<SpliceNode>&, const std::vector<std::uint32_t>&, bool,
                                        std::span<const ShapeId>, std::vector<std::uint32_t>&) const;
template bool LetterSplicer::step<false>(std::vector<SpliceNode>&, const std::vector<std::uint32_t>&, bool,
                                         std::span<const ShapeId>, std::vector<std::uint32_t>&) const;

template <bool Fuzzy>
std::uint64_t LetterSplicer::count(std::span<const std::string> latin_sequence) const {
    // counts[state] for state = last * 3 + nature; `started` as in step().
    const std::size_t states = tables_.shapes.size() * 3;
    std::vector<std::uint64_t> counts(states, 0), next(states, 0);
    std::uint64_t total = 0;
    bool started = false;
    auto add = [](std::uint64_t a, std::uint64_t b) { return a > UINT64_MAX - b ? UINT64_MAX : a + b; };
    for (std::size_t i = 0; i < latin_sequence.size(); ++i) {
        std::span<const ShapeId> shapes = shapes_for(latin_sequence[i], i, latin_sequence.size(), Fuzzy);
        if (shapes.empty()) {
            continue;  // Java: `continue`
        }
        std::fill(next.begin(), next.end(), 0);
        std::uint64_t next_total = 0;
        for (ShapeId id : shapes) {
            Nature own = shape_nature_[id];
            if (!started) {  // the first key with shapes starts sequences
                if (verifier_.can_splice<Fuzzy>(FrontState{}, id)) {
                    std::size_t s = static_cast<std::size_t>(id) * 3 + static_cast<std::size_t>(own);
                    next[s] = add(next[s], 1);
                    next_total = add(next_total, 1);
                }
                continue;
            }
            for (std::size_t st = 0; st < states; ++st) {
                if (counts[st] == 0) continue;
                FrontState fs{false, static_cast<ShapeId>(st / 3), static_cast<Nature>(st % 3)};
                if (verifier_.can_splice<Fuzzy>(fs, id)) {
                    Nature n = own == Nature::saarmag ? fs.nature : own;
                    std::size_t s = static_cast<std::size_t>(id) * 3 + static_cast<std::size_t>(n);
                    next[s] = add(next[s], counts[st]);
                    next_total = add(next_total, counts[st]);
                }
            }
        }
        counts.swap(next);
        total = next_total;
        started = true;
    }
    return total;
}

template std::uint64_t LetterSplicer::count<true>(std::span<const std::string>) const;
template std::uint64_t LetterSplicer::count<false>(std::span<const std::string>) const;

template <bool Fuzzy>
SpliceResult LetterSplicer::splice(std::span<const std::string> latin_sequence) const {
    SpliceResult result;
    std::vector<std::uint32_t> next;
    bool started = false;
    for (std::size_t i = 0; i < latin_sequence.size(); ++i) {
        started = step<Fuzzy>(result.nodes, result.candidates, started,
                              shapes_for(latin_sequence[i], i, latin_sequence.size(), Fuzzy), next);
        result.candidates.swap(next);
    }
    return result;
}

SpliceResult LetterSplicer::fuzzy(std::span<const std::string> latin_sequence) const {
    return splice<true>(latin_sequence);
}

SpliceResult LetterSplicer::severe(std::span<const std::string> latin_sequence) const {
    return splice<false>(latin_sequence);
}

}  // namespace zcode
