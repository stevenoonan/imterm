#include "capture_settings.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

#include "toml.hpp"

namespace imterm {
namespace {
constexpr int64_t Version = 1;
constexpr size_t MaxTextLength = 4096;
using Newline = TerminalState::NewLineMode;

template<typename T>
using Choices = std::initializer_list<std::pair<std::string_view, T>>;
const Choices<serial::bytesize_t> DataBits{{"5", serial::fivebits}, {"6", serial::sixbits},
    {"7", serial::sevenbits}, {"8", serial::eightbits}};
const Choices<serial::stopbits_t> StopBits{{"1", serial::stopbits_one},
    {"1.5", serial::stopbits_one_point_five}, {"2", serial::stopbits_two}};
const Choices<serial::parity_t> Parity{{"none", serial::parity_none}, {"odd", serial::parity_odd},
    {"even", serial::parity_even}, {"mark", serial::parity_mark}, {"space", serial::parity_space}};
const Choices<serial::flowcontrol_t> Flow{{"none", serial::flowcontrol_none},
    {"hardware", serial::flowcontrol_hardware}, {"software", serial::flowcontrol_software}};
const Choices<Newline> Newlines{{"strict", Newline::Strict},
    {"add_cr_to_lf", Newline::AddCrToLf}, {"add_lf_to_cr", Newline::AddLfToCr}};

bool ValidText(std::string_view text)
{
    return !text.empty() && text.size() <= MaxTextLength
        && text.find_first_not_of(' ') != std::string_view::npos
        && std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

bool ValidLogPath(const std::filesystem::path& path)
{
    if (!ValidText(path.string())) return false;
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    // Nonexistent directories are created by the logger when data arrives.
    if (error == std::errc::no_such_file_or_directory) return true;
    return !error && (!std::filesystem::exists(status) || std::filesystem::is_directory(status));
}

std::string Normalize(std::string text)
{
    for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == ' ') c = '_';
    }
    return text;
}

template<typename T>
std::string Name(T value, Choices<T> choices)
{
    for (const auto& [name, candidate] : choices) if (value == candidate) return std::string(name);
    return {};
}

// Legacy files used strings for numbers and human-readable enum labels.
std::optional<std::string> ScalarText(toml::node_view<const toml::node> node)
{
    if (auto text = node.value<std::string>()) return text;
    if (node.is_integer()) return std::to_string(*node.value<int64_t>());
    if (auto number = node.value<double>(); number && *number == 1.5) return "1.5";
    return std::nullopt;
}

template<typename T>
void ReadChoice(toml::node_view<const toml::node> node, const char* key,
    T& value, Choices<T> choices, std::vector<std::string>& warnings)
{
    if (auto text = ScalarText(node)) {
        const auto normalized = Normalize(*text);
        for (const auto& [name, candidate] : choices) {
            if (normalized == name) { value = candidate; return; }
        }
    }
    warnings.push_back(std::string(key) + " is missing or invalid; using the default. Review Setup and Save.");
}

// A private sibling directory reserves the temporary filename across processes.
struct StagingDirectory {
    std::filesystem::path path;
    explicit StagingDirectory(const std::filesystem::path& destination) {
        static std::atomic<uint64_t> sequence{0};
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = destination;
            path += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "-" + std::to_string(sequence++);
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("Could not reserve a temporary settings file");
    }
    ~StagingDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
}

CaptureSettings::CaptureSettings(std::filesystem::path path) : mPath(std::move(path)) {}

std::optional<uint32_t> CaptureSettings::ParseBaud(std::string_view text)
{
    uint32_t value = 0;
    if (text.empty() || text.size() > 10) return std::nullopt;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
        return std::nullopt;
    return value;
}

std::string CaptureSettings::Validate(const SettingsValues& values)
{
    if (!ValidText(values.serial.port)) return "Choose a serial port (1-4096 characters, without control characters).";
    if (values.serial.baudrate == 0) return "Baud must be a whole number from 1 to 4294967295.";
    if (Name(values.serial.bytesize, DataBits).empty()) return "Data bits must be 5, 6, 7, or 8.";
    if (Name(values.serial.stopbits, StopBits).empty()) return "Stop bits must be 1, 1.5, or 2.";
    if (Name(values.serial.parity, Parity).empty()) return "Choose a supported parity mode.";
    if (Name(values.serial.flowcontrol, Flow).empty()) return "Choose a supported flow-control mode.";
    if (Name(values.newlineMode, Newlines).empty()) return "Choose a supported newline mode.";
    if (!ValidLogPath(values.logPath)) return "Choose a log directory (1-4096 characters, without control characters); an existing path must be a directory.";
    if (!std::isfinite(values.zoomFactor) || values.zoomFactor <= 0) return "Zoom must be a positive finite number.";
    return {};
}

bool CaptureSettings::Set(const SettingsValues& values)
{
    mWriteError = Validate(values);
    if (!mWriteError.empty()) return false;
    mValues = values;
    mValues.zoomFactor = NormalizeInterfaceZoom(values.zoomFactor);
    return true;
}

void CaptureSettings::Load()
{
    mValues = SettingsValues{};
    mWarnings.clear();
    mWriteError.clear();
    mFutureVersion = false;
    try {
        if (!std::filesystem::exists(mPath)) {
            auto backup = mPath;
            backup += ".bak";
            if (std::filesystem::is_regular_file(backup)) {
                std::filesystem::copy_file(backup, mPath);
                Load();
                mWarnings.push_back("Recovered " + mPath.string()
                    + " from its .bak copy after an interrupted save. Review Setup and Save.");
            }
            else Save();
            return;
        }
        if (std::filesystem::file_size(mPath) > 1024 * 1024)
            throw std::runtime_error("Configuration exceeds the 1 MiB size limit");
        const auto table = toml::parse_file(mPath.string());
        const auto version = table["version"];
        if (version && (!version.is_integer() || version.value<int64_t>().value_or(0) != Version)) {
            mFutureVersion = version.value<int64_t>().value_or(0) > Version;
            mWarnings.push_back("Unsupported configuration version in " + mPath.string()
                + "; using defaults. Move the file aside or use a supported version, then restart before saving.");
            return;
        }
        const auto root = table["default"];
        const auto serial = root["serial"];
        const auto warn = [&](const char* key) {
            mWarnings.push_back(std::string(key) + " is missing or invalid; using the default. Review Setup and Save.");
        };
        if (auto port = serial["serial_port"].value<std::string>(); port && ValidText(*port))
            mValues.serial.port = *port;
        else warn("serial.serial_port");
        if (auto text = ScalarText(serial["baud"]); text && ParseBaud(*text))
            mValues.serial.baudrate = *ParseBaud(*text);
        else warn("serial.baud");
        ReadChoice(serial["data_bits"], "serial.data_bits", mValues.serial.bytesize, DataBits, mWarnings);
        ReadChoice(serial["stop_bits"], "serial.stop_bits", mValues.serial.stopbits, StopBits, mWarnings);
        ReadChoice(serial["parity"], "serial.parity", mValues.serial.parity, Parity, mWarnings);
        ReadChoice(serial["flow_control"], "serial.flow_control", mValues.serial.flowcontrol, Flow, mWarnings);
        ReadChoice(root["input"]["new_line_mode"], "input.new_line_mode", mValues.newlineMode, Newlines, mWarnings);
        const auto logging = root["logging"];
        const auto readBool = [&](const char* key, bool& destination) {
            if (logging[key].is_boolean()) destination = *logging[key].value<bool>();
            else if (logging || version) warn(key);
        };
        readBool("enabled", mValues.logging.Enabled);
        readBool("line_numbers", mValues.logging.LineNumbers);
        readBool("timestamps", mValues.logging.TimeStamps);
        if (auto path = logging["path"].value<std::string>(); path && ValidLogPath(*path))
            mValues.logPath = *path;
        else if (logging || version) warn("logging.path");
        if (auto zoom = root["display"]["zoom_factor"].value<double>();
            zoom && std::isfinite(*zoom) && *zoom > 0 && *zoom <= std::numeric_limits<float>::max())
            mValues.zoomFactor = NormalizeInterfaceZoom(static_cast<float>(*zoom));
        else if (root["display"] || version) warn("display.zoom_factor");
    }
    catch (const std::exception& error) {
        mValues = SettingsValues{};
        mWarnings.push_back("Could not load " + mPath.string() + ": " + error.what()
            + ". Using defaults; review Setup and Save to replace the file (a .bak copy is retained).");
    }
}

bool CaptureSettings::Save()
{
    mWriteError.clear();
    if (mFutureVersion) {
        mWriteError = "Settings were not saved: " + mPath.string()
            + " uses a newer version. Move it aside and restart before saving; current settings still work for this session.";
        return false;
    }
    try {
        const auto invalid = Validate(mValues);
        if (!invalid.empty()) throw std::runtime_error(invalid);
        const auto& serial = mValues.serial;
        const toml::table table{
            {"version", Version},
            {"default", toml::table{
                {"serial", toml::table{{"serial_port", serial.port}, {"baud", int64_t(serial.baudrate)},
                    {"data_bits", int64_t(serial.bytesize)}, {"stop_bits", Name(serial.stopbits, StopBits)},
                    {"parity", Name(serial.parity, Parity)}, {"flow_control", Name(serial.flowcontrol, Flow)}}},
                {"input", toml::table{{"new_line_mode", Name(mValues.newlineMode, Newlines)}}},
                {"logging", toml::table{{"enabled", mValues.logging.Enabled},
                    {"line_numbers", mValues.logging.LineNumbers}, {"timestamps", mValues.logging.TimeStamps},
                    {"path", mValues.logPath.string()}}},
                {"display", toml::table{{"zoom_factor", double(mValues.zoomFactor)}}}
            }}
        };
        StagingDirectory staging(mPath);
        const auto temporary = staging.path / "settings.toml";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output << table << '\n';
            output.flush();
            output.close();
        }
        auto backup = mPath;
        backup += ".bak";
        const bool existed = std::filesystem::exists(mPath);
        if (existed) {
            if (!std::filesystem::is_regular_file(mPath))
                throw std::runtime_error("The configuration path is not a regular file");
            std::filesystem::copy_file(mPath, backup, std::filesystem::copy_options::overwrite_existing);
        }
#ifdef _WIN32
        // Windows rename cannot replace an existing file. Keep the .bak copy
        // recoverable throughout the replacement and restore it on failure.
        if (existed) std::filesystem::remove(mPath);
        try { std::filesystem::rename(temporary, mPath); }
        catch (...) {
            if (existed) {
                std::error_code ignored;
                std::filesystem::copy_file(backup, mPath, std::filesystem::copy_options::overwrite_existing, ignored);
            }
            throw;
        }
#else
        std::filesystem::rename(temporary, mPath);
#endif
        mWarnings.clear();
        return true;
    }
    catch (const std::exception& error) {
        mWriteError = "Could not save " + mPath.string() + ": " + error.what()
            + ". Check the directory permissions and available space; current settings still work for this session.";
        return false;
    }
}

} // namespace imterm
