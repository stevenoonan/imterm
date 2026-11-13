#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include "serial/serial.h"

namespace imterm {

struct SerialParameters {
    std::string port;
    uint32_t baudrate = 115200;
    serial::bytesize_t bytesize = serial::eightbits;
    serial::parity_t parity = serial::parity_none;
    serial::stopbits_t stopbits = serial::stopbits_one;
    serial::flowcontrol_t flowcontrol = serial::flowcontrol_none;
};

struct ModemSignals {
    bool cts = false;
    bool dsr = false;
    bool dcd = false;
};

// All methods, including destruction, run on the session's I/O worker. Reads
// and writes must return within a bounded timeout. Errors may throw.
class SerialTransport {
public:
    virtual ~SerialTransport() = default;
    virtual void Open(const SerialParameters& parameters) = 0;
    virtual size_t Read(std::span<uint8_t> buffer) = 0;
    virtual size_t Write(std::span<const uint8_t> bytes) = 0;
    virtual ModemSignals GetSignals() = 0;
    virtual void SetDTR(bool value) = 0;
    virtual void SetRTS(bool value) = 0;
};

std::unique_ptr<SerialTransport> MakeSerialTransport();

} // namespace imterm
