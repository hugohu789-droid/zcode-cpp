// SPDX-License-Identifier: GPL-2.0-or-later
//
// Helpers for reading the golden TSV files written by tools/golden-java.
// The formats are documented at the top of GoldenDump.java.
#pragma once

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "zcode/zcode.h"

namespace zcode::test {

inline std::string testdata_path(std::string_view name) {
    return std::string(ZCODE_TESTDATA_DIR) + "/" + std::string(name);
}

using Row = std::vector<std::string>;

// Reads a golden file; skips '#' comments; keeps empty trailing fields.
inline std::vector<Row> read_tsv(std::string_view name) {
    std::ifstream in(testdata_path(name), std::ios::binary);
    std::vector<Row> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        Row row;
        std::size_t start = 0;
        while (true) {
            std::size_t tab = line.find('\t', start);
            row.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

inline std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::size_t start = 0;
    while (true) {
        std::size_t p = s.find(sep, start);
        out.emplace_back(s.substr(start, p == std::string_view::npos ? std::string_view::npos : p - start));
        if (p == std::string_view::npos) break;
        start = p + 1;
    }
    return out;
}

// "1865.186A" for a ZCode string, exactly as GoldenDump.hex().
inline std::string hex(std::u32string_view z) {
    std::string out;
    char buf[16];
    for (std::size_t i = 0; i < z.size(); ++i) {
        if (i > 0) out += '.';
        std::snprintf(buf, sizeof buf, "%04X", static_cast<unsigned>(z[i]));
        out += buf;
    }
    return out;
}

inline std::u32string unhex(std::string_view s) {
    std::u32string out;
    for (const std::string& part : split(s, '.')) {
        out.push_back(static_cast<char32_t>(std::stoul(part, nullptr, 16)));
    }
    return out;
}

inline std::string fnv1a64(std::string_view ascii) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (char c : ascii) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ull;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llX", static_cast<unsigned long long>(h));
    return buf;
}

// GoldenDump.candidates(): candidates joined by ' ', or "H:<fnv>" when long.
inline std::string candidates(const std::vector<std::u32string>& zs) {
    std::string full;
    for (std::size_t i = 0; i < zs.size(); ++i) {
        if (i > 0) full += ' ';
        full += hex(zs[i]);
    }
    return zs.size() > 2000 ? "H:" + fnv1a64(full) : full;
}

// Undoes GoldenDump.escape() into the UTF-16 string Java saw, then encodes it
// as UTF-8 (lone surrogates cannot occur in the inputs and become U+FFFD).
inline std::string unescape_to_utf8(std::string_view s) {
    std::u16string u;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c != '\\') {
            u.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
            continue;
        }
        char e = s[++i];
        switch (e) {
            case '\\': u.push_back(u'\\'); break;
            case 't': u.push_back(u'\t'); break;
            case 'n': u.push_back(u'\n'); break;
            case 'r': u.push_back(u'\r'); break;
            case 'u':
                u.push_back(static_cast<char16_t>(std::stoul(std::string(s.substr(i + 1, 4)), nullptr, 16)));
                i += 4;
                break;
            default: u.push_back(static_cast<char16_t>(e)); break;
        }
    }
    std::u32string cps;
    for (std::size_t i = 0; i < u.size(); ++i) {
        char32_t c = u[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (u[i + 1] - 0xDC00);
            ++i;
        }
        cps.push_back(c);
    }
    return to_utf8(cps);
}

}  // namespace zcode::test
