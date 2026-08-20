#include <gtest/gtest.h>

#include <string_view>
#include <variant>

#include "terminal_command.h"

namespace {

EscapeSequenceParser::ParseResult ParseSequence(std::string_view aSequence)
{
    EscapeSequenceParser parser;
    EscapeSequenceParser::ParseResult result{};
    for (const unsigned char byte : aSequence) {
        result = parser.Parse(byte);
    }
    return result;
}

TEST(TerminalCommandTest, ConvertsCupAndHvpFromOneBasedCoordinates)
{
    for (const std::string_view sequence : {"\x1B[2;3H", "\x1B[2;3f"}) {
        const auto command = imterm::DecodeTerminalCommand(
            ParseSequence(sequence));
        ASSERT_TRUE(command.has_value());
        ASSERT_TRUE(std::holds_alternative<imterm::SetCursorPosition>(*command));
        const auto position = std::get<imterm::SetCursorPosition>(*command);
        EXPECT_EQ(position.mPosition.mRow, 1);
        EXPECT_EQ(position.mPosition.mColumn, 2);
    }
}

TEST(TerminalCommandTest, PreservesEmptyParametersForAnsiDefaults)
{
    const auto rowOnly = imterm::DecodeTerminalCommand(
        ParseSequence("\x1B[2;H"));
    const auto columnOnly = imterm::DecodeTerminalCommand(
        ParseSequence("\x1B[;3H"));
    ASSERT_TRUE(rowOnly.has_value());
    ASSERT_TRUE(columnOnly.has_value());

    const auto rowPosition = std::get<imterm::SetCursorPosition>(*rowOnly);
    EXPECT_EQ(rowPosition.mPosition.mRow, 1);
    EXPECT_EQ(rowPosition.mPosition.mColumn, 0);
    const auto columnPosition = std::get<imterm::SetCursorPosition>(*columnOnly);
    EXPECT_EQ(columnPosition.mPosition.mRow, 0);
    EXPECT_EQ(columnPosition.mPosition.mColumn, 2);
}

TEST(TerminalCommandTest, UsesOneAsTheDefaultMovementAmount)
{
    for (const std::string_view sequence : {"\x1B[A", "\x1B[0A"}) {
        const auto command = imterm::DecodeTerminalCommand(
            ParseSequence(sequence));
        ASSERT_TRUE(command.has_value());
        ASSERT_TRUE(std::holds_alternative<imterm::MoveCursor>(*command));
        EXPECT_EQ(std::get<imterm::MoveCursor>(*command).mAmount, 1);
    }
}

TEST(TerminalCommandTest, IgnoresPrivateScreenAndUnknownCommands)
{
    for (const std::string_view sequence : {
             "\x1B[?25h", "\x1B[=1h", "\x1B[12z"}) {
        EXPECT_FALSE(imterm::DecodeTerminalCommand(
            ParseSequence(sequence)).has_value());
    }
}

} // namespace
