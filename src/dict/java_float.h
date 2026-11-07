// SPDX-License-Identifier: GPL-2.0-or-later
//
// java.lang.Float.toString(), because SuggestWord.toString() goes over the
// wire and a host comparing this engine against the original would otherwise
// see "1" where Java writes "1.0".
//
// Java's rule, from the Float.toString contract: print the shortest decimal
// that tells the value apart from its neighbours, with at least one digit
// after the point; use plain notation when 10^-3 <= |value| < 10^7 and
// scientific notation otherwise, with 'E', no '+' and no padded exponent.
#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace zcode::dict {

inline std::string java_float_to_string(float value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-Infinity" : "Infinity";
    }
    if (value == 0.0F) {
        return std::signbit(value) ? "-0.0" : "0.0";
    }

    const bool negative = value < 0;
    const float magnitude = negative ? -value : value;

    // Shortest round-trip digits, as one digit, a point and the rest.
    char buffer[64];
    const auto result =
        std::to_chars(buffer, buffer + sizeof buffer, magnitude, std::chars_format::scientific);
    const std::string_view text(buffer, static_cast<std::size_t>(result.ptr - buffer));
    const std::size_t e = text.find('e');
    std::string digits;
    for (const char c : text.substr(0, e)) {
        if (c != '.') {
            digits.push_back(c);
        }
    }
    // value == <digits[0]>.<rest> * 10^exponent. std::from_chars rejects a
    // leading '+', which to_chars writes, so step over it.
    std::size_t at = e + 1;
    bool exponent_negative = false;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        exponent_negative = text[at] == '-';
        ++at;
    }
    int exponent = 0;
    std::from_chars(text.data() + at, text.data() + text.size(), exponent);
    if (exponent_negative) {
        exponent = -exponent;
    }

    std::string out;
    if (negative) {
        out.push_back('-');
    }
    if (exponent >= -3 && exponent < 7) {
        if (exponent >= 0) {
            const auto whole = static_cast<std::size_t>(exponent) + 1;
            if (digits.size() <= whole) {
                out += digits;
                out.append(whole - digits.size(), '0');
                out += ".0";
            } else {
                out += digits.substr(0, whole);
                out.push_back('.');
                out += digits.substr(whole);
            }
        } else {
            out += "0.";
            out.append(static_cast<std::size_t>(-exponent) - 1, '0');
            out += digits;
        }
        return out;
    }
    out.push_back(digits[0]);
    out.push_back('.');
    out += digits.size() > 1 ? digits.substr(1) : "0";
    out.push_back('E');
    out += std::to_string(exponent);
    return out;
}

}  // namespace zcode::dict
