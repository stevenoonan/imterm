#include "capture_session.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace imterm {
namespace {
constexpr size_t QueueLimit = 256 * 1024;
constexpr size_t ChunkSize = 4096;
std::string CurrentError()
{
    try { throw; }
    catch (const serial::IOException& error) {
        std::string message = error.what();
        const auto sourceLocation = message.rfind(", file ");
        if (sourceLocation != std::string::npos) message.erase(sourceLocation);
        return message;
    }
    catch (const std::exception& error) { return error.what(); }
    catch (...) { return "Unknown serial transport failure"; }
}
}

struct CaptureSession::Worker {
    enum class Kind { Connected, Data, Failure, Signals, SignalError };
    struct Event {
        uint64_t generation;
        Kind kind;
        std::vector<uint8_t> bytes;
        std::string error;
        ModemSignals signals;
    };
    struct Request {
        uint64_t generation = 0;
        std::optional<SerialParameters> parameters;
    };

    TransportFactory factory;
    std::mutex mutex;
    std::condition_variable wake;
    Request request;
    std::deque<Event> events;
    size_t receivedBytes = 0;
    std::deque<std::vector<uint8_t>> writes;
    size_t queuedWriteBytes = 0;
    std::optional<bool> dtr;
    std::optional<bool> rts;
    bool stopping = false;
    std::thread thread;

    explicit Worker(TransportFactory make) : factory(std::move(make)), thread([this] { Run(); }) {}
    ~Worker() { Stop(); }

    void Stop()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        if (thread.joinable()) thread.join();
    }

    void RequestConnection(uint64_t generation, std::optional<SerialParameters> parameters)
    {
        {
            std::lock_guard lock(mutex);
            request = {generation, std::move(parameters)};
            writes.clear();
            queuedWriteBytes = 0;
            events.clear();
            receivedBytes = 0;
            dtr.reset();
            rts.reset();
        }
        wake.notify_all();
    }

    bool Write(uint64_t generation, std::span<const uint8_t> bytes)
    {
        std::lock_guard lock(mutex);
        if (stopping || generation != request.generation
            || bytes.size() > QueueLimit - queuedWriteBytes) return false;
        // Limit each transport call, including pastes, so cancellation is prompt.
        for (size_t offset = 0; offset < bytes.size(); offset += ChunkSize) {
            const auto chunk = bytes.subspan(offset, std::min(ChunkSize, bytes.size() - offset));
            writes.emplace_back(chunk.begin(), chunk.end());
        }
        queuedWriteBytes += bytes.size();
        wake.notify_all();
        return true;
    }

    void SetLine(bool isDTR, bool value)
    {
        std::lock_guard lock(mutex);
        (isDTR ? dtr : rts) = value;
        wake.notify_all();
    }

    void Push(Event event)
    {
        std::lock_guard lock(mutex);
        if (event.generation != request.generation || stopping) return;
        if (event.kind == Kind::Signals || event.kind == Kind::SignalError) {
            const auto previous = std::find_if(events.begin(), events.end(), [&](const Event& queued) {
                return queued.kind == event.kind;
            });
            if (previous != events.end()) {
                *previous = std::move(event);
                return;
            }
        }
        receivedBytes += event.bytes.size();
        events.push_back(std::move(event));
    }

    std::deque<Event> TakeEvents()
    {
        std::lock_guard lock(mutex);
        std::deque<Event> result;
        result.swap(events);
        receivedBytes = 0;
        wake.notify_all();
        return result;
    }

    void Run()
    {
        uint64_t generation = 0;
        std::unique_ptr<SerialTransport> transport;
        std::vector<uint8_t> pendingWrite;
        size_t written = 0;
        auto nextSignals = Clock::now();
        bool querySignals = true;
        for (;;) {
            Request desired;
            std::optional<bool> setDTR;
            std::optional<bool> setRTS;
            bool canRead;
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(5), [&] {
                    return stopping || generation != request.generation
                        || !writes.empty() || dtr || rts;
                });
                if (stopping) return;
                desired = request;
                if (generation == desired.generation && transport) {
                    setDTR = dtr;
                    setRTS = rts;
                    dtr.reset();
                    rts.reset();
                    if (pendingWrite.empty() && !writes.empty()) {
                        pendingWrite = std::move(writes.front());
                        writes.pop_front();
                        // Include the in-flight chunk in the queue budget until
                        // it has been written, preventing unbounded backlog.
                        written = 0;
                    }
                }
                else if (generation == desired.generation) {
                    dtr.reset();
                    rts.reset();
                    writes.clear();
                    queuedWriteBytes = 0;
                }
                canRead = receivedBytes <= QueueLimit - ChunkSize;
            }
            if (generation != desired.generation) {
                transport.reset();
                pendingWrite.clear();
                generation = desired.generation;
                if (!desired.parameters) continue;
                try {
                    transport = factory();
                    if (!transport) throw std::runtime_error("No serial transport available");
                    transport->Open(*desired.parameters);
                    Push({generation, Kind::Connected, {}, {}, {}});
                    nextSignals = Clock::now();
                    querySignals = true;
                }
                catch (...) {
                    Push({generation, Kind::Failure, {}, CurrentError(), {}});
                    transport.reset();
                }
                continue;
            }
            if (!transport) continue;
            try {
                // Unsupported modem ioctls do not imply a failed data channel
                // (for example, pseudo-terminals and some USB adapters).
                try {
                    if (setDTR) transport->SetDTR(*setDTR);
                    if (setRTS) transport->SetRTS(*setRTS);
                }
                catch (...) { Push({generation, Kind::SignalError, {}, CurrentError(), {}}); }

                if (!pendingWrite.empty()) {
                    const auto count = transport->Write(std::span(pendingWrite).subspan(written));
                    if (count == 0 || count > pendingWrite.size() - written)
                        throw std::runtime_error("Serial write timed out or returned an invalid count");
                    written += count;
                    if (written == pendingWrite.size()) {
                        std::lock_guard lock(mutex);
                        if (generation == request.generation) queuedWriteBytes -= pendingWrite.size();
                        pendingWrite.clear();
                    }
                }
                if (canRead) {
                    std::array<uint8_t, ChunkSize> buffer;
                    const auto count = transport->Read(buffer);
                    if (count > buffer.size()) throw std::runtime_error("Invalid serial read count");
                    if (count) Push({generation, Kind::Data,
                        std::vector<uint8_t>(buffer.begin(), buffer.begin() + count), {}, {}});
                }
                if (querySignals && Clock::now() >= nextSignals) {
                    try {
                        Push({generation, Kind::Signals, {}, {}, transport->GetSignals()});
                        nextSignals = Clock::now() + std::chrono::milliseconds(250);
                    }
                    catch (...) {
                        Push({generation, Kind::SignalError, {}, CurrentError(), {}});
                        querySignals = false;
                    }
                }
            }
            catch (...) {
                Push({generation, Kind::Failure, {}, CurrentError(), {}});
                transport.reset();
                pendingWrite.clear();
                std::lock_guard lock(mutex);
                if (generation == request.generation) {
                    writes.clear();
                    queuedWriteBytes = 0;
                }
            }
        }
    }
};

CaptureSession::CaptureSession(TransportFactory factory)
{
    TerminalLogger::Options options;
    options.Enabled = false;
    mLogger = std::make_shared<TerminalLogger>(options);
    mData = std::make_shared<TerminalData>(mLogger);
    mTerminal = std::make_shared<TerminalState>(mData, TerminalState::NewLineMode::Strict);
    mWorker = std::make_unique<Worker>(std::move(factory));
}

CaptureSession::~CaptureSession() { Shutdown(); }

void CaptureSession::EndStream()
{
    try { mTerminal->FinishInput(); }
    catch (...) { mError = "Could not finish terminal input: " + CurrentError(); }
    while (mTerminal->TerminalOutputAvailable()) mTerminal->GetTerminalOutput();
    mLogger->Close();
    mSignals.reset();
}

void CaptureSession::StartAttempt()
{
    mAttemptPending = true;
    mWorker->RequestConnection(++mGeneration, mParameters);
}

bool CaptureSession::Connect(const SerialParameters& parameters)
{
    if (mShutdown || IsBusy()) return false;
    Poll();
    EndStream();
    mParameters = parameters;
    mError.clear();
    mSignalError.clear();
    mState = State::Connecting;
    StartAttempt();
    return true;
}

void CaptureSession::Stop(State state)
{
    if (mShutdown) return;
    // Finish bytes from an established stream, but never adopt a queued open
    // success while cancelling: it could reset the previous port's buffer.
    // In-flight and not-yet-adopted work is discarded with its generation.
    if (mState == State::Connected) Poll();
    mWorker->RequestConnection(++mGeneration, std::nullopt);
    mAttemptPending = false;
    EndStream();
    mState = state;
}

void CaptureSession::Disconnect() { Stop(State::Disconnected); }
void CaptureSession::Cancel() { Stop(State::Disconnected); }
void CaptureSession::BeginReconfiguration() { Stop(State::Reconfiguring); }

void CaptureSession::Shutdown()
{
    if (mShutdown) return;
    Stop(State::Disconnected);
    mShutdown = true;
    mWorker->Stop();
    mRawCapture.Stop();
}

void CaptureSession::SetAutoReconnect(bool value)
{
    mAutoReconnect = value;
    if (!value && mState == State::Reconnecting) Cancel();
}

bool CaptureSession::Send(std::span<const uint8_t> bytes)
{
    if (!CanSend()) return false;
    if (mWorker->Write(mGeneration, bytes)) return true;
    mError = "Serial output queue is full; input was not sent";
    return false;
}

void CaptureSession::SetDTR(bool value)
{
    if (!CanSend()) return;
    mDTR = value;
    mDTRRequested = true;
    mWorker->SetLine(true, value);
}
void CaptureSession::SetRTS(bool value)
{
    if (!CanSetRTS()) return;
    mRTS = value;
    mRTSRequested = true;
    mWorker->SetLine(false, value);
}

bool CaptureSession::Poll(Clock::time_point now)
{
    if (mShutdown) return false;
    bool received = false;
    for (auto& event : mWorker->TakeEvents()) {
        if (event.generation != mGeneration) continue;
        switch (event.kind) {
        case Worker::Kind::Connected:
            mAttemptPending = false;
            if (mBufferPort != mParameters->port) {
                mTerminal->ClearContents();
                // Retain session preferences while resetting protocol state.
                const auto mode = mTerminal->GetNewLineMode();
                const auto size = mTerminal->GetViewportSize();
                mTerminal = std::make_shared<TerminalState>(mData, mode);
                mTerminal->SetViewportSize(size.mRows, size.mColumns);
                mBufferPort = mParameters->port;
                mDTR = false;
                mRTS = false;
                mDTRRequested = false;
                mRTSRequested = false;
            }
            // Port paths must not become subdirectories in the log filename.
            mLogger->SetPostfix(std::filesystem::path(mParameters->port).filename().string());
            mError.clear();
            mSignalError.clear();
            mState = State::Connected;
            if (mDTRRequested) mWorker->SetLine(true, mDTR);
            if (mRTSRequested && CanSetRTS()) mWorker->SetLine(false, mRTS);
            break;
        case Worker::Kind::Data:
            mRawCapture.Append(event.bytes);
            try {
                mTerminal->Input(event.bytes);
                received = true;
                while (mTerminal->TerminalOutputAvailable()) Send(mTerminal->GetTerminalOutput());
            }
            catch (...) { mError = "Terminal input error: " + CurrentError(); }
            break;
        case Worker::Kind::Failure: {
            const bool retry = mState == State::Connected || mState == State::Reconnecting;
            mAttemptPending = false;
            EndStream();
            mError = event.error;
            mState = retry && mAutoReconnect ? State::Reconnecting : State::Failed;
            mNextReconnect = now + std::chrono::seconds(1);
            break;
        }
        case Worker::Kind::Signals:
            mSignals = event.signals;
            break;
        case Worker::Kind::SignalError:
            mSignalError = event.error;
            break;
        }
    }
    if (mState == State::Reconnecting && !mAttemptPending && now >= mNextReconnect) StartAttempt();
    return received;
}

} // namespace imterm
