#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "terminal_state.h"
#include "test_support.h"

namespace {

class TerminalStateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        data = std::make_shared<imterm::TerminalData>();
        state = std::make_unique<imterm::TerminalState>(
            data, imterm::TerminalState::NewLineMode::Strict);
        state->SetBounds(Coordinates(2, 79));
    }

    std::shared_ptr<imterm::TerminalData> data;
    std::unique_ptr<imterm::TerminalState> state;
};

TEST_F(TerminalStateTest, AppendsOrdinaryText)
{
    EXPECT_EQ(state->Input(imterm::test::Bytes("hello")), 0);

    ASSERT_EQ(data->GetLineCount(), 1U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "hello");
    EXPECT_EQ(state->getPosition(), Coordinates(0, 5));
}

TEST_F(TerminalStateTest, StrictNewlinePreservesTheColumn)
{
    state->Input(imterm::test::Bytes("A\nB"));

    ASSERT_EQ(data->GetLineCount(), 2U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "A");
    EXPECT_EQ(imterm::test::LineText(data->GetLine(1)), " B");
}

TEST_F(TerminalStateTest, AddCrToLfResetsTheColumn)
{
    state->SetNewLineMode(imterm::TerminalState::NewLineMode::AddCrToLf);

    state->Input(imterm::test::Bytes("A\nB"));

    ASSERT_EQ(data->GetLineCount(), 2U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "A");
    EXPECT_EQ(imterm::test::LineText(data->GetLine(1)), "B");
}

TEST_F(TerminalStateTest, AddLfToCrAdvancesToANewLine)
{
    state->SetNewLineMode(imterm::TerminalState::NewLineMode::AddLfToCr);

    state->Input(imterm::test::Bytes("A\rB"));

    ASSERT_EQ(data->GetLineCount(), 2U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "A");
    EXPECT_EQ(imterm::test::LineText(data->GetLine(1)), "B");
}

TEST_F(TerminalStateTest, RetainsEscapeParserStateAcrossInputChunks)
{
    state->Input(imterm::test::Bytes("\x1B[3"));
    state->Input(imterm::test::Bytes("1mR"));

    ASSERT_EQ(data->GetLine(0).size(), 1U);
    EXPECT_EQ(data->GetLine(0)[0].mChar, 'R');
    EXPECT_EQ(data->GetLine(0)[0].mColorIndex, imterm::PaletteIndex::Red);
}

TEST_F(TerminalStateTest, QueuesDeviceStatusResponse)
{
    state->Input(imterm::test::Bytes("\x1B[5n"));

    ASSERT_TRUE(state->TerminalOutputAvailable());
    EXPECT_EQ(state->GetTerminalOutput(), imterm::test::Bytes("\x1B[0n"));
    EXPECT_FALSE(state->TerminalOutputAvailable());
}

TEST_F(TerminalStateTest, MovesCursorAndPadsBeforeWriting)
{
    state->Input(imterm::test::Bytes("\x1B[3CX"));

    ASSERT_EQ(data->GetLine(0).size(), 4U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "   X");
    EXPECT_EQ(state->getPosition(), Coordinates(0, 4));
}

TEST_F(TerminalStateTest, ErasesLineAfterCursor)
{
    state->Input(imterm::test::Bytes("abc\x1B[2D\x1B[K"));

    ASSERT_EQ(data->GetLineCount(), 1U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "a");
    EXPECT_EQ(state->getPosition(), Coordinates(0, 1));
}

TEST_F(TerminalStateTest, PreservesScrollbackWhenTheViewportFills)
{
    state->SetBounds(Coordinates(1, 79));
    state->SetNewLineMode(imterm::TerminalState::NewLineMode::AddCrToLf);

    EXPECT_EQ(state->Input(imterm::test::Bytes("0\n1\n2")), 2);

    ASSERT_EQ(data->GetLineCount(), 3U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "0");
    EXPECT_EQ(imterm::test::LineText(data->GetLine(1)), "1");
    EXPECT_EQ(imterm::test::LineText(data->GetLine(2)), "2");
    EXPECT_EQ(state->getPositionRelative(data->GetLineCount()), Coordinates(2, 1));
}

TEST_F(TerminalStateTest, BuffersEveryTruncatedUtf8Prefix)
{
    const std::vector<std::vector<uint8_t>> sequences = {
        {0xC2, 0xA2},
        {0xE2, 0x82, 0xAC},
        {0xF0, 0x9F, 0x98, 0x80},
        {0xF8, 0x80, 0x80, 0x80, 0x80},
        {0xFC, 0x80, 0x80, 0x80, 0x80, 0x80},
    };

    for (const auto& sequence : sequences) {
        for (size_t split = 1; split < sequence.size(); ++split) {
            data = std::make_shared<imterm::TerminalData>();
            state = std::make_unique<imterm::TerminalState>(
                data, imterm::TerminalState::NewLineMode::Strict);
            state->SetBounds(Coordinates(2, 79));

            state->Input(std::span(sequence).first(split));
            EXPECT_TRUE(data->GetLine(0).empty()) << "split=" << split;

            state->Input(std::span(sequence).subspan(split));
            EXPECT_EQ(imterm::test::LineText(data->GetLine(0)),
                std::string(sequence.begin(), sequence.end())) << "split=" << split;
        }
    }
}

TEST_F(TerminalStateTest, IgnoresUnknownSgrAndCsiCommands)
{
    EXPECT_NO_THROW(state->Input(imterm::test::Bytes(
        "\x1B[999mA\x1B[12zB\x1B[31mR")));

    ASSERT_EQ(data->GetLine(0).size(), 3U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "ABR");
    EXPECT_EQ(data->GetLine(0).back().mColorIndex, imterm::PaletteIndex::Red);
}

TEST_F(TerminalStateTest, ClampsLargeCursorMovementsInEveryDirection)
{
    state->SetBounds(Coordinates(4, 9));

    state->Input(imterm::test::Bytes("\x1B[65535C\x1B[65535B"));
    EXPECT_EQ(state->getPosition(), Coordinates(4, 9));

    state->Input(imterm::test::Bytes("\x1B[65535D\x1B[65535A"));
    EXPECT_EQ(state->getPosition(), Coordinates(0, 0));

    state->Input(imterm::test::Bytes("\x1B[65535E"));
    EXPECT_EQ(state->getPosition(), Coordinates(4, 0));
    state->Input(imterm::test::Bytes("\x1B[65535F"));
    EXPECT_EQ(state->getPosition(), Coordinates(0, 0));
}

TEST_F(TerminalStateTest, MalformedSequenceRecoversAtEveryChunkBoundary)
{
    const auto malformed = imterm::test::Bytes("A\x1B[12!B");

    for (size_t split = 0; split <= malformed.size(); ++split) {
        data = std::make_shared<imterm::TerminalData>();
        state = std::make_unique<imterm::TerminalState>(
            data, imterm::TerminalState::NewLineMode::Strict);
        state->SetBounds(Coordinates(2, 79));

        state->Input(std::span(malformed).first(split));
        state->Input(std::span(malformed).subspan(split));

        EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "AB")
            << "split=" << split;
    }
}

TEST_F(TerminalStateTest, EmbeddedNulDoesNotTerminateOrHideFollowingBytes)
{
    state->Input(imterm::test::Bytes({'A', 0, 'B'}));

    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "AB");
}

TEST_F(TerminalStateTest, OversizedCsiArgumentRecoversForFollowingText)
{
    state->Input(imterm::test::Bytes("A\x1B[999999999mZ"));

    const std::string text = imterm::test::LineText(data->GetLine(0));
    EXPECT_EQ(text.front(), 'A');
    EXPECT_EQ(text.back(), 'Z');
}

TEST_F(TerminalStateTest, ErasingBeyondAShortLineUsesValidatedIterators)
{
    state->Input(imterm::test::Bytes("x\x1B[10C\x1B[1KZ"));

    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)),
        "           Z");
}

TEST_F(TerminalStateTest, DeterministicRandomInputDoesNotCrashOrGrowWithoutBound)
{
    state->SetBounds(Coordinates(4, 79));
    std::mt19937 generator(0x1A2B3C4D);
    std::uniform_int_distribution<int> distribution(0, 255);
    std::vector<uint8_t> bytes(100000);
    size_t newlineCount = 0;
    for (uint8_t& byte : bytes) {
        byte = static_cast<uint8_t>(distribution(generator));
        if (byte == '\a') {
            byte = 0;
        }
        newlineCount += byte == '\n' ? 1U : 0U;
    }

    EXPECT_NO_THROW(state->Input(bytes));
    EXPECT_LE(data->GetLineCount(), newlineCount + 5);
}

} // namespace
