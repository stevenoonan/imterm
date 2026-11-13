#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "raw_serial_capture.h"
#include "serial_transport.h"
#include "terminal_state.h"

namespace imterm {

// Main-thread API. The worker owns the transport; Poll is the only boundary
// where received bytes touch terminal data, logging, or other view-facing state.
class CaptureSession {
public:
    enum class State { Disconnected, Connecting, Connected, Reconnecting, Reconfiguring, Failed };
    using TransportFactory = std::function<std::unique_ptr<SerialTransport>()>;
    using Clock = std::chrono::steady_clock;

    explicit CaptureSession(TransportFactory factory = MakeSerialTransport);
    ~CaptureSession();
    CaptureSession(const CaptureSession&) = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;

    bool Connect(const SerialParameters& parameters);
    void Disconnect();
    void Cancel();
    void BeginReconfiguration();
    void Shutdown();
    // Returns true when terminal text was received this turn.
    bool Poll(Clock::time_point now = Clock::now());
    bool Send(std::span<const uint8_t> bytes);
    void SetDTR(bool value);
    void SetRTS(bool value);
    void SetAutoReconnect(bool value);

    State GetState() const { return mState; }
    bool CanSend() const { return !mShutdown && mState == State::Connected; }
    bool CanSetRTS() const { return CanSend() && mParameters->flowcontrol != serial::flowcontrol_hardware; }
    bool IsBusy() const { return mState == State::Connecting || mState == State::Reconnecting; }
    bool GetAutoReconnect() const { return mAutoReconnect; }
    const std::optional<SerialParameters>& GetParameters() const { return mParameters; }
    const std::string& GetError() const { return mError; }
    const std::string& GetSignalError() const { return mSignalError; }
    const std::optional<ModemSignals>& GetSignals() const { return mSignals; }
    bool GetDTR() const { return mDTR; }
    bool GetRTS() const { return mRTS; }
    std::shared_ptr<TerminalData> GetData() const { return mData; }
    std::shared_ptr<TerminalState> GetTerminalState() const { return mTerminal; }
    TerminalLogger& GetLogger() { return *mLogger; }
    RawSerialCapture& GetRawCapture() { return mRawCapture; }

    // UI preferences belong to this session, even before the first connection.
    bool autoScroll = true;

private:
    struct Worker;
    std::shared_ptr<TerminalLogger> mLogger;
    std::shared_ptr<TerminalData> mData;
    std::shared_ptr<TerminalState> mTerminal;
    RawSerialCapture mRawCapture;
    std::unique_ptr<Worker> mWorker;
    State mState = State::Disconnected;
    std::optional<SerialParameters> mParameters;
    std::string mBufferPort;
    std::string mError;
    std::string mSignalError;
    std::optional<ModemSignals> mSignals;
    bool mAutoReconnect = true;
    bool mDTR = false;
    bool mRTS = false;
    bool mDTRRequested = false;
    bool mRTSRequested = false;
    bool mShutdown = false;
    bool mAttemptPending = false;
    uint64_t mGeneration = 0;
    Clock::time_point mNextReconnect{};

    void StartAttempt();
    void EndStream();
    void Stop(State state);
};

} // namespace imterm
