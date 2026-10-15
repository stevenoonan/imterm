#include <gtest/gtest.h>

#include "utf8.h"

namespace {
using namespace imterm::utf8;

TEST(Utf8Test, DecodesModernUnicodeBoundaries)
{
    struct Case { uint32_t codePoint; std::string text; };
    const Case cases[] = {
        {0, std::string(1, '\0')}, {0x7F, "\x7F"},
        {0x80, "\xC2\x80"}, {0x7FF, "\xDF\xBF"},
        {0x800, "\xE0\xA0\x80"}, {0xD7FF, "\xED\x9F\xBF"},
        {0xE000, "\xEE\x80\x80"}, {0xFFFF, "\xEF\xBF\xBF"},
        {0x10000, "\xF0\x90\x80\x80"}, {0x10FFFF, "\xF4\x8F\xBF\xBF"},
    };
    for (const auto& [codePoint, text] : cases) {
        const auto decoded = Decode(std::string_view(text));
        EXPECT_EQ(decoded.mStatus, Status::Valid);
        EXPECT_EQ(decoded.mCodePoint, codePoint);
        EXPECT_EQ(decoded.mLength, text.size());
        EXPECT_EQ(Encode(codePoint), text);
        EXPECT_EQ(Sanitize(text), text);
    }
}

TEST(Utf8Test, RejectsOverlongSurrogateOutOfRangeAndObsoleteSequences)
{
    const std::string cases[] = {
        "\x80", "\xBF", "\xC0\xAF", "\xC1\xBF",
        "\xE0\x80\x80", "\xED\xA0\x80", "\xED\xBF\xBF",
        "\xF0\x80\x80\x80", "\xF4\x90\x80\x80",
        "\xF5\x80\x80\x80", "\xF8\x80\x80\x80\x80",
        "\xFC\x80\x80\x80\x80\x80", "\xFE", "\xFF",
    };
    for (const auto& text : cases) {
        EXPECT_EQ(Decode(std::string_view(text)).mStatus, Status::Invalid);
        std::string expected;
        for (size_t i = 0; i < text.size(); ++i) expected += Replacement;
        EXPECT_EQ(Sanitize(text), expected);
    }
    for (uint32_t codePoint : {0xD800U, 0xDFFFU, 0x110000U, 0xFFFFFFFFU})
        EXPECT_EQ(Encode(codePoint), Replacement);
}

TEST(Utf8Test, OnlyBuffersPrefixesThatCouldStillBecomeValid)
{
    for (const std::string text : {"\xC2\xA2", "\xE6\xBC\xA2", "\xF0\x9F\x98\x80"}) {
        for (size_t size = 0; size < text.size(); ++size) {
            EXPECT_EQ(Decode(std::string_view(text).substr(0, size)).mStatus, Status::Incomplete);
        }
    }
    EXPECT_EQ(Decode(std::string_view("\xE0\x80")).mStatus, Status::Invalid);
    EXPECT_EQ(Decode(std::string_view("\xF4\x90")).mStatus, Status::Invalid);
    EXPECT_EQ(Sanitize("\xE2\x82"), std::string(Replacement) + std::string(Replacement));
    EXPECT_EQ(Sanitize("\xE2\x82!"), std::string(Replacement) + std::string(Replacement) + "!");
}
}
