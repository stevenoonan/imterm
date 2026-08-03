#include <gtest/gtest.h>

#include <memory>

#include "terminal_logger.h"
#include "terminal_state.h"
#include "test_support.h"

namespace {

TEST(TerminalLoggerTest, WritesConfiguredLineFormat)
{
    imterm::test::TemporaryDirectory directory;
    imterm::TerminalLogger::Options options;
    options.Enabled = true;
    options.LineNumbers = true;
    options.TimeStamps = false;
    imterm::TerminalLogger logger(
        false, "session", ".log", directory.Path(), options);
    imterm::Line line;
    line.emplace_back('o', imterm::PaletteIndex::Default);
    line.emplace_back('k', imterm::PaletteIndex::Default);

    logger.Log(line, 7);
    logger.Close();

    EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "session.log"),
        "7 ok\n");
}

TEST(TerminalLoggerTest, ClosingFlushesAnIncompleteTerminalLine)
{
    imterm::test::TemporaryDirectory directory;
    imterm::TerminalLogger::Options options;
    options.Enabled = true;
    options.LineNumbers = false;
    options.TimeStamps = false;
    auto logger = std::make_shared<imterm::TerminalLogger>(
        false, "partial", ".log", directory.Path(), options);

    {
        imterm::TerminalData data(logger);
        int column = 0;
        data.InputGlyph(
            data.mLines.front(), column, imterm::PaletteIndex::Default, 'x');

        logger->Close();

        EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "partial.log"),
            "x\n");
    }
}

TEST(TerminalLoggerTest, CompletingALineLogsThroughTerminalData)
{
    imterm::test::TemporaryDirectory directory;
    imterm::TerminalLogger::Options options;
    options.Enabled = true;
    options.LineNumbers = false;
    options.TimeStamps = false;
    auto logger = std::make_shared<imterm::TerminalLogger>(
        false, "complete", ".log", directory.Path(), options);

    {
        auto data = std::make_shared<imterm::TerminalData>(logger);
        imterm::TerminalState state(
            data, imterm::TerminalState::NewLineMode::AddCrToLf);
        state.SetBounds(Coordinates(0, 79));

        state.Input(imterm::test::Bytes("complete\n"));

        EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "complete.log"),
            "complete\n");
    }
}

} // namespace
