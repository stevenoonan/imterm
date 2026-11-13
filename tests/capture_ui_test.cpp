#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include "capture.h"
#include "imgui_internal.h"
#include "test_support.h"

namespace {
using namespace std::chrono_literals;
using State = imterm::CaptureSession::State;

class UiTransport : public imterm::SerialTransport {
public:
    explicit UiTransport(std::atomic<bool>& failed, std::string openError = {})
        : mFailed(failed), mOpenError(std::move(openError)) {}
    void Open(const imterm::SerialParameters&) override {
        std::this_thread::sleep_for(20ms);
        if (!mOpenError.empty()) throw std::runtime_error(mOpenError);
    }
    size_t Read(std::span<uint8_t>) override {
        if (mFailed) throw std::runtime_error("disconnected");
        return 0;
    }
    size_t Write(std::span<const uint8_t> bytes) override { return bytes.size(); }
    imterm::ModemSignals GetSignals() override { return {true, true, true}; }
    void SetDTR(bool) override {}
    void SetRTS(bool) override {}
private:
    std::atomic<bool>& mFailed;
    std::string mOpenError;
};

class CaptureUiTest : public testing::Test {
protected:
    imterm::test::TemporaryDirectory directory;
    std::filesystem::path originalDirectory;
    std::atomic<bool> failed{false};
    std::string openError;
    std::unique_ptr<imterm::Capture> capture;
    std::string modal;

    void SetUp() override {
        originalDirectory = std::filesystem::current_path();
        std::filesystem::current_path(directory.Path());
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(900, 600);
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        capture = std::make_unique<imterm::Capture>([this] { return std::make_unique<UiTransport>(failed, openError); });
        capture->InitializeInterfaceZoom();
    }
    void TearDown() override {
        capture.reset();
        ImGui::DestroyContext();
        std::filesystem::current_path(originalDirectory);
    }
    void Frame() {
        capture->Update();
        capture->ApplyPendingInterfaceZoom();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(900, 600));
        capture->Render();
        const auto* popup = ImGui::GetTopMostPopupModal();
        modal = popup ? popup->Name : "";
        ImGui::Render();
    }
    bool Until(State state) {
        const auto deadline = imterm::CaptureSession::Clock::now() + 2s;
        do {
            Frame();
            if (capture->GetSession().GetState() == state) return true;
            std::this_thread::sleep_for(1ms);
        } while (imterm::CaptureSession::Clock::now() < deadline);
        return false;
    }
    void ExpectErrorVisible(const char* windowName) {
        const auto* window = ImGui::FindWindowByName(windowName);
        ASSERT_NE(window, nullptr);
        const auto color = ImGui::GetColorU32(ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        size_t vertices = 0;
        for (const auto& vertex : window->DrawList->VtxBuffer) {
            if (vertex.col != color) continue;
            ++vertices;
            EXPECT_GE(vertex.pos.x, window->InnerClipRect.Min.x);
            EXPECT_GE(vertex.pos.y, window->InnerClipRect.Min.y);
            EXPECT_LE(vertex.pos.x, window->InnerClipRect.Max.x);
            EXPECT_LE(vertex.pos.y, window->InnerClipRect.Max.y);
        }
        const auto& error = capture->GetSession().GetError();
        ASSERT_FALSE(error.empty());
        // Each visible ASCII glyph contributes a quad. Check the actual drawn
        // text, including clipping, rather than just the stored session error.
        EXPECT_EQ(vertices, 4u * std::count_if(error.begin(), error.end(), [](char c) {
            return c != ' ' && c != '\n';
        }));
    }
};

TEST_F(CaptureUiTest, StartupAndReconfigurationRenderWithAnOwnedTerminal)
{
    Frame();
    EXPECT_EQ(modal, "Setup");
    EXPECT_NE(capture->GetSession().GetTerminalState(), nullptr);
    capture->GetSession().BeginReconfiguration();
    Frame();
    EXPECT_EQ(modal, "Setup");
    capture->GetSession().Cancel();
    Frame();
    EXPECT_EQ(modal, "Setup");
}

TEST_F(CaptureUiTest, SuccessfulConnectClosesTheConnectingModal)
{
    ASSERT_TRUE(capture->GetSession().Connect({"loopback"}));
    Frame();
    EXPECT_EQ(modal, "Connecting...");
    ASSERT_TRUE(Until(State::Connected));
    EXPECT_TRUE(modal.empty()) << modal;
    capture->GetSession().Disconnect();
    Frame();
    EXPECT_EQ(modal, "Setup");
}

TEST_F(CaptureUiTest, ReconnectCancellationClosesModalAndReturnsToSetup)
{
    ASSERT_TRUE(capture->GetSession().Connect({"loopback"}));
    ASSERT_TRUE(Until(State::Connected));
    failed = true;
    ASSERT_TRUE(Until(State::Reconnecting));
    EXPECT_EQ(modal, "Reconnecting...");
    capture->GetSession().Cancel();
    Frame();
    EXPECT_EQ(modal, "Setup");
}

TEST_F(CaptureUiTest, FailedConnectionRendersAnErrorAndSetupSafely)
{
    capture->GetSession().SetAutoReconnect(false);
    ASSERT_TRUE(capture->GetSession().Connect({"loopback"}));
    ASSERT_TRUE(Until(State::Connected));
    failed = true;
    ASSERT_TRUE(Until(State::Failed));
    EXPECT_EQ(modal, "Setup");
    EXPECT_EQ(capture->GetSession().GetError(), "disconnected");
    ExpectErrorVisible("Setup");
}

TEST_F(CaptureUiTest, FailedOpenShowsWrappedErrorInsideSetup)
{
    openError = "Unable to open /dev/serial/by-id/usb-example-device-with-a-long-name: "
        "permission denied (100% unavailable). Check the device permissions and retry.";
    ASSERT_TRUE(capture->GetSession().Connect({"loopback"}));
    ASSERT_TRUE(Until(State::Failed));
    Frame();
    EXPECT_EQ(modal, "Setup");
    EXPECT_EQ(capture->GetSession().GetError(), openError);
    ExpectErrorVisible("Setup");
}

TEST_F(CaptureUiTest, OutputQueueErrorIsVisibleAboveTerminal)
{
    ASSERT_TRUE(capture->GetSession().Connect({"loopback"}));
    ASSERT_TRUE(Until(State::Connected));
    EXPECT_FALSE(capture->GetSession().Send(std::vector<uint8_t>(256 * 1024 + 1, 'x')));
    Frame();
    EXPECT_TRUE(modal.empty());
    ExpectErrorVisible("loopback");
}
} // namespace
