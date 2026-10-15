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

TEST(TerminalDataTest, UnicodeColumnsAndSelectionRemainDistinctFromBytes)
{
    imterm::TerminalData data;
    const std::string text = "A\xC3\xA9\xE6\xBC\xA2\xF0\x9F\x98\x80Z";
    data.SetText(text);
    const int offsets[] = {0, 1, 3, 6, 10, 11};
    for (int column = 0; column <= 5; ++column) {
        EXPECT_EQ(data.GetCharacterIndex(Coordinates(0, column)), offsets[column]);
        EXPECT_EQ(data.GetCharacterColumn(0, offsets[column]), column);
    }
    EXPECT_EQ(data.GetLineCharacterCount(0), 5);
    EXPECT_EQ(data.GetLineMaxColumn(0), 5);
    EXPECT_EQ(data.GetText(Coordinates(0, 1), Coordinates(0, 4)), text.substr(1, 9));
    data.DeleteRange(Coordinates(0, 1), Coordinates(0, 4));
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{"AZ"});
}

TEST(TerminalDataTest, AllTextInsertionBoundariesReplaceMalformedUtf8)
{
    imterm::TerminalData data;
    const std::string invalid = "\xC3\xA9\xFF\xE2\x82";
    const std::string expected = "\xC3\xA9\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD";
    data.SetText(invalid);
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{expected});
    data.SetTextLines({invalid});
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{expected});
    data.SetText("");
    Coordinates position;
    data.InsertTextAt(position, invalid.c_str());
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{expected});
    EXPECT_EQ(position, Coordinates(0, 4));
    data.SetText("");
    int column = 0;
    data.InputBytes(0, column, imterm::PaletteIndex::Red, imterm::test::Bytes(invalid));
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{expected});
    EXPECT_EQ(column, 4);
}

TEST(TerminalDataTest, OverwritesWholeUnicodeScalars)
{
    imterm::TerminalData data;
    data.SetText("\xF0\x9F\x98\x80X");
    int column = 0;
    data.InputGlyph(0, column, imterm::PaletteIndex::Default, 'A');
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{"AX"});
    column = 0;
    data.InputBytes(0, column, imterm::PaletteIndex::Default,
        imterm::test::Bytes("\xE6\xBC\xA2"));
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{"\xE6\xBC\xA2X"});
    EXPECT_EQ(column, 1);
}

TEST(TerminalDataTest, ByteErasureCannotLeavePartialUnicodeScalars)
{
    imterm::TerminalData data;
    data.SetText("A\xF0\x9F\x98\x80Z");
    data.EraseBytes(0, 2, 3);
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{"AZ"});
    data.SetText("A\xF0\x9F\x98\x80Z");
    data.ReplaceBytesWithSpaces(0, 2, 3);
    EXPECT_EQ(data.GetTextLines(), std::vector<std::string>{"A Z"});
}

TEST(TerminalDataTest, FindsNextTextFromAnAnchorAndWraps)
{
    imterm::TerminalData data;
    data.SetText("one hit\nno match\nhit again");

    const auto first = data.FindNextText("hit", Coordinates(0, 4), true);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->mStart, Coordinates(0, 4));
    EXPECT_EQ(first->mEnd, Coordinates(0, 7));

    const auto second = data.FindNextText("hit", first->mEnd, true);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->mStart, Coordinates(2, 0));

    const auto wrapped = data.FindNextText("hit", second->mEnd, true);
    ASSERT_TRUE(wrapped.has_value());
    EXPECT_EQ(wrapped->mStart, first->mStart);
}

TEST(TerminalDataTest, FindsPreviousTextBeforeAnAnchorAndWraps)
{
    imterm::TerminalData data;
    data.SetText("hit one\nno match\nlast hit");

    const auto previous = data.FindPreviousText("hit", Coordinates(2, 8), true);
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(previous->mStart, Coordinates(2, 5));

    const auto earlier = data.FindPreviousText("hit", previous->mStart, true);
    ASSERT_TRUE(earlier.has_value());
    EXPECT_EQ(earlier->mStart, Coordinates(0, 0));

    const auto wrapped = data.FindPreviousText("hit", earlier->mStart, true);
    ASSERT_TRUE(wrapped.has_value());
    EXPECT_EQ(wrapped->mStart, previous->mStart);
}

TEST(TerminalDataTest, FindTextSupportsAsciiCaseMatching)
{
    imterm::TerminalData data;
    data.SetText("Alpha alpha");

    const auto insensitive = data.FindNextText("ALPHA", Coordinates(), false);
    ASSERT_TRUE(insensitive.has_value());
    EXPECT_EQ(insensitive->mStart, Coordinates(0, 0));

    EXPECT_FALSE(data.FindNextText("ALPHA", Coordinates(), true).has_value());
    const auto sensitive = data.FindNextText("alpha", Coordinates(), true);
    ASSERT_TRUE(sensitive.has_value());
    EXPECT_EQ(sensitive->mStart, Coordinates(0, 6));
}

TEST(TerminalDataTest, FindTextReturnsRenderedColumnsForTabsAndUtf8)
{
    imterm::TerminalData data;
    data.SetText("\tA \xC3\xA9" "cho");

    const auto tabMatch = data.FindNextText("A", Coordinates(), true);
    ASSERT_TRUE(tabMatch.has_value());
    EXPECT_EQ(tabMatch->mStart, Coordinates(0, 4));
    EXPECT_EQ(tabMatch->mEnd, Coordinates(0, 5));

    const auto utf8Match = data.FindNextText("\xC3\xA9" "cho", Coordinates(), true);
    ASSERT_TRUE(utf8Match.has_value());
    EXPECT_EQ(utf8Match->mStart, Coordinates(0, 6));
    EXPECT_EQ(utf8Match->mEnd, Coordinates(0, 10));
    EXPECT_TRUE(data.TextMatches(*utf8Match, "\xC3\xA9" "cho", true));
}

TEST(TerminalDataTest, FindTextRejectsEmptyAndCrossLineQueries)
{
    imterm::TerminalData data;
    data.SetText("first\nsecond");

    EXPECT_FALSE(data.FindNextText("", Coordinates(), false).has_value());
    EXPECT_FALSE(data.FindNextText("first\nsecond", Coordinates(), false).has_value());
    EXPECT_FALSE(data.FindPreviousText("missing", Coordinates(), false).has_value());
}

TEST(TerminalDataTest, ReportsTheCurrentAndTotalTextMatches)
{
    imterm::TerminalData data;
    data.SetText("hit hit\nHIT\naaa");

    const auto current = data.FindNextText("hit", Coordinates(0, 4), false);
    ASSERT_TRUE(current.has_value());

    const auto insensitive = data.GetTextMatchStatus("hit", current, false);
    EXPECT_EQ(insensitive.mCurrent, 2U);
    EXPECT_EQ(insensitive.mTotal, 3U);

    const auto sensitive = data.GetTextMatchStatus("hit", current, true);
    EXPECT_EQ(sensitive.mCurrent, 2U);
    EXPECT_EQ(sensitive.mTotal, 2U);

    const auto overlapping = data.GetTextMatchStatus("aa", std::nullopt, true);
    EXPECT_EQ(overlapping.mCurrent, 0U);
    EXPECT_EQ(overlapping.mTotal, 1U);

    const auto missing = data.GetTextMatchStatus("missing", std::nullopt, false);
    EXPECT_EQ(missing.mCurrent, 0U);
    EXPECT_EQ(missing.mTotal, 0U);
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
