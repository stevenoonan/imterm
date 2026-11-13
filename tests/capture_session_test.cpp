#include <gtest/gtest.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "capture_session.h"
#include "test_support.h"

#ifdef __linux__
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace {
using imterm::CaptureSession;
using State = CaptureSession::State;
using namespace std::chrono_literals;

// A controllable byte loopback. Only the worker touches the transport; tests
// exchange device data through this separately synchronized endpoint.
struct Device {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<uint8_t> incoming;
    std::vector<uint8_t> outgoing;
    std::vector<imterm::SerialParameters> opens;
    int alive = 0;
    int destroyed = 0;
    int reads = 0;
    int openFailures = 0;
    bool readFailure = false;
    bool writeFailure = false;
    bool zeroWrite = false;
    bool signalFailure = false;
    bool blockOpen = false;
    bool openEntered = false;
    bool dtr = false;
    bool rts = false;
    size_t maxWrite = 4096;
    std::thread::id ioThread;

    void Receive(std::string_view bytes)
    {
        std::lock_guard lock(mutex);
        incoming.insert(incoming.end(), bytes.begin(), bytes.end());
    }
};

class LoopbackTransport : public imterm::SerialTransport {
public:
    explicit LoopbackTransport(std::shared_ptr<Device> device) : mDevice(std::move(device))
    {
        std::lock_guard lock(mDevice->mutex);
        ++mDevice->alive;
        mDevice->ioThread = std::this_thread::get_id();
    }
    ~LoopbackTransport() override
    {
        std::lock_guard lock(mDevice->mutex);
        --mDevice->alive;
        ++mDevice->destroyed;
    }
    void Open(const imterm::SerialParameters& parameters) override
    {
        std::unique_lock lock(mDevice->mutex);
        mDevice->opens.push_back(parameters);
        mDevice->openEntered = true;
        mDevice->wake.notify_all();
        mDevice->wake.wait(lock, [&] { return !mDevice->blockOpen; });
        if (mDevice->openFailures > 0) {
            --mDevice->openFailures;
            throw std::runtime_error("open failure");
        }
        mDevice->reads = 0;
        mDevice->dtr = false;
        mDevice->rts = false;
    }
    size_t Read(std::span<uint8_t> buffer) override
    {
        std::lock_guard lock(mDevice->mutex);
        ++mDevice->reads;
        if (mDevice->readFailure) throw std::runtime_error("device disconnected");
        const auto count = std::min(buffer.size(), mDevice->incoming.size());
        for (size_t i = 0; i < count; ++i) {
            buffer[i] = mDevice->incoming.front();
            mDevice->incoming.pop_front();
        }
        return count;
    }
    size_t Write(std::span<const uint8_t> bytes) override
    {
        std::lock_guard lock(mDevice->mutex);
        if (mDevice->writeFailure) throw std::runtime_error("write failure");
        if (mDevice->zeroWrite) return 0;
        const auto count = std::min(bytes.size(), mDevice->maxWrite);
        mDevice->outgoing.insert(mDevice->outgoing.end(), bytes.begin(), bytes.begin() + count);
        return count;
    }
    imterm::ModemSignals GetSignals() override
    {
        std::lock_guard lock(mDevice->mutex);
        if (mDevice->signalFailure) throw std::runtime_error("modem signals unsupported");
        return {true, false, true};
    }
    void SetDTR(bool value) override
    {
        std::lock_guard lock(mDevice->mutex);
        mDevice->dtr = value;
    }
    void SetRTS(bool value) override
    {
        std::lock_guard lock(mDevice->mutex);
        mDevice->rts = value;
    }
private:
    std::shared_ptr<Device> mDevice;
};

class CaptureSessionTest : public testing::Test {
protected:
    std::shared_ptr<Device> device = std::make_shared<Device>();
    CaptureSession session{[this] { return std::make_unique<LoopbackTransport>(device); }};

    void SetUp() override { session.GetTerminalState()->SetViewportSize(24, 80); }

    bool Until(const std::function<bool()>& condition,
        CaptureSession::Clock::time_point now = CaptureSession::Clock::now())
    {
        const auto deadline = CaptureSession::Clock::now() + 2s;
        do {
            session.Poll(now);
            if (condition()) return true;
            std::this_thread::sleep_for(1ms);
        } while (CaptureSession::Clock::now() < deadline);
        return false;
    }
    void Connect(std::string port = "loopback")
    {
        ASSERT_TRUE(session.Connect({port}));
        ASSERT_TRUE(Until([&] { return session.GetState() == State::Connected; }));
    }
    std::string Output()
    {
        std::lock_guard lock(device->mutex);
        return {device->outgoing.begin(), device->outgoing.end()};
    }
    std::string Text() { return imterm::test::LineText(session.GetData()->GetLine(0)); }
    void FailRead()
    {
        std::lock_guard lock(device->mutex);
        device->readFailure = true;
    }
};

TEST_F(CaptureSessionTest, StartupAndDisconnectedActionsAreSafe)
{
    EXPECT_EQ(session.GetState(), State::Disconnected);
    EXPECT_FALSE(session.CanSend());
    EXPECT_FALSE(session.Send(imterm::test::Bytes("x")));
    EXPECT_NE(session.GetData(), nullptr);
    EXPECT_NE(session.GetTerminalState(), nullptr);
    EXPECT_FALSE(session.GetLogger().GetOptions().Enabled);
    session.SetDTR(true);
    session.SetRTS(true);
    session.Cancel();
    session.Disconnect();
    EXPECT_FALSE(session.GetDTR());
    session.BeginReconfiguration();
    EXPECT_EQ(session.GetState(), State::Reconfiguring);
}

TEST_F(CaptureSessionTest, FailedOpenReleasesTransportAndCanBeRetried)
{
    device->openFailures = 1;
    ASSERT_TRUE(session.Connect({"missing"}));
    EXPECT_EQ(session.GetState(), State::Connecting);
    EXPECT_FALSE(session.Connect({"other"}));
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Failed; }));
    EXPECT_EQ(session.GetError(), "open failure");
    EXPECT_FALSE(session.CanSend());
    Connect();
    EXPECT_TRUE(session.GetError().empty());
    session.Shutdown();
    std::lock_guard lock(device->mutex);
    EXPECT_EQ(device->alive, 0);
    EXPECT_EQ(device->destroyed, 2);
}

TEST_F(CaptureSessionTest, LoopbackResponsesAndShortWritesPreserveByteOrder)
{
    device->maxWrite = 1;
    Connect();
    ASSERT_TRUE(session.Send(imterm::test::Bytes("A\xC3\xA9")));
    ASSERT_TRUE(Until([&] { return Output() == "A\xC3\xA9"; }));
    device->Receive("hello\x1b[5n\x1b[6n");
    ASSERT_TRUE(Until([&] { return Output() == "A\xC3\xA9\x1b[0n\x1b[1;6R"; }));
    EXPECT_EQ(Text(), "hello");
    EXPECT_NE(device->ioThread, std::this_thread::get_id());
}

TEST_F(CaptureSessionTest, ManualDisconnectFinishesUtf8AndNeverReconnects)
{
    Connect();
    device->Receive("keep\xE2\x82");
    ASSERT_TRUE(Until([&] { return Text() == "keep"; }));
    session.Disconnect();
    EXPECT_EQ(session.GetState(), State::Disconnected);
    EXPECT_EQ(Text(), "keep\xEF\xBF\xBD\xEF\xBF\xBD");
    session.Poll(CaptureSession::Clock::now() + 10s);
    EXPECT_EQ(session.GetState(), State::Disconnected);
    EXPECT_FALSE(session.GetSignals());
    Connect();
    EXPECT_EQ(Text(), "keep\xEF\xBF\xBD\xEF\xBF\xBD");
}

TEST_F(CaptureSessionTest, AutomaticReconnectPreservesDataAndUsesRetryDelay)
{
    Connect();
    device->Receive("keep");
    ASSERT_TRUE(Until([&] { return Text() == "keep"; }));
    FailRead();
    const auto now = CaptureSession::Clock::now();
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Reconnecting; }, now));
    EXPECT_FALSE(session.CanSend());
    session.Poll(now + 999ms);
    {
        std::lock_guard lock(device->mutex);
        EXPECT_EQ(device->opens.size(), 1U);
        device->readFailure = false;
        device->openFailures = 1;
    }
    // A failed retry stays in reconnecting and waits another interval.
    session.Poll(now + 1s);
    ASSERT_TRUE(Until([&] { return !session.GetError().empty() && session.GetError() == "open failure"; }, now + 1s));
    session.Poll(now + 2s);
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Connected; }, now + 2s));
    EXPECT_EQ(Text(), "keep");
}

TEST_F(CaptureSessionTest, DisablingReconnectTurnsIoFailureIntoFailedState)
{
    session.SetAutoReconnect(false);
    Connect();
    FailRead();
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Failed; }));
    EXPECT_EQ(session.GetError(), "device disconnected");
    session.Poll(CaptureSession::Clock::now() + 10s);
    EXPECT_EQ(session.GetState(), State::Failed);
}

TEST_F(CaptureSessionTest, CancelReconnectAndDisablingReconnectPreventFurtherAttempts)
{
    Connect();
    FailRead();
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Reconnecting; }));
    session.SetAutoReconnect(false);
    EXPECT_EQ(session.GetState(), State::Disconnected);
    session.Poll(CaptureSession::Clock::now() + 10s);
    session.Shutdown();
    EXPECT_EQ(device->opens.size(), 1U);
}

TEST_F(CaptureSessionTest, CancelInFlightOpenDiscardsStaleSuccessAndInput)
{
    {
        std::lock_guard lock(device->mutex);
        device->blockOpen = true;
    }
    ASSERT_TRUE(session.Connect({"slow"}));
    {
        std::unique_lock lock(device->mutex);
        ASSERT_TRUE(device->wake.wait_for(lock, 2s, [&] { return device->openEntered; }));
    }
    const auto start = CaptureSession::Clock::now();
    session.Cancel();
    EXPECT_LT(CaptureSession::Clock::now() - start, 100ms);
    EXPECT_EQ(session.GetState(), State::Disconnected);
    {
        std::lock_guard lock(device->mutex);
        device->blockOpen = false;
    }
    device->wake.notify_all();
    session.Shutdown();
    EXPECT_EQ(session.GetState(), State::Disconnected);
    EXPECT_EQ(Text(), "");
    EXPECT_EQ(device->alive, 0);
}

TEST_F(CaptureSessionTest, RepeatedReconfigurationPreservesSamePortAndResetsChangedPort)
{
    Connect();
    device->Receive("keep");
    ASSERT_TRUE(Until([&] { return Text() == "keep"; }));
    auto data = session.GetData();
    auto terminal = session.GetTerminalState();
    for (int i = 0; i < 30; ++i) {
        session.BeginReconfiguration();
        EXPECT_EQ(session.GetState(), State::Reconfiguring);
        imterm::SerialParameters parameters{"loopback"};
        parameters.baudrate = 9600 + i;
        parameters.flowcontrol = serial::flowcontrol_software;
        ASSERT_TRUE(session.Connect(parameters));
        ASSERT_TRUE(Until([&] { return session.CanSend(); }));
        EXPECT_EQ(Text(), "keep");
        EXPECT_EQ(session.GetTerminalState(), terminal);
    }
    session.BeginReconfiguration();
    Connect("different");
    EXPECT_EQ(Text(), "");
    EXPECT_EQ(session.GetData(), data);
    EXPECT_NE(session.GetTerminalState(), terminal);
    session.Shutdown();
    EXPECT_EQ(device->alive, 0);
    EXPECT_EQ(device->destroyed, 32);
    EXPECT_EQ(device->opens[1].flowcontrol, serial::flowcontrol_software);
}

TEST_F(CaptureSessionTest, CancelQueuedOpenSuccessDoesNotAdoptAnotherPortsBuffer)
{
    Connect();
    device->Receive("keep");
    ASSERT_TRUE(Until([&] { return Text() == "keep"; }));
    const auto terminal = session.GetTerminalState();
    ASSERT_TRUE(session.Connect({"different"}));
    // Wait until the worker has published success and entered the read loop,
    // without allowing the UI to consume that queued success.
    const auto deadline = CaptureSession::Clock::now() + 2s;
    bool openCompleted = false;
    while (!openCompleted && CaptureSession::Clock::now() < deadline) {
        {
            std::lock_guard lock(device->mutex);
            openCompleted = device->opens.size() == 2 && device->reads > 2;
        }
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_TRUE(openCompleted);
    session.Cancel();
    EXPECT_EQ(session.GetState(), State::Disconnected);
    EXPECT_EQ(Text(), "keep");
    EXPECT_EQ(session.GetTerminalState(), terminal);
}

TEST_F(CaptureSessionTest, FailedPortChangeKeepsPreviousBuffer)
{
    Connect();
    device->Receive("keep");
    ASSERT_TRUE(Until([&] { return Text() == "keep"; }));
    {
        std::lock_guard lock(device->mutex);
        device->openFailures = 1;
    }
    ASSERT_TRUE(session.Connect({"missing"}));
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Failed; }));
    EXPECT_EQ(Text(), "keep");
    Connect();
    EXPECT_EQ(Text(), "keep");
}

TEST_F(CaptureSessionTest, WriteFailureAndTimeoutUseConnectionFailurePolicy)
{
    session.SetAutoReconnect(false);
    Connect();
    {
        std::lock_guard lock(device->mutex);
        device->writeFailure = true;
    }
    ASSERT_TRUE(session.Send(imterm::test::Bytes("x")));
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Failed; }));
    EXPECT_EQ(session.GetError(), "write failure");
    {
        std::lock_guard lock(device->mutex);
        device->writeFailure = false;
        device->zeroWrite = true;
    }
    Connect();
    ASSERT_TRUE(session.Send(imterm::test::Bytes("x")));
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Failed; }));
    EXPECT_NE(session.GetError().find("timed out"), std::string::npos);
}

TEST_F(CaptureSessionTest, OutputBacklogIsBoundedAndReported)
{
    Connect();
    EXPECT_FALSE(session.Send(std::vector<uint8_t>(256 * 1024 + 1, 'x')));
    EXPECT_NE(session.GetError().find("queue is full"), std::string::npos);
    EXPECT_EQ(session.GetState(), State::Connected);
}

TEST_F(CaptureSessionTest, ModemLinesAndCachedSignalsAreWorkerOwned)
{
    Connect();
    ASSERT_TRUE(Until([&] { return session.GetSignals().has_value(); }));
    EXPECT_TRUE(session.GetSignals()->cts);
    EXPECT_FALSE(session.GetSignals()->dsr);
    EXPECT_TRUE(session.GetSignals()->dcd);
    session.SetDTR(true);
    session.SetRTS(true);
    ASSERT_TRUE(Until([&] {
        std::lock_guard lock(device->mutex);
        return device->dtr && device->rts;
    }));
    session.Disconnect();
    EXPECT_FALSE(session.GetSignals());
}

TEST_F(CaptureSessionTest, UnsupportedSignalsDoNotDisconnectDataChannel)
{
    device->signalFailure = true;
    Connect();
    ASSERT_TRUE(Until([&] { return !session.GetSignalError().empty(); }));
    device->Receive("data");
    ASSERT_TRUE(Until([&] { return Text() == "data"; }));
    EXPECT_EQ(session.GetState(), State::Connected);
    EXPECT_FALSE(session.GetSignals());
}

TEST_F(CaptureSessionTest, RequestedModemLinesAreRestoredAfterReconnect)
{
    Connect();
    session.SetDTR(true);
    session.SetRTS(true);
    ASSERT_TRUE(Until([&] {
        std::lock_guard lock(device->mutex);
        return device->dtr && device->rts;
    }));
    FailRead();
    const auto now = CaptureSession::Clock::now();
    ASSERT_TRUE(Until([&] { return session.GetState() == State::Reconnecting; }, now));
    {
        std::lock_guard lock(device->mutex);
        device->readFailure = false;
    }
    session.Poll(now + 1s);
    ASSERT_TRUE(Until([&] {
        std::lock_guard lock(device->mutex);
        return session.CanSend() && device->dtr && device->rts;
    }, now + 1s));
    Connect("different");
    EXPECT_FALSE(session.GetDTR());
    EXPECT_FALSE(session.GetRTS());
}

TEST_F(CaptureSessionTest, HardwareFlowControlOwnsRts)
{
    imterm::SerialParameters parameters{"loopback"};
    parameters.flowcontrol = serial::flowcontrol_hardware;
    ASSERT_TRUE(session.Connect(parameters));
    ASSERT_TRUE(Until([&] { return session.CanSend(); }));
    EXPECT_FALSE(session.CanSetRTS());
    session.SetRTS(true);
    EXPECT_FALSE(session.GetRTS());
    session.Shutdown();
    EXPECT_FALSE(device->rts);
    EXPECT_EQ(device->opens[0].flowcontrol, serial::flowcontrol_hardware);
}

TEST_F(CaptureSessionTest, PortChangeClearsProtocolFormattingAndPreservesPreferences)
{
    Connect();
    session.GetTerminalState()->SetNewLineMode(imterm::TerminalState::NewLineMode::AddCrToLf);
    session.autoScroll = false;
    device->Receive("\x1b[1mold");
    ASSERT_TRUE(Until([&] { return session.GetTerminalState()->IsBold(); }));
    Connect("different");
    EXPECT_FALSE(session.GetTerminalState()->IsBold());
    EXPECT_EQ(session.GetTerminalState()->GetViewportSize().mColumns, 80);
    EXPECT_EQ(session.GetTerminalState()->GetNewLineMode(), imterm::TerminalState::NewLineMode::AddCrToLf);
    EXPECT_FALSE(session.autoScroll);
    EXPECT_EQ(Text(), "");
}

TEST_F(CaptureSessionTest, ShutdownFlushesLogsAndRawBytesAndReleasesTransport)
{
    imterm::test::TemporaryDirectory directory;
    auto& logger = session.GetLogger();
    logger.SetUsePrefixTimestamp(false);
    logger.SetBasePath(directory.Path());
    logger.SetOptions({true, false, false});
    ASSERT_TRUE(session.GetRawCapture().Start(directory.Path() / "raw.bin"));
    Connect();
    device->Receive("complete\r\npartial\xFF\xE2");
    ASSERT_TRUE(Until([&] { return session.GetData()->GetLineCount() == 2; }));
    session.Shutdown();
    session.Shutdown();
    EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "raw.bin"), "complete\r\npartial\xFF\xE2");
    EXPECT_EQ(imterm::test::ReadFile(directory.Path() / "loopback.log"),
        "complete\npartial\xEF\xBF\xBD\xEF\xBF\xBD\n");
    EXPECT_EQ(device->alive, 0);
    EXPECT_FALSE(session.Connect({"loopback"}));
    EXPECT_FALSE(session.Send(imterm::test::Bytes("x")));
}

TEST(CaptureSessionLifetimeTest, DestructorJoinsWorkerAndDestroysTransport)
{
    auto device = std::make_shared<Device>();
    {
        CaptureSession session([&] { return std::make_unique<LoopbackTransport>(device); });
        ASSERT_TRUE(session.Connect({"loopback"}));
        const auto deadline = CaptureSession::Clock::now() + 2s;
        while (!session.CanSend() && CaptureSession::Clock::now() < deadline) {
            session.Poll();
            std::this_thread::sleep_for(1ms);
        }
        ASSERT_TRUE(session.CanSend());
    }
    EXPECT_EQ(device->alive, 0);
    EXPECT_EQ(device->destroyed, 1);
}

#ifdef __linux__
TEST(CaptureSessionLoopbackTest, NativeTransportDetectsHangupAndReconnectsToReplacementPty)
{
    struct Master {
        int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        ~Master() { if (fd >= 0) close(fd); }
    } first, second;
    ASSERT_GE(first.fd, 0);
    ASSERT_GE(second.fd, 0);
    ASSERT_EQ(grantpt(first.fd), 0);
    ASSERT_EQ(unlockpt(first.fd), 0);
    ASSERT_EQ(grantpt(second.fd), 0);
    ASSERT_EQ(unlockpt(second.fd), 0);
    imterm::test::TemporaryDirectory directory;
    const auto port = directory.Path() / "serial";
    std::filesystem::create_symlink(ptsname(first.fd), port);
    CaptureSession session;
    session.GetTerminalState()->SetViewportSize(24, 80);
    const auto now = CaptureSession::Clock::now();
    const auto until = [&](const std::function<bool()>& condition, auto time) {
        const auto deadline = CaptureSession::Clock::now() + 2s;
        do {
            session.Poll(time);
            if (condition()) return true;
            std::this_thread::sleep_for(1ms);
        } while (CaptureSession::Clock::now() < deadline);
        return false;
    };
    ASSERT_TRUE(session.Connect({port.string()}));
    ASSERT_TRUE(until([&] { return session.CanSend(); }, now));
    ASSERT_EQ(write(first.fd, "keep", 4), 4);
    ASSERT_TRUE(until([&] { return session.GetData()->GetLineSize(0) == 4; }, now));
    close(first.fd);
    first.fd = -1;
    ASSERT_TRUE(until([&] { return session.GetState() == State::Reconnecting; }, now));
    std::filesystem::remove(port);
    std::filesystem::create_symlink(ptsname(second.fd), port);
    ASSERT_TRUE(until([&] { return session.CanSend(); }, now + 1s));
    ASSERT_EQ(write(second.fd, " more", 5), 5);
    ASSERT_TRUE(until([&] { return session.GetData()->GetLineSize(0) == 9; }, now + 1s));
    EXPECT_EQ(imterm::test::LineText(session.GetData()->GetLine(0)), "keep more");
}

TEST(CaptureSessionLoopbackTest, NativeFailedConfigurationDoesNotLeakFileDescriptors)
{
    const auto descriptorCount = [] {
        return static_cast<size_t>(std::distance(
            std::filesystem::directory_iterator("/proc/self/fd"),
            std::filesystem::directory_iterator{}));
    };
    imterm::test::TemporaryDirectory directory;
    const auto path = directory.Path() / "not-a-serial-port";
    std::ofstream(path).put('x');
    const auto before = descriptorCount();
    for (int i = 0; i < 10; ++i) {
        auto transport = imterm::MakeSerialTransport();
        EXPECT_THROW(transport->Open({path.string()}), std::exception);
    }
    EXPECT_EQ(descriptorCount(), before);
}

TEST(CaptureSessionLoopbackTest, NativeTransportConnectsTransfersAndReconfiguresOnPty)
{
    struct Master {
        int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        ~Master() { if (fd >= 0) close(fd); }
    } master;
    ASSERT_GE(master.fd, 0);
    ASSERT_EQ(grantpt(master.fd), 0);
    ASSERT_EQ(unlockpt(master.fd), 0);
    imterm::SerialParameters parameters{ptsname(master.fd)};
    CaptureSession session;
    session.GetTerminalState()->SetViewportSize(24, 80);
    const auto until = [&](const std::function<bool()>& condition) {
        const auto deadline = CaptureSession::Clock::now() + 2s;
        do {
            session.Poll();
            if (condition()) return true;
            std::this_thread::sleep_for(1ms);
        } while (CaptureSession::Clock::now() < deadline);
        return false;
    };
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(session.Connect(parameters));
        ASSERT_TRUE(until([&] { return session.CanSend(); })) << session.GetError();
        ASSERT_EQ(write(master.fd, "hello\r\n\x1b[5n", 11), 11);
        ASSERT_TRUE(session.Send(imterm::test::Bytes("\xC3\xA9")));
        std::string output;
        ASSERT_TRUE(until([&] {
            char buffer[64];
            const auto count = read(master.fd, buffer, sizeof(buffer));
            if (count > 0) output.append(buffer, static_cast<size_t>(count));
            return output.size() >= 6;
        }));
        EXPECT_EQ(output, "\xC3\xA9\x1b[0n");
        session.Disconnect();
        EXPECT_EQ(session.GetState(), State::Disconnected);
    }
    EXPECT_EQ(imterm::test::LineText(session.GetData()->GetLine(0)), "hello");
    session.Shutdown();
}
#endif
} // namespace
