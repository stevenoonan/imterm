#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace imterm::utf8 {

inline constexpr std::string_view Replacement = "\xEF\xBF\xBD";
enum class Status { Valid, Invalid, Incomplete };
struct Decoded {
    Status mStatus;
    uint32_t mCodePoint;
    size_t mLength;
};

// Validate modern UTF-8, including shortest forms and the Unicode scalar range.
// Invalid input consumes one byte; incomplete means a still-valid prefix.
template<class ByteAt>
Decoded Decode(size_t size, ByteAt byteAt)
{
    if (size == 0) return {Status::Incomplete, 0, 0};
    const uint8_t first = byteAt(0);
    if (first < 0x80) return {Status::Valid, first, 1};
    const size_t length = first >= 0xC2 && first <= 0xDF ? 2
        : first >= 0xE0 && first <= 0xEF ? 3
        : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    if (length == 0) return {Status::Invalid, 0xFFFD, 1};
    uint32_t codePoint = first & (0x7F >> length);
    for (size_t i = 1; i < length; ++i) {
        if (i == size) return {Status::Incomplete, 0, size};
        const uint8_t next = byteAt(i);
        if (next < 0x80 || next > 0xBF
            || (i == 1 && ((first == 0xE0 && next < 0xA0)
                || (first == 0xED && next > 0x9F)
                || (first == 0xF0 && next < 0x90)
                || (first == 0xF4 && next > 0x8F)))) {
            return {Status::Invalid, 0xFFFD, 1};
        }
        codePoint = (codePoint << 6) | (next & 0x3F);
    }
    return {Status::Valid, codePoint, length};
}

inline Decoded Decode(std::span<const uint8_t> bytes)
{
    return Decode(bytes.size(), [bytes](size_t i) { return bytes[i]; });
}

inline Decoded Decode(std::string_view text)
{
    return Decode(text.size(), [text](size_t i) { return static_cast<uint8_t>(text[i]); });
}

inline std::string Encode(uint32_t codePoint)
{
    if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
        return std::string(Replacement);
    std::string result;
    if (codePoint < 0x80) result += static_cast<char>(codePoint);
    else {
        if (codePoint >= 0x10000) result += static_cast<char>(0xF0 | (codePoint >> 18));
        else if (codePoint >= 0x800) result += static_cast<char>(0xE0 | (codePoint >> 12));
        else result += static_cast<char>(0xC0 | (codePoint >> 6));
        if (codePoint >= 0x10000) result += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        if (codePoint >= 0x800) result += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    return result;
}

inline std::string Sanitize(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    while (!text.empty()) {
        const auto decoded = Decode(text);
        const bool valid = decoded.mStatus == Status::Valid;
        result.append(valid ? text.substr(0, decoded.mLength) : Replacement);
        text.remove_prefix(valid ? decoded.mLength : 1);
    }
    return result;
}

} // namespace imterm::utf8
