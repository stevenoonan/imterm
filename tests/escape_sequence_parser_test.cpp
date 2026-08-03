#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "escape_sequence_parser.h"

namespace {

using CommandData = std::vector<int>;

TEST(EscapeSequenceParserTest, OrdinaryBytesPassThrough)
{
    EscapeSequenceParser parser;

    const auto& result = parser.Parse('x');

    EXPECT_EQ(result.mStage, EscapeSequenceParser::Stage::Inactive);
    EXPECT_EQ(result.mError, EscapeSequenceParser::Error::BadEsc);
    EXPECT_EQ(result.mOutputChar, 'x');
}

TEST(EscapeSequenceParserTest, ParsesSequenceAcrossCalls)
{
    EscapeSequenceParser parser;

    EXPECT_EQ(parser.Parse(0x1B).mStage, EscapeSequenceParser::Stage::GetCsi);
    EXPECT_EQ(parser.Parse('[').mStage, EscapeSequenceParser::Stage::GetMode);
    EXPECT_EQ(parser.Parse('3').mStage, EscapeSequenceParser::Stage::GetData);
    EXPECT_EQ(parser.Parse('1').mStage, EscapeSequenceParser::Stage::GetData);

    const auto& result = parser.Parse('m');

    EXPECT_EQ(result.mStage, EscapeSequenceParser::Stage::Inactive);
    EXPECT_EQ(result.mError, EscapeSequenceParser::Error::None);
    EXPECT_EQ(result.mIdentifier,
        EscapeSequenceParser::EscapeIdentifier::m_SetGraphics);
    EXPECT_EQ(result.mMode, EscapeSequenceParser::Mode::None);
    EXPECT_EQ(result.mCommandData, CommandData({31}));
}

TEST(EscapeSequenceParserTest, ParsesPrivateModeAndDelimitedArguments)
{
    EscapeSequenceParser parser;

    for (const uint8_t byte : std::vector<uint8_t>{0x1B, '[', '?', '1', ';', '2'}) {
        parser.Parse(byte);
    }
    const auto& result = parser.Parse('h');

    EXPECT_EQ(result.mError, EscapeSequenceParser::Error::None);
    EXPECT_EQ(result.mIdentifier,
        EscapeSequenceParser::EscapeIdentifier::h_Mode);
    EXPECT_EQ(result.mMode, EscapeSequenceParser::Mode::Private);
    EXPECT_EQ(result.mCommandData, CommandData({1, 2}));
}

TEST(EscapeSequenceParserTest, ReportsMalformedCsi)
{
    EscapeSequenceParser parser;

    parser.Parse(0x1B);
    const auto& result = parser.Parse('X');

    EXPECT_EQ(result.mStage, EscapeSequenceParser::Stage::Inactive);
    EXPECT_EQ(result.mError, EscapeSequenceParser::Error::BadCsi);
    EXPECT_EQ(result.mOutputChar, 0);
}

TEST(EscapeSequenceParserTest, RejectsAnArgumentWithTooManyDigits)
{
    EscapeSequenceParser parser;

    for (const uint8_t byte : std::vector<uint8_t>{0x1B, '[', '0', '0', '0', '0', '0', '0'}) {
        parser.Parse(byte);
    }
    const auto& result = parser.Parse('0');

    EXPECT_EQ(result.mStage, EscapeSequenceParser::Stage::Inactive);
    EXPECT_EQ(result.mError, EscapeSequenceParser::Error::ArgumentTooLong);
}

TEST(EscapeSequenceParserTest, RejectsAnArgumentOutsideTheNumericRange)
{
    EscapeSequenceParser parser;

    for (const uint8_t byte : std::vector<uint8_t>{0x1B, '[', '9', '9', '9', '9'}) {
        parser.Parse(byte);
    }
    const auto& result = parser.Parse('9');

    EXPECT_EQ(result.mStage, EscapeSequenceParser::Stage::Inactive);
    EXPECT_EQ(result.mError, EscapeSequenceParser::Error::NumericOverflow);
}

TEST(EscapeSequenceParserTest, RejectsTooManyArguments)
{
    EscapeSequenceParser parser;
    parser.Parse(0x1B);
    parser.Parse('[');

    for (size_t index = 0; index < EscapeSequenceParser::MaxArguments; ++index) {
        parser.Parse('1');
        parser.Parse(';');
    }
    parser.Parse('1');
    const auto& result = parser.Parse('m');

    EXPECT_EQ(result.mStage, EscapeSequenceParser::Stage::Inactive);
    EXPECT_EQ(result.mError, EscapeSequenceParser::Error::TooManyArguments);
}

TEST(EscapeSequenceParserTest, LimitsTotalSequenceLengthAndRecovers)
{
    EscapeSequenceParser parser;
    parser.Parse(0x1B);
    parser.Parse('[');

    EscapeSequenceParser::Error error = EscapeSequenceParser::Error::NotReady;
    for (size_t index = 0; index < EscapeSequenceParser::MaxSequenceLength; ++index) {
        error = parser.Parse(';').mError;
        if (error == EscapeSequenceParser::Error::SequenceTooLong) {
            break;
        }
    }

    EXPECT_EQ(error, EscapeSequenceParser::Error::SequenceTooLong);
    const auto& recovered = parser.Parse('x');
    EXPECT_EQ(recovered.mOutputChar, 'x');
    EXPECT_EQ(recovered.mStage, EscapeSequenceParser::Stage::Inactive);
}

TEST(EscapeSequenceParserTest, ANewEscapeRestartsAMalformedPartialSequence)
{
    EscapeSequenceParser parser;
    for (const uint8_t byte : std::vector<uint8_t>{0x1B, '[', '1', '2', 0x1B, '[', '3', '1'}) {
        parser.Parse(byte);
    }

    const auto& result = parser.Parse('m');

    EXPECT_EQ(result.mError, EscapeSequenceParser::Error::None);
    EXPECT_EQ(result.mCommandData, CommandData({31}));
}

} // namespace
