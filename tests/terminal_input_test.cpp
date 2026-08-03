#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <utility>

#include "terminal_input.h"

namespace {

TEST(TerminalInputTest, EncodesSpecialKeys)
{
    using imterm::TerminalKey;
    const std::array cases{
        std::pair{TerminalKey::Backspace, std::string_view("\x7F")},
        std::pair{TerminalKey::Enter, std::string_view("\n")},
        std::pair{TerminalKey::Tab, std::string_view("\t")},
        std::pair{TerminalKey::Up, std::string_view("\x1B[A")},
        std::pair{TerminalKey::Down, std::string_view("\x1B[B")},
        std::pair{TerminalKey::Right, std::string_view("\x1B[C")},
        std::pair{TerminalKey::Left, std::string_view("\x1B[D")},
        std::pair{TerminalKey::Home, std::string_view("\x1B[1~")},
        std::pair{TerminalKey::Delete, std::string_view("\x1B[3~")},
        std::pair{TerminalKey::End, std::string_view("\x1B[4~")},
    };

    for (const auto& [key, expected] : cases) {
        EXPECT_EQ(imterm::GetTerminalKeySequence(key), expected);
    }
}

TEST(TerminalInputTest, EncodesAlphabeticControlCharacters)
{
    ASSERT_TRUE(imterm::GetControlCharacter('a').has_value());
    ASSERT_TRUE(imterm::GetControlCharacter('Z').has_value());
    EXPECT_EQ(*imterm::GetControlCharacter('a'), 0x01);
    EXPECT_EQ(*imterm::GetControlCharacter('c'), 0x03);
    EXPECT_EQ(*imterm::GetControlCharacter('Z'), 0x1A);
    EXPECT_FALSE(imterm::GetControlCharacter('1').has_value());
}

} // namespace
