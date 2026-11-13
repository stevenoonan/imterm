#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include "capture.h"
#include "imgui_internal.h"
#include "test_support.h"

#ifdef __linux__
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

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

TEST_F(CaptureUiTest, MalformedPartialAndFutureConfigurationReachSetup)
{
    for (const auto& text : {std::string("[broken"), std::string("default = 'wrong'"),
        std::string("version = 999"), std::string("[default.serial]\nbaud = '") + std::string(10000, '9') + "'"}) {
        capture.reset();
        std::ofstream("imterm.toml") << text;
        ASSERT_NO_THROW(capture = std::make_unique<imterm::Capture>());
        capture->InitializeInterfaceZoom();
        Frame();
        Frame();
        EXPECT_EQ(modal, "Setup");
        EXPECT_FALSE(capture->GetSettings().GetWarnings().empty());
        EXPECT_TRUE(imterm::CaptureSettings::Validate(capture->GetSettings().Get()).empty());
        EXPECT_NE(capture->GetSession().GetTerminalState(), nullptr);
    }
}

TEST_F(CaptureUiTest, SelectedSettingsApplyAndSurviveRestart)
{
    auto values = capture->GetSettings().Get();
    values.serial.port = "loopback";
    values.serial.baudrate = 57600;
    values.serial.bytesize = serial::sevenbits;
    values.serial.parity = serial::parity_odd;
    values.serial.stopbits = serial::stopbits_one;
    values.serial.flowcontrol = serial::flowcontrol_hardware;
    values.newlineMode = imterm::TerminalState::NewLineMode::AddLfToCr;
    values.logging = {true, false, false};
    values.logPath = directory.Path() / "chosen logs";
    values.zoomFactor = 1.25f;
    ASSERT_TRUE(capture->Connect(values));
    ASSERT_TRUE(Until(State::Connected));
    EXPECT_EQ(capture->GetSession().GetParameters()->flowcontrol, serial::flowcontrol_hardware);
    EXPECT_FALSE(capture->GetSession().CanSetRTS());
    EXPECT_EQ(capture->GetSession().GetTerminalState()->GetNewLineMode(), values.newlineMode);
    EXPECT_EQ(capture->GetSession().GetLogger().GetBasePath(), values.logPath);
    EXPECT_TRUE(capture->GetSession().GetLogger().GetOptions().Enabled);
    EXPECT_FLOAT_EQ(capture->GetInterfaceZoomFactor(), 1.25f);
    capture.reset();
    capture = std::make_unique<imterm::Capture>();
    capture->InitializeInterfaceZoom();
    EXPECT_EQ(capture->GetSettings().Get().serial.port, "loopback");
    EXPECT_EQ(capture->GetSettings().Get().serial.baudrate, 57600u);
    EXPECT_EQ(capture->GetSettings().Get().serial.parity, serial::parity_odd);
    EXPECT_EQ(capture->GetSession().GetTerminalState()->GetNewLineMode(), values.newlineMode);
    EXPECT_EQ(capture->GetSession().GetLogger().GetBasePath(), values.logPath);
    EXPECT_TRUE(capture->GetSession().GetLogger().GetOptions().Enabled);
    EXPECT_FLOAT_EQ(capture->GetInterfaceZoomFactor(), 1.25f);
}

TEST_F(CaptureUiTest, InvalidSettingsCannotStartAConnection)
{
    auto values = capture->GetSettings().Get();
    values.serial.baudrate = 0;
    EXPECT_FALSE(capture->Connect(values));
    EXPECT_EQ(capture->GetSession().GetState(), State::Disconnected);
    Frame();
    EXPECT_EQ(modal, "Setup");
    EXPECT_EQ(capture->GetSettings().Get().serial.baudrate, 115200u);
}

TEST_F(CaptureUiTest, SaveFailureDoesNotPreventConnectionOrLoseSessionSettings)
{
    auto values = capture->GetSettings().Get();
    values.serial.port = "loopback";
    values.newlineMode = imterm::TerminalState::NewLineMode::AddCrToLf;
    std::filesystem::create_directory("imterm.toml.bak");
    ASSERT_TRUE(capture->Connect(values));
    ASSERT_TRUE(Until(State::Connected));
    EXPECT_FALSE(capture->GetSettings().GetWriteError().empty());
    EXPECT_EQ(capture->GetSession().GetTerminalState()->GetNewLineMode(), values.newlineMode);
    capture->GetSession().Disconnect();
    Frame();
    EXPECT_EQ(modal, "Setup");
}

TEST_F(CaptureUiTest, SetupSaveUsesLoadedSerialAndNewlineValues)
{
    auto values = capture->GetSettings().Get();
    values.serial.port = "loopback";
    values.serial.baudrate = 38400;
    values.serial.bytesize = serial::sevenbits;
    values.serial.stopbits = serial::stopbits_one_point_five;
    values.serial.parity = serial::parity_odd;
    values.serial.flowcontrol = serial::flowcontrol_software;
    values.newlineMode = imterm::TerminalState::NewLineMode::AddCrToLf;
    capture.reset();
    imterm::CaptureSettings settings("imterm.toml");
    settings.Load();
    ASSERT_TRUE(settings.Set(values));
    ASSERT_TRUE(settings.Save());
    capture = std::make_unique<imterm::Capture>([this] { return std::make_unique<UiTransport>(failed); });
    capture->InitializeInterfaceZoom();
    Frame();
    Frame();
    auto* setup = ImGui::FindWindowByName("Setup");
    ASSERT_NE(setup, nullptr);
    ImGui::ActivateItemByID(setup->GetID("Save"));
    Frame();
    ASSERT_TRUE(Until(State::Connected));
    const auto& parameters = *capture->GetSession().GetParameters();
    EXPECT_EQ(parameters.port, "loopback");
    EXPECT_EQ(parameters.baudrate, 38400u);
    EXPECT_EQ(parameters.bytesize, serial::sevenbits);
    EXPECT_EQ(parameters.stopbits, serial::stopbits_one_point_five);
    EXPECT_EQ(parameters.parity, serial::parity_odd);
    EXPECT_EQ(parameters.flowcontrol, serial::flowcontrol_software);
    EXPECT_EQ(capture->GetSession().GetTerminalState()->GetNewLineMode(), values.newlineMode);
}

TEST_F(CaptureUiTest, ZoomChangesPersistWithoutReplacingSerialPreferences)
{
    auto values = capture->GetSettings().Get();
    values.serial.port = "loopback";
    values.serial.baudrate = 57600;
    values.newlineMode = imterm::TerminalState::NewLineMode::AddCrToLf;
    ASSERT_TRUE(capture->Connect(values));
    ASSERT_TRUE(Until(State::Connected));
    capture->QueueInterfaceZoomWheel(1);
    Frame();
    imterm::CaptureSettings reloaded("imterm.toml");
    reloaded.Load();
    EXPECT_FLOAT_EQ(reloaded.Get().zoomFactor, 1.1f);
    EXPECT_EQ(reloaded.Get().serial.baudrate, 57600u);
    EXPECT_EQ(reloaded.Get().serial.port, "loopback");
    EXPECT_EQ(reloaded.Get().newlineMode, values.newlineMode);
}

TEST(CaptureComboTest, ItemsOwnTheirValuesAfterSourceAndListsChange)
{
    std::vector<serial::PortInfo> ports{{"first", "device", "id"}};
    imterm::ComboDataItem<serial::PortInfo> item("device", ports.front());
    ports.front().port = "changed";
    ports.clear();
    ports.shrink_to_fit();
    EXPECT_EQ(item.value.port, "first");
    auto copy = item;
    item.value.port = "another";
    EXPECT_EQ(copy.value.port, "first");
}

#ifdef __linux__
TEST_F(CaptureUiTest, NativePtySmokeAppliesSavedFlowControlNewlinesAndLogDirectory)
{
    struct Master {
        int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        ~Master() { if (fd >= 0) close(fd); }
    } master;
    ASSERT_GE(master.fd, 0);
    ASSERT_EQ(grantpt(master.fd), 0);
    ASSERT_EQ(unlockpt(master.fd), 0);
    auto values = capture->GetSettings().Get();
    values.serial.port = ptsname(master.fd);
    values.serial.stopbits = serial::stopbits_one;
    values.serial.flowcontrol = serial::flowcontrol_software;
    values.newlineMode = imterm::TerminalState::NewLineMode::AddCrToLf;
    values.logging = {true, false, false};
    values.logPath = directory.Path() / "pty logs";
    capture.reset();
    imterm::CaptureSettings settings("imterm.toml");
    settings.Load();
    ASSERT_TRUE(settings.Set(values));
    ASSERT_TRUE(settings.Save());
    capture = std::make_unique<imterm::Capture>();
    capture->InitializeInterfaceZoom();
    ASSERT_TRUE(capture->Connect(capture->GetSettings().Get()));
    ASSERT_TRUE(Until(State::Connected));
    capture->GetSession().GetLogger().SetUsePrefixTimestamp(false);
    const int slave = open(values.serial.port.c_str(), O_RDWR | O_NOCTTY);
    ASSERT_GE(slave, 0);
    termios attributes{};
    const int configured = tcgetattr(slave, &attributes);
    close(slave);
    ASSERT_EQ(configured, 0);
    EXPECT_NE(attributes.c_iflag & IXON, 0u);
    EXPECT_NE(attributes.c_iflag & IXOFF, 0u);
    ASSERT_EQ(write(master.fd, "first\nsecond\n", 13), 13);
    const auto deadline = imterm::CaptureSession::Clock::now() + 2s;
    while (capture->GetSession().GetData()->GetLineCount() < 2
        && imterm::CaptureSession::Clock::now() < deadline) {
        Frame();
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_GE(capture->GetSession().GetData()->GetLineCount(), 2u);
    EXPECT_EQ(imterm::test::LineText(capture->GetSession().GetData()->GetLine(0)), "first");
    EXPECT_EQ(imterm::test::LineText(capture->GetSession().GetData()->GetLine(1)), "second");
    capture->Shutdown();
    const auto log = values.logPath / (std::filesystem::path(values.serial.port).filename().string() + ".log");
    EXPECT_EQ(imterm::test::ReadFile(log), "first\nsecond\n");
}
#endif
} // namespace
