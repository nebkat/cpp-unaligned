#pragma once

// Shared test helpers: bytes <-> hex text, so that a byte layout reads as a string in a failure.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cstddef>

/** Renders bytes as "01 02 ff". */
inline std::string hex(std::span<const std::byte> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    for (const std::byte b : bytes) {
        if (!text.empty()) text += ' ';
        text += digits[std::to_integer<int>(b) >> 4];
        text += digits[std::to_integer<int>(b) & 0xF];
    }
    return text;
}

/** Decodes "01 02 ff"; anything that is not a hex digit separates bytes. */
inline std::vector<std::byte> from_hex(std::string_view text) {
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::byte> result;
    int high = -1;
    for (const char c : text) {
        const int value = nibble(c);
        if (value < 0) continue;
        if (high < 0) {
            high = value;
        } else {
            result.push_back(static_cast<std::byte>((high << 4) | value));
            high = -1;
        }
    }
    return result;
}
