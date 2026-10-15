#include <gtest/gtest.h>

#include "terminal_view.h"
#include "raw_serial_capture.h"
#include "test_support.h"

#ifdef __linux__
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include "serial/serial.h"
#endif

namespace {
// A clipboard backend independent of the OS/display.
std::string clipboard;
const std::string text = "A\xC3\xA9\xE6\xBC\xA2\xF0\x9F\x98\x80Z";

class TerminalEncodingUiTest : public testing::Test {
protected:
    void SetUp() override
    {
        static_assert(sizeof(ImWchar) == 4, "ImGui and its consumers must support non-BMP input");
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        auto& platform = ImGui::GetPlatformIO();
        platform.Platform_GetClipboardTextFn = [](ImGuiContext*) { return clipboard.c_str(); };
        platform.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* value) { clipboard = value; };
        clipboard.clear();
        data = std::make_shared<imterm::TerminalData>();
        state = std::make_shared<imterm::TerminalState>(data, imterm::TerminalState::NewLineMode::Strict);
        view = std::make_unique<imterm::TerminalView>(data, state, imterm::TerminalView::Options{false, false});
        view->SetHandleMouseInputs(false);
        view->SetImGuiChildIgnored(true);
        Frame();
        Frame();
    }

    void TearDown() override
    {
        view.reset();
        ImGui::DestroyContext();
    }

    void Frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 600));
        ImGui::SetNextWindowFocus();
        ImGui::Begin("Encoding test");
        view->Render("Terminal");
        ImGui::End();
        ImGui::Render();
    }

    std::string Drain()
    {
        std::string bytes;
        while (view->KeyboardInputAvailable()) bytes += static_cast<char>(view->GetKeyboardInput());
        return bytes;
    }

    std::shared_ptr<imterm::TerminalData> data;
    std::shared_ptr<imterm::TerminalState> state;
    std::unique_ptr<imterm::TerminalView> view;
};

TEST_F(TerminalEncodingUiTest, ImGuiCodePointsBecomeUtf8BytesExactlyOnce)
{
    auto& io = ImGui::GetIO();
    for (unsigned int codePoint : std::initializer_list<unsigned int>{'A', 0xE9, 0x6F22, 0x1F600, 'Z'}) io.AddInputCharacter(codePoint);
    Frame();
    EXPECT_EQ(Drain(), text);
    Frame();
    EXPECT_EQ(Drain(), "");
}

TEST_F(TerminalEncodingUiTest, PastePassesUtf8AndMalformedBytesThroughUnchanged)
{
    clipboard = text + "\r\n\xFF\xE2\x82";
    view->Paste();
    EXPECT_EQ(Drain(), clipboard);
}

TEST_F(TerminalEncodingUiTest, SelectedCharactersCopyAndPasteWithoutTruncation)
{
    state->Input(imterm::test::Bytes(text));
    Frame();
    view->SetSelection(Coordinates(0, 1), Coordinates(0, 4));
    EXPECT_EQ(view->GetSelectedText(), text.substr(1, text.size() - 2));
    view->Copy();
    EXPECT_EQ(clipboard, view->GetSelectedText());
    view->Paste();
    EXPECT_EQ(Drain(), clipboard);

    view->SetCursorPosition(Coordinates(0, 5));
    for (int column = 4; column >= 0; --column) {
        view->MoveLeft();
        EXPECT_EQ(view->GetCursorPosition(), Coordinates(0, column));
    }
    for (int column = 1; column <= 5; ++column) {
        view->MoveRight();
        EXPECT_EQ(view->GetCursorPosition(), Coordinates(0, column));
    }
}

TEST_F(TerminalEncodingUiTest, InvalidSerialTextRendersAndCopiesAsReplacement)
{
    state->Input(imterm::test::Bytes("A\xFF\xE2\x82"));
    state->FinishInput();
    Frame();
    view->SetSelection(Coordinates(0, 0), Coordinates(0, 4));
    view->Copy();
    EXPECT_EQ(clipboard, "A\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
}

#ifdef __linux__
TEST_F(TerminalEncodingUiTest, SerialLoopbackPreservesKeyboardPasteAndSplitReads)
{
    struct Master {
        int fd = posix_openpt(O_RDWR | O_NOCTTY);
        ~Master() { if (fd >= 0) close(fd); }
    } master;
    ASSERT_GE(master.fd, 0);
    ASSERT_EQ(grantpt(master.fd), 0);
    ASSERT_EQ(unlockpt(master.fd), 0);
    const char* slave = ptsname(master.fd);
    ASSERT_NE(slave, nullptr);
    serial::Serial port(slave, 115200, serial::Timeout::simpleTimeout(200));

    auto& io = ImGui::GetIO();
    io.AddInputCharactersUTF8(text.c_str());
    Frame();
    const auto send = [&] {
        while (view->KeyboardInputAvailable()) {
            const uint8_t byte = view->GetKeyboardInput();
            EXPECT_EQ(port.write(&byte, 1), 1U);
        }
    };
    const auto receiveAtDevice = [&](const std::string& expected) {
        std::string received;
        while (received.size() < expected.size()) {
            pollfd descriptor{master.fd, POLLIN, 0};
            ASSERT_GT(poll(&descriptor, 1, 200), 0);
            char buffer[64];
            const auto count = read(master.fd, buffer, sizeof(buffer));
            ASSERT_GT(count, 0);
            received.append(buffer, static_cast<size_t>(count));
        }
        EXPECT_EQ(received, expected);
    };
    send();
    receiveAtDevice(text);

    // Return the device output in single-byte reads, including a broken suffix.
    imterm::test::TemporaryDirectory directory;
    imterm::RawSerialCapture capture;
    const auto capturePath = directory.Path() / "rx.bin";
    ASSERT_TRUE(capture.Start(capturePath));
    const std::string response = text + "\xFF\xE2\x82";
    ASSERT_EQ(write(master.fd, response.data(), response.size()),
        static_cast<ssize_t>(response.size()));
    for (size_t i = 0; i < response.size(); ++i) {
        // The vector overload appends actual bytes, just as capture polling does.
        std::vector<uint8_t> buffer;
        buffer.reserve(1);
        ASSERT_EQ(port.read(buffer, 1), 1U);
        ASSERT_EQ(buffer.size(), 1U);
        ASSERT_TRUE(capture.Append(buffer));
        state->Input(buffer);
    }
    port.close();
    state->FinishInput();
    ASSERT_TRUE(capture.Stop());
    EXPECT_EQ(imterm::test::ReadFile(capturePath), response);
    Frame();
    view->SetSelection(Coordinates(0, 0), Coordinates(0, 8));
    view->Copy();
    EXPECT_EQ(clipboard, text + "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    port.open();
    view->Paste();
    send();
    receiveAtDevice(clipboard);
}
#endif
}
