#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "interface_zoom.h"
#include "serial_transport.h"
#include "terminal_state.h"

namespace imterm {

struct SettingsValues {
    SerialParameters serial{"COM1", 115200, serial::eightbits, serial::parity_none,
        serial::stopbits_two, serial::flowcontrol_none};
    TerminalState::NewLineMode newlineMode = TerminalState::NewLineMode::Strict;
    TerminalLogger::Options logging{false, true, true};
    std::filesystem::path logPath = TerminalLogger::GetDefaultLogPath();
    float zoomFactor = DefaultInterfaceZoom;
};

class CaptureSettings {
public:
    // Construction has no file I/O. The controller calls Load during startup.
    explicit CaptureSettings(std::filesystem::path path = "imterm.toml");
    void Load();
    bool Save();
    bool Set(const SettingsValues& values);
    const SettingsValues& Get() const { return mValues; }
    const std::vector<std::string>& GetWarnings() const { return mWarnings; }
    const std::string& GetWriteError() const { return mWriteError; }
    static std::optional<uint32_t> ParseBaud(std::string_view text);
    static std::string Validate(const SettingsValues& values);

private:
    std::filesystem::path mPath;
    SettingsValues mValues;
    std::vector<std::string> mWarnings;
    std::string mWriteError;
    bool mFutureVersion = false;
};

} // namespace imterm
