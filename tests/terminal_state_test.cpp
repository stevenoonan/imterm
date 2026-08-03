#include <gtest/gtest.h>

#include <memory>
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

    ASSERT_EQ(data->mLines.size(), 1U);
    EXPECT_EQ(imterm::test::LineText(data->mLines[0]), "hello");
    EXPECT_EQ(state->getPosition(), Coordinates(0, 5));
}

TEST_F(TerminalStateTest, StrictNewlinePreservesTheColumn)
{
    state->Input(imterm::test::Bytes("A\nB"));

    ASSERT_EQ(data->mLines.size(), 2U);
    EXPECT_EQ(imterm::test::LineText(data->mLines[0]), "A");
    EXPECT_EQ(imterm::test::LineText(data->mLines[1]), " B");
}

TEST_F(TerminalStateTest, AddCrToLfResetsTheColumn)
{
    state->SetNewLineMode(imterm::TerminalState::NewLineMode::AddCrToLf);

    state->Input(imterm::test::Bytes("A\nB"));

    ASSERT_EQ(data->mLines.size(), 2U);
    EXPECT_EQ(imterm::test::LineText(data->mLines[0]), "A");
    EXPECT_EQ(imterm::test::LineText(data->mLines[1]), "B");
}

TEST_F(TerminalStateTest, AddLfToCrAdvancesToANewLine)
{
    state->SetNewLineMode(imterm::TerminalState::NewLineMode::AddLfToCr);

    state->Input(imterm::test::Bytes("A\rB"));

    ASSERT_EQ(data->mLines.size(), 2U);
    EXPECT_EQ(imterm::test::LineText(data->mLines[0]), "A");
    EXPECT_EQ(imterm::test::LineText(data->mLines[1]), "B");
}

TEST_F(TerminalStateTest, RetainsEscapeParserStateAcrossInputChunks)
{
    state->Input(imterm::test::Bytes("\x1B[3"));
    state->Input(imterm::test::Bytes("1mR"));

    ASSERT_EQ(data->mLines[0].size(), 1U);
    EXPECT_EQ(data->mLines[0][0].mChar, 'R');
    EXPECT_EQ(data->mLines[0][0].mColorIndex, imterm::PaletteIndex::Red);
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

    ASSERT_EQ(data->mLines[0].size(), 4U);
    EXPECT_EQ(imterm::test::LineText(data->mLines[0]), "   X");
    EXPECT_EQ(state->getPosition(), Coordinates(0, 4));
}

TEST_F(TerminalStateTest, ErasesLineAfterCursor)
{
    state->Input(imterm::test::Bytes("abc\x1B[2D\x1B[K"));

    ASSERT_EQ(data->mLines.size(), 1U);
    EXPECT_EQ(imterm::test::LineText(data->mLines[0]), "a");
    EXPECT_EQ(state->getPosition(), Coordinates(0, 1));
}

TEST_F(TerminalStateTest, PreservesScrollbackWhenTheViewportFills)
{
    state->SetBounds(Coordinates(1, 79));
    state->SetNewLineMode(imterm::TerminalState::NewLineMode::AddCrToLf);

    EXPECT_EQ(state->Input(imterm::test::Bytes("0\n1\n2")), 2);

    ASSERT_EQ(data->mLines.size(), 3U);
    EXPECT_EQ(imterm::test::LineText(data->mLines[0]), "0");
    EXPECT_EQ(imterm::test::LineText(data->mLines[1]), "1");
    EXPECT_EQ(imterm::test::LineText(data->mLines[2]), "2");
    EXPECT_EQ(state->getPositionRelative(data->mLines.size()), Coordinates(2, 1));
}

} // namespace
