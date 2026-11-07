// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.input.method.service.InputMethodService
// (the dictionary half: run() and matchInBkMaps()).

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "dict/bk_tree.h"
#include "dict/java_float.h"
#include "dict/scan_index.h"
#include "dict/score_marker.h"
#include "dict/trie.h"
#include "dict/word_data.h"
#include "dict/word_store.h"
#include "zcode/dictionary.h"

namespace zcode {
namespace {

// InputMethodService.ROOT_STRING: the dummy word every BK tree is rooted on.
// Only the reference implementation needs it, but it has to be exactly this
// string, because the tree's shape is measured from it.
constexpr char32_t kRootString[] = {0x1865, 0x186A, 0x1871, 0x186A, 0x189D, 0x186F,
                                    0x186A, 0x189E, 0x186A, 0x1894, 0x1873};

// Decodes one UTF-8 word. The data is generated, so anything malformed is a
// build problem, not user input: report it rather than substituting U+FFFD.
bool utf8_to_u32(std::string_view text, ZString& out, std::string& error) {
    out.clear();
    std::size_t i = 0;
    while (i < text.size()) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        char32_t cp = 0;
        std::size_t extra = 0;
        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1Fu;
            extra = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0Fu;
            extra = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07u;
            extra = 3;
        } else {
            error = "malformed UTF-8 in the dictionary";
            return false;
        }
        if (i + extra >= text.size()) {
            error = "truncated UTF-8 in the dictionary";
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto b = static_cast<unsigned char>(text[i + k]);
            if ((b & 0xC0) != 0x80) {
                error = "malformed UTF-8 in the dictionary";
                return false;
            }
            cp = (cp << 6) | (b & 0x3Fu);
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return true;
}

bool parse_word_list(std::string_view tsv, std::vector<Word>& out, std::string& error) {
    std::size_t pos = 0;
    ZString text;
    while (pos < tsv.size()) {
        std::size_t end = tsv.find('\n', pos);
        if (end == std::string_view::npos) {
            end = tsv.size();
        }
        std::string_view line = tsv.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            error = "dictionary line without a frequency";
            return false;
        }
        if (!utf8_to_u32(line.substr(0, tab), text, error)) {
            return false;
        }
        std::uint64_t frequency = 0;
        const std::string_view number = line.substr(tab + 1);
        const auto result =
            std::from_chars(number.data(), number.data() + number.size(), frequency);
        if (result.ec != std::errc() || result.ptr != number.data() + number.size()) {
            error = "dictionary line with an unparsable frequency";
            return false;
        }
        out.push_back(Word{text, frequency});
    }
    return true;
}

}  // namespace

std::string SuggestWord::to_string() const {
    // SuggestWord.toString(). The floats go through Java's own formatting:
    // std::to_chars gives the same digits but writes 1 where Java writes
    // 1.0, and a host diffing the two would see every such value as a
    // difference.
    const auto number = dict::java_float_to_string;
    return to_utf8(str) + ",frequency:" + std::to_string(frequency) +
           ",frequencyScore:" + number(frequency_score) +
           ",levenshteinD:" + std::to_string(levenshtein_d) +
           ",levenshteinScore:" + number(levenshtein_score) +
           ",jaroWinklerScore:" + number(jaro_winkler_score) + ",score:" + number(score);
}

struct Dictionary::Impl {
    dict::WordStore words;  // sorted, no duplicates
    Index index = Index::fast;
    dict::Trie trie;
    dict::ScanIndex scan;

    void search(std::u32string_view query, dict::Trie::Result& out) const {
        if (index == Index::fast) {
            trie.search(query, out);
        } else {
            scan.search(query, out);
        }
    }

    // Built on first use: only the reference path needs them, and building
    // 38,743 words into one tree per length is not something a host should
    // pay for when nothing asks.
    mutable std::once_flag bk_once;
    mutable std::map<std::size_t, dict::BkTree> bk_by_length;

    void build_bk() const {
        for (std::uint32_t i = 0; i < words.size(); ++i) {
            const std::size_t len = words.length(i);
            auto it = bk_by_length.find(len);
            if (it == bk_by_length.end()) {
                it = bk_by_length
                         .emplace(len, dict::BkTree(ZString(kRootString,
                                                            std::size(kRootString))))
                         .first;
            }
            it->second.add(i, ZString(words.text(i)));
        }
    }
};

Dictionary::Dictionary(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Dictionary::Dictionary(Dictionary&&) noexcept = default;
Dictionary& Dictionary::operator=(Dictionary&&) noexcept = default;
Dictionary::~Dictionary() = default;

std::size_t Dictionary::size() const { return impl_->words.size(); }

Dictionary Dictionary::from_words(std::vector<Word> words, Index index) {
    std::sort(words.begin(), words.end(),
              [](const Word& a, const Word& b) { return a.str < b.str; });
    words.erase(std::unique(words.begin(), words.end(),
                            [](const Word& a, const Word& b) { return a.str == b.str; }),
                words.end());
    auto impl = std::make_unique<Impl>();
    impl->words.assign(words);
    words.clear();
    words.shrink_to_fit();
    impl->index = index;
    if (index == Index::fast) {
        impl->trie = dict::Trie::build(impl->words);
    } else {
        impl->scan = dict::ScanIndex::build(impl->words);
    }
    return Dictionary(std::move(impl));
}

std::optional<Dictionary> Dictionary::from_tsv(std::string_view tsv, Index index,
                                               std::string* error) {
    std::vector<Word> words;
    std::string message;
    if (!parse_word_list(tsv, words, message)) {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    }
    return from_words(std::move(words), index);
}

std::optional<Dictionary> Dictionary::create(std::string* error) {
    return from_tsv(dict::embedded_word_list(), Index::fast, error);
}

std::optional<Dictionary> Dictionary::from_file(const std::string& path, Index index,
                                                std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr) {
            *error = "cannot open " + path;
        }
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    if (!in.eof() && in.bad()) {
        if (error != nullptr) {
            *error = "cannot read " + path;
        }
        return std::nullopt;
    }
    return from_tsv(text, index, error);
}

std::vector<SuggestWord> Dictionary::match(std::u32string_view spliced) const {
    if (spliced.empty()) {
        return {};
    }
    dict::Trie::Result found;
    impl_->search(spliced, found);

    dict::ScoreMarker marker(spliced);
    marker.reserve(found.words.size());
    marker.observe_frequency(found.max_frequency);
    marker.observe_length(found.max_length);
    // The index reports the hits of the one subtree that can survive the
    // filter; the maxima above already cover the rest.
    for (std::uint32_t index : found.words) {
        SuggestWord sw;
        sw.str = ZString(impl_->words.text(index));
        sw.length = static_cast<int>(sw.str.size());
        sw.frequency = impl_->words.frequency(index);
        sw.levenshtein_d = sw.length - static_cast<int>(spliced.size());
        marker.add(std::move(sw));
    }
    return marker.mark_and_filter();
}

std::vector<SuggestWord> Dictionary::match_bk_reference(std::u32string_view spliced) const {
    if (spliced.empty()) {
        return {};
    }
    std::call_once(impl_->bk_once, [this] { impl_->build_bk(); });

    const std::size_t len = spliced.size();
    dict::ScoreMarker marker(spliced);
    std::vector<dict::BkTree::Hit> hits;
    // lengthKeyBkMap is a HashMap<Integer, ...> of small keys, so the
    // original iterates it in ascending length; std::map does the same.
    for (const auto& [key_length, tree] : impl_->bk_by_length) {
        if (key_length < len || key_length >= len * 2) {
            continue;
        }
        hits.clear();
        tree.match(spliced, static_cast<int>(key_length - len), hits);
        for (const dict::BkTree::Hit& hit : hits) {
            SuggestWord sw;
            sw.str = ZString(impl_->words.text(hit.word));
            sw.length = static_cast<int>(sw.str.size());
            sw.frequency = impl_->words.frequency(hit.word);
            sw.levenshtein_d = hit.distance;
            marker.observe_frequency(sw.frequency);
            marker.observe_length(sw.length);
            marker.add(std::move(sw));
        }
    }
    return marker.mark_and_filter();
}

}  // namespace zcode
