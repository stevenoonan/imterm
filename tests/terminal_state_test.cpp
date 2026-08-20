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
        state->SetViewportSize(3, 80);
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

TEST_F(TerminalStateTest, SupportedCursorCommandsWorkAcrossEveryInputSplit)
{
    struct CursorCase {
        std::string mSequence;
        Coordinates mExpected;
    };
    const std::vector<CursorCase> cases = {
        {"\x1B[H", Coordinates(0, 0)},
        {"\x1B[2;3H", Coordinates(1, 2)},
        {"\x1B[2;3f", Coordinates(1, 2)},
        {"\x1B[A", Coordinates(1, 3)},
        {"\x1B[2B", Coordinates(4, 3)},
        {"\x1B[C", Coordinates(2, 4)},
        {"\x1B[2D", Coordinates(2, 1)},
        {"\x1B[E", Coordinates(3, 0)},
        {"\x1B[F", Coordinates(1, 0)},
        {"\x1B[G", Coordinates(2, 0)},
        {"\x1B[s\x1B[1;1H\x1B[u", Coordinates(2, 3)},
    };

    for (const CursorCase& testCase : cases) {
        const auto sequence = imterm::test::Bytes(testCase.mSequence);
        for (size_t split = 0; split <= sequence.size(); ++split) {
            SCOPED_TRACE(testCase.mSequence + " split=" + std::to_string(split));
            data = std::make_shared<imterm::TerminalData>();
            state = std::make_unique<imterm::TerminalState>(
                data, imterm::TerminalState::NewLineMode::Strict);
            state->SetViewportSize(5, 10);
            state->Input(imterm::test::Bytes("\x1B[3;4H"));

            state->Input(std::span(sequence).first(split));
            state->Input(std::span(sequence).subspan(split));

            EXPECT_EQ(state->getPosition(), testCase.mExpected);
        }
    }
}

TEST_F(TerminalStateTest, ErasesLineAfterCursor)
{
    state->Input(imterm::test::Bytes("abc\x1B[2D\x1B[K"));

    ASSERT_EQ(data->GetLineCount(), 1U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "a");
    EXPECT_EQ(state->getPosition(), Coordinates(0, 1));
}

TEST_F(TerminalStateTest, LineEraseModesUseInclusiveCursorSemantics)
{
    struct EraseCase {
        std::string mSequence;
        std::string mExpected;
    };
    const std::vector<EraseCase> cases = {
        {"\x1B[K", "ab"},
        {"\x1B[0K", "ab"},
        {"\x1B[1K", "   def"},
        {"\x1B[2K", ""},
    };

    for (const EraseCase& testCase : cases) {
        const auto sequence = imterm::test::Bytes(testCase.mSequence);
        for (size_t split = 0; split <= sequence.size(); ++split) {
            SCOPED_TRACE(testCase.mSequence + " split=" + std::to_string(split));
            data->SetText("abcdef");
            state->Input(imterm::test::Bytes("\x1B[1;3H"));
            state->Input(std::span(sequence).first(split));
            state->Input(std::span(sequence).subspan(split));
            EXPECT_EQ(imterm::test::LineText(data->GetLine(0)),
                testCase.mExpected);
        }
    }
}

TEST_F(TerminalStateTest, DisplayEraseModesAffectOnlyTheViewport)
{
    struct EraseCase {
        std::string mSequence;
        std::vector<std::string> mExpected;
    };
    const std::vector<EraseCase> cases = {
        {"\x1B[J", {"abc", "d", ""}},
        {"\x1B[0J", {"abc", "d", ""}},
        {"\x1B[1J", {"", "  f", "ghi"}},
        {"\x1B[2J", {"", "", ""}},
    };

    for (const EraseCase& testCase : cases) {
        const auto sequence = imterm::test::Bytes(testCase.mSequence);
        for (size_t split = 0; split <= sequence.size(); ++split) {
            SCOPED_TRACE(testCase.mSequence + " split=" + std::to_string(split));
            data->SetTextLines({"abc", "def", "ghi"});
            state->Input(imterm::test::Bytes("\x1B[2;2H"));
            state->Input(std::span(sequence).first(split));
            state->Input(std::span(sequence).subspan(split));
            EXPECT_EQ(data->GetTextLines(), testCase.mExpected);
        }
    }
}

TEST_F(TerminalStateTest, EraseSavedLinesRemovesOnlyScrollback)
{
    state->SetViewportSize(2, 80);
    data->SetTextLines({"0", "1", "2", "3", "4"});

    state->Input(imterm::test::Bytes("\x1B[3J"));

    EXPECT_EQ(data->GetTextLines(), std::vector<std::string>({"3", "4"}));
}

TEST_F(TerminalStateTest, EraseUsesRenderedColumnsForTabsAndUtf8)
{
    data->SetText("\tAB");
    state->Input(imterm::test::Bytes("\x1B[3G\x1B[K"));
    EXPECT_TRUE(data->GetLine(0).empty());

    data->SetText("");
    state->Input(imterm::test::Bytes("\x1B[H"));
    state->Input(imterm::test::Bytes({0xE2, 0x82, 0xAC, 'A'}));
    EXPECT_EQ(state->getPosition(), Coordinates(0, 2));
    state->Input(imterm::test::Bytes("\x1B[D\x1B[K"));
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)),
        std::string("\xE2\x82\xAC"));

    data->SetText("");
    state->Input(imterm::test::Bytes("\x1B[H"));
    state->Input(imterm::test::Bytes({0xE2, 0x82, 0xAC, 'A'}));
    state->Input(imterm::test::Bytes("\x1B[2D\x1B[1K"));
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), " A");
}

TEST_F(TerminalStateTest, PreservesScrollbackWhenTheViewportFills)
{
    state->SetViewportSize(2, 80);
    state->SetNewLineMode(imterm::TerminalState::NewLineMode::AddCrToLf);

    EXPECT_EQ(state->Input(imterm::test::Bytes("0\n1\n2")), 2);

    ASSERT_EQ(data->GetLineCount(), 3U);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "0");
    EXPECT_EQ(imterm::test::LineText(data->GetLine(1)), "1");
    EXPECT_EQ(imterm::test::LineText(data->GetLine(2)), "2");
    EXPECT_EQ(state->getPositionRelative(data->GetLineCount()), Coordinates(2, 1));
}

TEST_F(TerminalStateTest, ResizePreservesTheCursorBufferRow)
{
    state->SetViewportSize(2, 80);
    state->SetNewLineMode(imterm::TerminalState::NewLineMode::AddCrToLf);
    state->Input(imterm::test::Bytes("0\n1\n2"));
    ASSERT_EQ(state->getPositionRelative(data->GetLineCount()),
        Coordinates(2, 1));

    state->SetViewportSize(3, 80);
    EXPECT_EQ(state->getPositionRelative(data->GetLineCount()),
        Coordinates(2, 1));
    state->SetViewportSize(2, 80);
    EXPECT_EQ(state->getPositionRelative(data->GetLineCount()),
        Coordinates(2, 1));
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
            state->SetViewportSize(3, 80);

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

TEST_F(TerminalStateTest, SgrFormattingAndResetsAreAppliedIndependently)
{
    const auto setSequence = imterm::test::Bytes(
        "\x1B[1;2;3;4;5;7;8;9m");
    const auto resetSequence = imterm::test::Bytes(
        "\x1B[22;23;24;25;27;28;29m");
    for (size_t split = 0; split <= setSequence.size(); ++split) {
        data = std::make_shared<imterm::TerminalData>();
        state = std::make_unique<imterm::TerminalState>(
            data, imterm::TerminalState::NewLineMode::Strict);
        state->SetViewportSize(3, 80);
        state->Input(std::span(setSequence).first(split));
        state->Input(std::span(setSequence).subspan(split));
        EXPECT_TRUE(state->IsBold()) << "split=" << split;
        EXPECT_TRUE(state->IsDim()) << "split=" << split;
        EXPECT_TRUE(state->IsItalic()) << "split=" << split;
        EXPECT_TRUE(state->IsUnderline()) << "split=" << split;
        EXPECT_TRUE(state->IsBlinking()) << "split=" << split;
        EXPECT_TRUE(state->IsInverse()) << "split=" << split;
        EXPECT_TRUE(state->IsHidden()) << "split=" << split;
        EXPECT_TRUE(state->IsStrikethrough()) << "split=" << split;

        state->Input(resetSequence);
        EXPECT_FALSE(state->IsBold()) << "split=" << split;
        EXPECT_FALSE(state->IsDim()) << "split=" << split;
        EXPECT_FALSE(state->IsItalic()) << "split=" << split;
        EXPECT_FALSE(state->IsUnderline()) << "split=" << split;
        EXPECT_FALSE(state->IsBlinking()) << "split=" << split;
        EXPECT_FALSE(state->IsInverse()) << "split=" << split;
        EXPECT_FALSE(state->IsHidden()) << "split=" << split;
        EXPECT_FALSE(state->IsStrikethrough()) << "split=" << split;
    }
}

TEST_F(TerminalStateTest, SupportedSgrColorsAreTableDriven)
{
    const std::vector<std::pair<int, imterm::PaletteIndex>> colors = {
        {30, imterm::PaletteIndex::Black},
        {31, imterm::PaletteIndex::Red},
        {32, imterm::PaletteIndex::Green},
        {33, imterm::PaletteIndex::Yellow},
        {34, imterm::PaletteIndex::Blue},
        {35, imterm::PaletteIndex::Magenta},
        {36, imterm::PaletteIndex::Cyan},
        {37, imterm::PaletteIndex::White},
    };

    int column = 0;
    for (const auto& [code, expected] : colors) {
        state->Input(imterm::test::Bytes(
            "\x1B[0;" + std::to_string(code) + "mX"));
        EXPECT_EQ(data->GetLine(0)[static_cast<size_t>(column)].mColorIndex,
            expected) << "SGR=" << code;
        ++column;
    }

    for (const auto& [foregroundCode, expected] : colors) {
        const int backgroundCode = foregroundCode + 10;
        state->Input(imterm::test::Bytes(
            "\x1B[0;" + std::to_string(backgroundCode) + ";7mX"));
        EXPECT_EQ(data->GetLine(0)[static_cast<size_t>(column)].mColorIndex,
            expected) << "SGR=" << backgroundCode << " inverse";
        ++column;
    }
}

TEST_F(TerminalStateTest, DefaultSgrColorsResetForegroundAndBackground)
{
    state->Input(imterm::test::Bytes("\x1B[31mR\x1B[39mD"));
    ASSERT_EQ(data->GetLine(0).size(), 2U);
    EXPECT_EQ(data->GetLine(0)[0].mColorIndex, imterm::PaletteIndex::Red);
    EXPECT_EQ(data->GetLine(0)[1].mColorIndex, imterm::PaletteIndex::Default);

    state->Input(imterm::test::Bytes(
        "\x1B[0;41;7mR\x1B[49mD"));
    ASSERT_EQ(data->GetLine(0).size(), 4U);
    EXPECT_EQ(data->GetLine(0)[2].mColorIndex, imterm::PaletteIndex::Red);
    EXPECT_EQ(data->GetLine(0)[3].mColorIndex, imterm::PaletteIndex::Default);
}

TEST_F(TerminalStateTest, StatusReportsUseOneBasedScreenCoordinates)
{
    const auto report = imterm::test::Bytes("\x1B[6n");
    for (size_t split = 0; split <= report.size(); ++split) {
        state->Input(imterm::test::Bytes("\x1B[2;3H"));
        state->Input(std::span(report).first(split));
        state->Input(std::span(report).subspan(split));

        ASSERT_TRUE(state->TerminalOutputAvailable()) << "split=" << split;
        EXPECT_EQ(state->GetTerminalOutput(), imterm::test::Bytes("\x1B[2;3R"))
            << "split=" << split;
    }
}

TEST_F(TerminalStateTest, ClampsLargeCursorMovementsInEveryDirection)
{
    state->SetViewportSize(5, 10);

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
        state->SetViewportSize(3, 80);

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
    state->SetViewportSize(5, 80);
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
