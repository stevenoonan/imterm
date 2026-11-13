#include <gtest/gtest.h>
#include "terminal_selection.h"
#include "terminal_viewport.h"
#include "terminal_palette.h"
#include "test_support.h"

namespace {
using imterm::SelectionMove;
using imterm::SelectionMode;
class TerminalSelectionTest : public testing::Test {
protected:
    imterm::TerminalData data;
    imterm::TerminalSelection selection{data};
};

TEST_F(TerminalSelectionTest, NormalizesReversedAndOutOfRangeSelections)
{
    data.SetTextLines({"first", "second"});
    selection.SetRange({1, 100}, {0, 2});
    EXPECT_EQ(selection.Start(), Coordinates(0, 2));
    EXPECT_EQ(selection.End(), Coordinates(1, 6));
    EXPECT_EQ(selection.SelectedText(), "rst\nsecond");
    selection.SelectAll();
    EXPECT_EQ(selection.SelectedText(), "first\nsecond");
}
TEST_F(TerminalSelectionTest, Utf8AndTabsMoveByCharactersRatherThanBytes)
{
    data.SetText("A\xC3\xA9\t\xF0\x9F\x98\x80Z");
    data.SetTabSize(4);
    for (int column : {1, 2, 4, 5, 6}) {
        selection.Move(SelectionMove::Right);
        EXPECT_EQ(selection.Cursor().mColumn, column);
    }
    for (int column : {5, 4, 2, 1, 0}) {
        selection.Move(SelectionMove::Left);
        EXPECT_EQ(selection.Cursor().mColumn, column);
    }
    selection.SetRange({0, 1}, {0, 5});
    EXPECT_EQ(selection.SelectedText(), "\xC3\xA9\t\xF0\x9F\x98\x80");
}
TEST_F(TerminalSelectionTest, MultiStepNavigationCrossesLinesAndCommitsSelectionAtEnd)
{
    data.SetTextLines({"A", "\xC3\xA9Z"});
    selection.Move(SelectionMove::Right, 20, true);
    EXPECT_EQ(selection.Cursor(), Coordinates(1, 2));
    EXPECT_EQ(selection.SelectedText(), "A\n\xC3\xA9Z");
    selection.Move(SelectionMove::Left, 20, true);
    EXPECT_EQ(selection.Cursor(), Coordinates(0, 0));
    EXPECT_FALSE(selection.HasSelection());
}
TEST_F(TerminalSelectionTest, SelectionAnchorSurvivesDirectionChanges)
{
    data.SetText("abcdef");
    selection.SetCursor({0, 3});
    selection.Move(SelectionMove::Left, 2, true);
    EXPECT_EQ(selection.SelectedText(), "bc");
    selection.Move(SelectionMove::Right, 4, true);
    EXPECT_EQ(selection.SelectedText(), "de");
    selection.Move(SelectionMove::Left, 1, false);
    EXPECT_FALSE(selection.HasSelection());
}
TEST_F(TerminalSelectionTest, VerticalMovesPreservePreferredColumnThroughShortLines)
{
    data.SetTextLines({"abcdef", "x", "abcdef"});
    selection.SetCursor({0, 5});
    selection.Move(SelectionMove::Down);
    EXPECT_EQ(selection.Cursor(), Coordinates(1, 1));
    selection.Move(SelectionMove::Down);
    EXPECT_EQ(selection.Cursor(), Coordinates(2, 5));
    selection.Move(SelectionMove::Top);
    EXPECT_EQ(selection.Cursor(), Coordinates(0, 0));
    selection.Move(SelectionMove::Bottom, 1, true);
    EXPECT_EQ(selection.SelectedText(), "abcdef\nx\n");
}
TEST_F(TerminalSelectionTest, WordSelectionUsesWhitespaceAndIgnoresAnsiColors)
{
    int column = 0;
    data.InputBytes(0, column, imterm::PaletteIndex::Red, imterm::test::Bytes("hel"));
    data.InputBytes(0, column, imterm::PaletteIndex::Green, imterm::test::Bytes("lo \xC3\xA9!"));
    selection.Begin({0, 2}, SelectionMode::Word);
    EXPECT_EQ(selection.SelectedText(), "hello");
    selection.Begin({0, 7}, SelectionMode::Word);
    EXPECT_EQ(selection.SelectedText(), "\xC3\xA9!");
    selection.Begin({0, 5}, SelectionMode::Word);
    EXPECT_EQ(selection.SelectedText(), " ");
}
TEST_F(TerminalSelectionTest, WordDragIncludesAnchorWordInBothDirections)
{
    data.SetText("one two three");
    selection.Begin({0, 4}, SelectionMode::Word);
    selection.Drag({0, 1});
    EXPECT_EQ(selection.SelectedText(), "one two");
    selection.Drag({0, 9});
    EXPECT_EQ(selection.SelectedText(), "two three");
}
TEST_F(TerminalSelectionTest, LineDragAndMultilineHighlightsIncludeNewlines)
{
    data.SetTextLines({"abc", "def", "ghi"});
    selection.Begin({1, 2}, SelectionMode::Line);
    EXPECT_EQ(selection.SelectedText(), "def");
    selection.Drag({0, 2});
    EXPECT_EQ(selection.SelectedText(), "abc\ndef");
    ASSERT_TRUE(selection.OnLine(0));
    EXPECT_TRUE(selection.OnLine(0)->includesNewline);
    EXPECT_EQ(selection.OnLine(0)->firstColumn, 0);
    EXPECT_EQ(selection.OnLine(0)->lastColumn, 3);
    ASSERT_TRUE(selection.OnLine(1));
    EXPECT_FALSE(selection.OnLine(1)->includesNewline);
    EXPECT_FALSE(selection.OnLine(2));
}
TEST_F(TerminalSelectionTest, ShrinkingDataClampsCaretAndSelectionWithoutMutatingText)
{
    data.SetTextLines({"first", "second"});
    selection.SetCursor({1, 6});
    selection.SelectAll();
    data.SetText("x");
    const auto original = data.GetText();
    EXPECT_EQ(selection.Cursor(), Coordinates(0, 1));
    EXPECT_EQ(selection.SelectedText(), "x");
    selection.Reset();
    EXPECT_FALSE(selection.HasSelection());
    EXPECT_EQ(data.GetText(), original);
}
TEST(TerminalViewportTest, VisibleRangeClampsEmptySmallAndOverscrolledBuffers)
{
    EXPECT_EQ(imterm::CalculateVisibleLines(0, 13, 0, 100).last, 0);
    EXPECT_EQ(imterm::CalculateVisibleLines(3, 13, 0, 100).last, 2);
    const auto range = imterm::CalculateVisibleLines(1000, 20, 6000, 100);
    EXPECT_EQ(range.first, 300);
    EXPECT_EQ(range.last, 305);
    EXPECT_EQ(imterm::CalculateVisibleLines(3, 13, 10000, 100).first, 2);
}
TEST(TerminalViewportTest, RevealUsesPixelsForHorizontalScrollingAndNeverGoesNegative)
{
    auto scroll = imterm::RevealTerminalPoint({0, 0}, 200, 100, 500, 300, 10, 20);
    EXPECT_GT(scroll.x, 300);
    EXPECT_GE(scroll.y, 240);
    scroll = imterm::RevealTerminalPoint(scroll, 200, 100, 0, 0, 10, 20);
    EXPECT_FLOAT_EQ(scroll.x, 0);
    EXPECT_FLOAT_EQ(scroll.y, 0);
}
TEST(TerminalPaletteTest, ThemesKeepAnsiColorsAndHaveDistinctBackgrounds)
{
    const auto& dark = imterm::GetDarkTerminalPalette();
    const auto& light = imterm::GetLightTerminalPalette();
    const auto& retro = imterm::GetRetroBlueTerminalPalette();
    const auto index = [](imterm::PaletteIndex value) { return static_cast<size_t>(value); };
    EXPECT_EQ(dark[index(imterm::PaletteIndex::Default)], 0xffeeeeeeu);
    EXPECT_EQ(light[index(imterm::PaletteIndex::Background)], 0xffffffffu);
    EXPECT_NE(dark[index(imterm::PaletteIndex::Background)], retro[index(imterm::PaletteIndex::Background)]);
    for (auto color = index(imterm::PaletteIndex::Black); color < dark.size(); ++color) {
        EXPECT_EQ(dark[color], light[color]);
        EXPECT_EQ(dark[color], retro[color]);
    }
}
} // namespace
