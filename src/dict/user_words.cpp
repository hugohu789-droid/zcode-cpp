// SPDX-License-Identifier: GPL-2.0-or-later
//
// UserWords: the user's own words and habits. Not in the Java original,
// which has neither.
//
// Kept deliberately small. The matching is a linear scan with the same two
// cheap tests the index uses as its first prunes, because a user's own list
// is hundreds of words where the dictionary is 38,743: a trie over it would
// cost more to rebuild on every commit than the scan costs to run. The
// number this has to beat is a few microseconds per spliced sequence, and a
// scan of a few hundred short words is well inside that.

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

#include "zcode/dictionary.h"

namespace zcode {
namespace {

// The dictionary's own hit test, which this has to agree with or a user word
// would appear where a dictionary word could not: the query is a
// subsequence of the word, and the word is long enough to be the query with
// insertions but less than twice as long.
bool is_hit(std::u32string_view query, std::u32string_view word) {
    const std::size_t len = query.size();
    if (len == 0 || word.size() < len || word.size() >= 2 * len) {
        return false;
    }
    // The JaroWinkler filter in ScoreMarker reduces to this, and dropping a
    // word here saves walking it.
    if (word[0] != query[0]) {
        return false;
    }
    std::size_t matched = 0;
    for (const char32_t c : word) {
        if (matched < len && c == query[matched]) {
            ++matched;
        }
    }
    return matched == len;
}

}  // namespace

struct UserWords::Impl {
    Options options;
    // Ordered, so entries() comes out in a stable order without sorting and
    // a host diffing two dumps sees only what changed.
    std::map<ZString, Entry> by_word;
    mutable std::mutex mutex;
};

UserWords::UserWords() : impl_(std::make_unique<Impl>()) {}

UserWords::UserWords(Options options) : impl_(std::make_unique<Impl>()) {
    impl_->options = options;
}

UserWords::UserWords(UserWords&&) noexcept = default;
UserWords& UserWords::operator=(UserWords&&) noexcept = default;
UserWords::~UserWords() = default;

const UserWords::Options& UserWords::options() const { return impl_->options; }

void UserWords::set_options(Options options) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->options = options;
}

void UserWords::restore(const Entry& entry) {
    if (entry.word.empty() || entry.count == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Entry& slot = impl_->by_word[entry.word];
    if (slot.word.empty()) {
        slot = entry;
        return;
    }
    // Replaying the same store twice must not double a count, and must not
    // move a word backwards in time.
    slot.count = std::max(slot.count, entry.count);
    slot.tick = std::max(slot.tick, entry.tick);
}

void UserWords::learn(const ZString& word, std::uint64_t tick) {
    if (word.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Entry& slot = impl_->by_word[word];
    slot.word = word;
    // Saturating rather than wrapping: a count that wrapped would throw away
    // a habit the user spent years building.
    if (slot.count != UINT32_MAX) {
        ++slot.count;
    }
    slot.tick = std::max(slot.tick, tick);
}

void UserWords::forget(const ZString& word) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->by_word.erase(word);
}

void UserWords::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->by_word.clear();
}

std::size_t UserWords::size() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->by_word.size();
}

std::vector<UserWords::Entry> UserWords::entries() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<Entry> out;
    out.reserve(impl_->by_word.size());
    for (const auto& [word, entry] : impl_->by_word) {
        out.push_back(entry);
    }
    return out;
}

float UserWords::bonus(const ZString& word, std::uint64_t now) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto it = impl_->by_word.find(word);
    if (it == impl_->by_word.end() || it->second.count == 0) {
        return 0;
    }
    const Options& o = impl_->options;
    if (o.weight == 0 || o.half_life <= 0) {
        return 0;
    }
    const float count = static_cast<float>(it->second.count);
    const float strength = count / (count + o.saturation);
    // A tick in the future means the host's clock moved backwards; treat it
    // as "now" rather than letting the exponent blow the bonus up.
    const std::uint64_t age = now > it->second.tick ? now - it->second.tick : 0;
    const float decay = std::pow(0.5F, static_cast<float>(age) / o.half_life);
    return o.weight * strength * decay;
}

std::vector<ZString> UserWords::match(std::u32string_view spliced) const {
    std::vector<ZString> out;
    if (spliced.empty()) {
        return out;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& [word, entry] : impl_->by_word) {
        if (is_hit(spliced, word)) {
            out.push_back(word);
        }
    }
    return out;
}

}  // namespace zcode
