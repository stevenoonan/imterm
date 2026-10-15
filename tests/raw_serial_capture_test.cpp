#include <gtest/gtest.h>

#include "raw_serial_capture.h"
#include "terminal_state.h"
#include "test_support.h"

namespace {
using namespace std::string_literals;

TEST(RawSerialCaptureTest, PreservesEveryByteAndReadBoundaryWithoutMetadata)
{
    imterm::test::TemporaryDirectory directory;
    const auto path = directory.Path() / "rx.bin";
    imterm::RawSerialCapture capture;
    ASSERT_TRUE(capture.Start(path));
    std::string expected;
    for (int value = 0; value < 256; ++value) expected += static_cast<char>(value);
    const auto bytes = imterm::test::Bytes(expected);
    for (size_t offset = 0; offset < bytes.size(); offset += 7) {
        ASSERT_TRUE(capture.Append(std::span(bytes).subspan(
            offset, std::min<size_t>(7, bytes.size() - offset))));
    }
    // The file is inspectable before Stop and includes CR/LF, ESC, NUL, invalid
    // UTF-8, and an incomplete final byte without replacements or added newlines.
    EXPECT_EQ(imterm::test::ReadFile(path), expected);
    EXPECT_EQ(capture.GetByteCount(), 256U);
    EXPECT_TRUE(capture.Stop());
    EXPECT_FALSE(capture.IsActive());
    EXPECT_EQ(imterm::test::ReadFile(path), expected);
}

TEST(RawSerialCaptureTest, PreservesColorSequencesBeforeTerminalProcessing)
{
    imterm::test::TemporaryDirectory directory;
    const auto path = directory.Path() / "boot.bin";
    imterm::RawSerialCapture capture;
    ASSERT_TRUE(capture.Start(path));
    auto data = std::make_shared<imterm::TerminalData>();
    imterm::TerminalState state(data, imterm::TerminalState::NewLineMode::AddCrToLf);
    state.SetViewportSize(3, 80);
    const std::string raw = "\x1b[0;32mI (1226) quad_psram: ESP32\x1b[0m\r\n\xFF\0\xE2\x82"s;
    for (const uint8_t byte : imterm::test::Bytes(raw)) {
        ASSERT_TRUE(capture.Append(std::span(&byte, 1)));
        state.Input(std::span(&byte, 1));
    }
    state.FinishInput();
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "I (1226) quad_psram: ESP32");
    for (const auto& glyph : data->GetLine(0)) {
        EXPECT_EQ(glyph.mColorIndex, imterm::PaletteIndex::Green);
    }
    EXPECT_EQ(imterm::test::LineText(data->GetLine(1)), "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    EXPECT_TRUE(capture.Stop());
    EXPECT_EQ(imterm::test::ReadFile(path), raw);
}

TEST(RawSerialCaptureTest, StartingBeforeInputCreatesAnEmptyFileAndStopPreventsFurtherWrites)
{
    imterm::test::TemporaryDirectory directory;
    const auto path = directory.Path() / "empty.bin";
    imterm::RawSerialCapture capture;
    ASSERT_TRUE(capture.Start(path));
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(capture.Append({}));
    EXPECT_TRUE(capture.Stop());
    EXPECT_TRUE(capture.Stop());
    EXPECT_TRUE(capture.Append(imterm::test::Bytes("not captured")));
    EXPECT_EQ(capture.GetPath(), path);
    EXPECT_EQ(capture.GetByteCount(), 0U);
    EXPECT_EQ(imterm::test::ReadFile(path), "");
}

TEST(RawSerialCaptureTest, FailedStartIsVisibleAndCanBeRetried)
{
    imterm::test::TemporaryDirectory directory;
    imterm::RawSerialCapture capture;
    EXPECT_FALSE(capture.Start(directory.Path() / "missing" / "rx.bin"));
    EXPECT_FALSE(capture.IsActive());
    EXPECT_FALSE(capture.GetError().empty());
    ASSERT_TRUE(capture.Start(directory.Path() / "rx.bin"));
    EXPECT_TRUE(capture.GetError().empty());
    EXPECT_TRUE(capture.Append(imterm::test::Bytes("ok")));
    EXPECT_TRUE(capture.Stop());
}

TEST(RawSerialCaptureTest, CannotReplaceAnActiveCaptureAndCanStartANewOneAfterStopping)
{
    imterm::test::TemporaryDirectory directory;
    imterm::RawSerialCapture capture;
    const auto first = directory.Path() / "first.bin";
    const auto second = directory.Path() / "second.bin";
    ASSERT_TRUE(capture.Start(first));
    ASSERT_TRUE(capture.Append(imterm::test::Bytes("first")));
    EXPECT_FALSE(capture.Start(second));
    EXPECT_TRUE(capture.IsActive());
    EXPECT_EQ(capture.GetPath(), first);
    EXPECT_FALSE(std::filesystem::exists(second));
    EXPECT_TRUE(capture.Stop());
    ASSERT_TRUE(capture.Start(second));
    EXPECT_EQ(capture.GetByteCount(), 0U);
    ASSERT_TRUE(capture.Append(imterm::test::Bytes("second")));
    EXPECT_TRUE(capture.Stop());
    EXPECT_EQ(imterm::test::ReadFile(first), "first");
    EXPECT_EQ(imterm::test::ReadFile(second), "second");
}

TEST(RawSerialCaptureTest, DestructionFinishesTheFileWithoutATrailingNewline)
{
    imterm::test::TemporaryDirectory directory;
    const auto path = directory.Path() / "close.bin";
    {
        imterm::RawSerialCapture capture;
        ASSERT_TRUE(capture.Start(path));
        ASSERT_TRUE(capture.Append(imterm::test::Bytes("unfinished")));
    }
    EXPECT_EQ(imterm::test::ReadFile(path), "unfinished");
}

#ifdef __linux__
TEST(RawSerialCaptureTest, WriteFailureStopsOnlyTheRawCaptureAndCanBeRecovered)
{
    imterm::RawSerialCapture capture;
    ASSERT_TRUE(capture.Start("/dev/full"));
    const auto bytes = imterm::test::Bytes("still renders");
    EXPECT_FALSE(capture.Append(bytes));
    EXPECT_FALSE(capture.IsActive());
    EXPECT_FALSE(capture.GetError().empty());
    auto data = std::make_shared<imterm::TerminalData>();
    imterm::TerminalState state(data, imterm::TerminalState::NewLineMode::Strict);
    state.SetViewportSize(3, 80);
    state.Input(bytes);
    EXPECT_EQ(imterm::test::LineText(data->GetLine(0)), "still renders");
    imterm::test::TemporaryDirectory directory;
    ASSERT_TRUE(capture.Start(directory.Path() / "recovered.bin"));
    EXPECT_TRUE(capture.Append(bytes));
    EXPECT_TRUE(capture.Stop());
    EXPECT_EQ(imterm::test::ReadFile(capture.GetPath()), "still renders");
}
#endif
} // namespace
