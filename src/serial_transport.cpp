#include "serial_transport.h"

#include <algorithm>
#include <stdexcept>

namespace imterm {
namespace {
class NativeSerialTransport final : public SerialTransport {
public:
    void Open(const SerialParameters& parameters) override
    {
        // Configure before opening. A failed open still has a fully constructed
        // RAII owner, including when the platform rejects the UART parameters.
        mPort.setPort(parameters.port);
        mPort.setBaudrate(parameters.baudrate);
        mPort.setBytesize(parameters.bytesize);
        mPort.setParity(parameters.parity);
        mPort.setStopbits(parameters.stopbits);
        mPort.setFlowcontrol(parameters.flowcontrol);
        auto timeout = serial::Timeout::simpleTimeout(20);
        mPort.setTimeout(timeout);
        mPort.open();
        if (!mPort.isOpen()) throw std::runtime_error("Serial port did not open");
    }

    size_t Read(std::span<uint8_t> buffer) override
    {
        const auto available = mPort.available();
#ifndef _WIN32
        if (available == 0 && !mPort.waitReadable()) return 0;
#endif
        // Read one byte on hangup as well, so the library reports disconnection.
        return mPort.read(buffer.data(), std::min(buffer.size(), std::max(size_t{1}, available)));
    }

    size_t Write(std::span<const uint8_t> bytes) override
    {
        return mPort.write(bytes.data(), bytes.size());
    }
    ModemSignals GetSignals() override { return {mPort.getCTS(), mPort.getDSR(), mPort.getCD()}; }
    void SetDTR(bool value) override { mPort.setDTR(value); }
    void SetRTS(bool value) override { mPort.setRTS(value); }

private:
    serial::Serial mPort;
};
} // namespace

std::unique_ptr<SerialTransport> MakeSerialTransport()
{
    return std::make_unique<NativeSerialTransport>();
}
} // namespace imterm
