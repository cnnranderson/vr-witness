#pragma once
#include <cstdint>
#include <string_view>

namespace witness::input {
inline constexpr std::uint32_t default_stick_speed_percent = 20;

// Parse 10..300 percent of view width per second; leave result unchanged on failure.
inline bool parse_cursor_speed(std::wstring_view text, std::uint32_t& result) {
    while (!text.empty() && (text.front() == L' ' || text.front() == L'\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t'))
        text.remove_suffix(1);
    if (text.empty())
        return false;
    std::uint32_t value = 0;
    for (const auto c : text) {
        if (c < L'0' || c > L'9')
            return false;
        value = value * 10 + static_cast<unsigned>(c - L'0');
        if (value > 300)
            return false;
    }
    if (value < 10)
        return false;
    result = value;
    return true;
}
} // namespace witness::input
