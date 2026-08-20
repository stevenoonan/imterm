#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <thread>

#include "terminal_data.h"
#include "test_support.h"

namespace {

TEST(TerminalDataTest, StartsWithOneEmptyLine)
{
    imterm::TerminalData data;

    ASSERT_EQ(data.GetLineCount(), 1U);
    EXPECT_TRUE(data.GetLine(0).empty());
    EXPECT_FALSE(data.IsTextChanged());
}

TEST(TerminalDataTest, GetTextTerminatesEveryStoredLine)
{
    imterm::TerminalData data;

    data.SetText("first\r\nsecond\n");

    EXPECT_EQ(data.GetTextLines(),
        std::vector<std::string>({"first", "second", ""}));
    // Characterization: GetText appends a newline for every stored line,
    // including the empty line created by the trailing input newline.
    EXPECT_EQ(data.GetText(), "first\nsecond\n\n");
    EXPECT_TRUE(data.IsTextChanged());
}

TEST(TerminalDataTest, InsertsTextAndSplitsLines)
{
    imterm::TerminalData data;
    data.SetText("ab\ncd");
    Coordinates position(0, 1);

    const int inserted_lines = data.InsertTextAt(position, "X\nY");

    EXPECT_EQ(inserted_lines, 1);
    EXPECT_EQ(position, Coordinates(1, 1));
    EXPECT_EQ(data.GetTextLines(),
        std::vector<std::string>({"aX", "Yb", "cd"}));
}

TEST(TerminalDataTest, ConvertsBetweenTabColumnsAndByteIndices)
{
    imterm::TerminalData data;
    data.SetText("\tA");

    EXPECT_EQ(data.GetLineCharacterCount(0), 2);
    EXPECT_EQ(data.GetLineMaxColumn(0), 5);
    EXPECT_EQ(data.GetCharacterIndex(Coordinates(0, 4)), 1);
    EXPECT_EQ(data.GetCharacterColumn(0, 1), 4);
}

TEST(TerminalDataTest, DeletesAcrossMultipleLines)
{
    imterm::TerminalData data;
    data.SetText("abc\ndef\nghi");

    data.DeleteRange(Coordinates(0, 1), Coordinates(2, 1));

    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>({"ahi"}));
}

TEST(TerminalDataTest, RepeatedGrowthResetAndRemovalPreserveANonemptyBuffer)
{
    imterm::TerminalData data;

    for (int index = 0; index < 4096; ++index) {
        data.InsertLine(static_cast<int>(data.GetLineCount()));
    }
    EXPECT_EQ(data.GetLineCount(), 4097U);

    data.SetTextLines({});
    ASSERT_EQ(data.GetLineCount(), 1U);
    EXPECT_TRUE(data.GetLine(0).empty());

    data.SetText("one\ntwo\nthree");
    while (data.GetLineCount() > 1) {
        data.RemoveLine(0);
    }
    ASSERT_EQ(data.GetLineCount(), 1U);
    EXPECT_EQ(imterm::test::LineText(data.GetLine(0)), "three");
    EXPECT_THROW(data.RemoveLine(0), std::invalid_argument);
    EXPECT_EQ(data.GetLineCount(), 1U);
}

TEST(TerminalDataTest, TimestampTracksTheMostRecentContentMutation)
{
    imterm::TerminalData data;
    const auto initialTimestamp = data.GetLine(0).GetTimestamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    int column = 0;
    data.InputGlyph(0, column, imterm::PaletteIndex::Default, 'x');
    const auto inputTimestamp = data.GetLine(0).GetTimestamp();
    EXPECT_GT(inputTimestamp, initialTimestamp);

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    data.ReplaceBytesWithSpaces(0, 0, 1);
    EXPECT_GT(data.GetLine(0).GetTimestamp(), inputTimestamp);
}

} // namespace
