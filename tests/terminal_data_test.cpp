#include <gtest/gtest.h>

#include "terminal_data.h"

namespace {

TEST(TerminalDataTest, StartsWithOneEmptyLine)
{
    imterm::TerminalData data;

    ASSERT_EQ(data.mLines.size(), 1U);
    EXPECT_TRUE(data.mLines.front().empty());
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

} // namespace
