#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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
    imterm::Line line{
        imterm::Glyph('o', imterm::PaletteIndex::Default),
        imterm::Glyph('k', imterm::PaletteIndex::Default)};

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
        data.InputGlyph(0, column, imterm::PaletteIndex::Default, 'x');

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
        state.SetViewportSize(1, 80);

        state.Input(imterm::test::Bytes("complete\n"));

        EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "complete.log"),
            "complete\n");
    }
}

TEST(TerminalLoggerTest, CursorCreatedRowsKeepThePendingLogLineValid)
{
    imterm::test::TemporaryDirectory directory;
    imterm::TerminalLogger::Options options;
    options.Enabled = true;
    options.LineNumbers = false;
    options.TimeStamps = false;
    auto logger = std::make_shared<imterm::TerminalLogger>(
        false, "cursor-rows", ".log", directory.Path(), options);

    {
        auto data = std::make_shared<imterm::TerminalData>(logger);
        imterm::TerminalState state(
            data, imterm::TerminalState::NewLineMode::AddCrToLf);
        state.SetViewportSize(5, 80);

        EXPECT_NO_THROW(state.Input(
            imterm::test::Bytes("\x1B[4BX\n")));
        logger->Close();
    }

    EXPECT_NE(imterm::test::ReadFile(directory.Path() / "cursor-rows.log")
                  .find("X\n"),
        std::string::npos);
}

TEST(TerminalLoggerTest, BufferResetFlushesAndReestablishesThePendingLine)
{
    imterm::test::TemporaryDirectory directory;
    imterm::TerminalLogger::Options options;
    options.Enabled = true;
    options.LineNumbers = false;
    options.TimeStamps = false;
    auto logger = std::make_shared<imterm::TerminalLogger>(
        false, "reset", ".log", directory.Path(), options);

    {
        imterm::TerminalData data(logger);
        int column = 0;
        data.InputGlyph(0, column, imterm::PaletteIndex::Default, 'a');
        data.SetText("replacement");
        logger->Close();
    }

    EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "reset.log"),
        "a\nreplacement\n");
}

TEST(TerminalLoggerTest, LoggingContinuesAfterAnExplicitClose)
{
    imterm::test::TemporaryDirectory directory;
    imterm::TerminalLogger::Options options;
    options.Enabled = true;
    options.LineNumbers = false;
    options.TimeStamps = false;
    auto logger = std::make_shared<imterm::TerminalLogger>(
        false, "reconnect", ".log", directory.Path(), options);
    auto data = std::make_shared<imterm::TerminalData>(logger);
    imterm::TerminalState state(
        data, imterm::TerminalState::NewLineMode::AddCrToLf);
    state.SetViewportSize(1, 80);

    state.Input(imterm::test::Bytes("first"));
    logger->Close();
    state.Input(imterm::test::Bytes("\nsecond"));
    logger->Close();

    EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "reconnect.log"),
        "first\nsecond\n");
}

TEST(TerminalLoggerTest, RegistrationTokensIdentifyIndividualWatchers)
{
    imterm::TerminalLogger::Options options;
    options.Enabled = false;
    imterm::TerminalLogger logger(options);
    int firstCalls = 0;
    int secondCalls = 0;
    const auto first = logger.RegisterLogClosingWatcher(
        [&firstCalls] { ++firstCalls; });
    logger.RegisterLogClosingWatcher([&secondCalls] { ++secondCalls; });

    EXPECT_TRUE(logger.DeregisterLogClosingWatcher(first));
    EXPECT_FALSE(logger.DeregisterLogClosingWatcher(first));
    logger.Close();

    EXPECT_EQ(firstCalls, 0);
    EXPECT_EQ(secondCalls, 1);
}

TEST(TerminalLoggerTest, AFailedWriteDoesNotLeaveTheLoggerReentrant)
{
    imterm::test::TemporaryDirectory directory;
    const auto blockedPath = directory.Path() / "not-a-directory";
    {
        std::ofstream file(blockedPath);
        file << "content";
    }
    imterm::TerminalLogger::Options options;
    options.Enabled = true;
    options.LineNumbers = false;
    options.TimeStamps = false;
    imterm::TerminalLogger logger(
        false, "failure", ".log", blockedPath, options);
    imterm::Line line{
        imterm::Glyph('x', imterm::PaletteIndex::Default)};

    EXPECT_THROW(logger.Log(line, 1), std::filesystem::filesystem_error);
    EXPECT_THROW(logger.Log(line, 1), std::filesystem::filesystem_error);
}

TEST(TerminalLoggerTest, MultipleLoggerInstancesRemainIndependent)
{
    imterm::test::TemporaryDirectory directory;
    imterm::TerminalLogger::Options options;
    options.Enabled = true;
    options.LineNumbers = false;
    options.TimeStamps = false;
    auto firstLogger = std::make_shared<imterm::TerminalLogger>(
        false, "first", ".log", directory.Path(), options);
    auto secondLogger = std::make_shared<imterm::TerminalLogger>(
        false, "second", ".log", directory.Path(), options);

    {
        imterm::TerminalData firstData(firstLogger);
        imterm::TerminalData secondData(secondLogger);
        int firstColumn = 0;
        int secondColumn = 0;
        firstData.InputGlyph(
            0, firstColumn, imterm::PaletteIndex::Default, 'A');
        secondData.InputGlyph(
            0, secondColumn, imterm::PaletteIndex::Default, 'B');
        firstLogger->Close();
        secondLogger->Close();
    }

    EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "first.log"), "A\n");
    EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "second.log"), "B\n");
}

} // namespace
