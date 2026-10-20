// SPDX-License-Identifier: GPL-2.0-or-later
//
// zcode::Session: incremental LetterSplicer (no Java counterpart).
//
// In LetterSplicer the key at index k takes HEAD shapes if k == 0, the
// last-key shapes if k == n-1, and MIDDLE shapes otherwise. So the layer after
// keys 0..k-1 depends only on those keys as long as none of them is the last
// one, and is shared by every longer sequence with the same prefix. A session
// keeps these "frontier" layers per mode and, for candidates(), runs only the
// final step with the last key's shapes, using the same LetterSplicer::step()
// as the stateless splice.
#include <cstdint>

#include "engine_impl.h"

namespace zcode {

struct Session::State {
    struct Track {
        std::vector<SpliceNode> nodes;
        // frontiers[k]: layer after keys 0..k, all spliced as non-last keys
        std::vector<std::vector<std::uint32_t>> frontiers;
        std::vector<char> started;  // step()'s `started` after frontiers[k]
        std::vector<std::size_t> marks;  // nodes.size() right after frontiers[k]
        std::vector<std::uint32_t> scratch;
    };

    const Engine::Impl* engine = nullptr;
    std::vector<std::string> keys;
    const std::vector<std::uint32_t> empty_layer;
    Track tracks[2];  // 0 = severe, 1 = fuzzy

    template <bool Fuzzy>
    void advance(Track& t, std::size_t count) {
        while (t.frontiers.size() < count) {
            std::size_t k = t.frontiers.size();
            const std::vector<std::uint32_t>& current = k == 0 ? empty_layer : t.frontiers[k - 1];
            Location loc = k == 0 ? Location::head : Location::middle;
            const bool started = k == 0 ? false : t.started[k - 1] != 0;
            std::vector<std::uint32_t> next;
            bool now = engine->splicer.step<Fuzzy>(t.nodes, current, started, engine->layout.get(keys[k], loc), next);
            t.frontiers.push_back(std::move(next));
            t.started.push_back(now ? 1 : 0);
            t.marks.push_back(t.nodes.size());
        }
    }

    template <bool Fuzzy>
    std::vector<ZString> candidates(const Options& options) {
        Track& t = tracks[Fuzzy ? 1 : 0];
        std::vector<ZString> out;
        Engine::Impl::Seen seen = Engine::Impl::make_seen(options);
        const std::size_t n = keys.size();
        if (n == 0) {
            return out;
        }
        if (n == 1) {  // the only key is the first one: HEAD shapes, i.e. frontiers[0]
            advance<Fuzzy>(t, 1);
            engine->collect(t.nodes, t.frontiers[0], options, out, seen);
            return out;
        }
        advance<Fuzzy>(t, n - 1);
        const std::size_t mark = t.nodes.size();
        engine->splicer.step<Fuzzy>(t.nodes, t.frontiers[n - 2], t.started[n - 2] != 0,
                                    engine->splicer.shapes_for(keys[n - 1], n - 1, n, Fuzzy), t.scratch);
        engine->collect(t.nodes, t.scratch, options, out, seen);
        t.nodes.resize(mark);  // the last-key layer is not reused
        return out;
    }

    void truncate(std::size_t keep) {
        for (Track& t : tracks) {
            if (t.frontiers.size() > keep) {
                t.frontiers.resize(keep);
                t.started.resize(keep);
                t.marks.resize(keep);
                t.nodes.resize(keep == 0 ? 0 : t.marks[keep - 1]);
            }
        }
    }
};

Session::Session(const Engine& engine) : state_(std::make_unique<State>()) { state_->engine = engine.impl_.get(); }
Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;
Session::~Session() = default;

void Session::push(std::string_view key) { state_->keys.emplace_back(key); }

void Session::pop() {
    if (state_->keys.empty()) return;
    state_->keys.pop_back();
    state_->truncate(state_->keys.size());  // frontiers[k] needs keys 0..k
}

void Session::clear() {
    state_->keys.clear();
    state_->truncate(0);
}

const std::vector<std::string>& Session::keys() const { return state_->keys; }

std::vector<ZString> Session::candidates(Mode mode, const Options& options) {
    return mode == Mode::severe ? state_->candidates<false>(options) : state_->candidates<true>(options);
}

}  // namespace zcode
