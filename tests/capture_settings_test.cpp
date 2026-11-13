#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "capture_settings.h"
#include "test_support.h"
#include "toml.hpp"

namespace {
using imterm::CaptureSettings;
using Newline = imterm::TerminalState::NewLineMode;

class CaptureSettingsTest : public testing::Test {
protected:
    imterm::test::TemporaryDirectory directory;
    std::filesystem::path path = directory.Path() / "settings.toml";
    CaptureSettings settings{path};
    void Write(const std::string& text) { std::ofstream(path) << text; }
};

TEST_F(CaptureSettingsTest, ExplicitLoadCreatesValidDefaults)
{
    EXPECT_FALSE(std::filesystem::exists(path));
    settings.Load();
    EXPECT_TRUE(settings.GetWarnings().empty());
    EXPECT_TRUE(settings.GetWriteError().empty());
    EXPECT_TRUE(CaptureSettings::Validate(settings.Get()).empty());
    const auto table = toml::parse_file(path.string());
    EXPECT_EQ(table["version"].value<int>(), 1);
    EXPECT_EQ(settings.Get().serial.port, "COM1");
    EXPECT_EQ(settings.Get().serial.baudrate, 115200u);
    EXPECT_EQ(settings.Get().serial.bytesize, serial::eightbits);
    EXPECT_EQ(settings.Get().serial.stopbits, serial::stopbits_two);
    EXPECT_EQ(settings.Get().serial.parity, serial::parity_none);
    EXPECT_EQ(settings.Get().serial.flowcontrol, serial::flowcontrol_none);
    EXPECT_EQ(settings.Get().newlineMode, Newline::Strict);
    EXPECT_FALSE(settings.Get().logging.Enabled);
    EXPECT_EQ(settings.Get().logPath, imterm::TerminalLogger::GetDefaultLogPath());
    EXPECT_FLOAT_EQ(settings.Get().zoomFactor, 1.0f);
}

TEST_F(CaptureSettingsTest, MalformedTomlUsesDefaultsAndPreservesOriginalUntilSave)
{
    const std::string original = "[default.serial\nbaud = garbage\n";
    Write(original);
    EXPECT_NO_THROW(settings.Load());
    EXPECT_FALSE(settings.GetWarnings().empty());
    EXPECT_EQ(settings.Get().serial.baudrate, 115200u);
    EXPECT_EQ(imterm::test::ReadFile(path), original);
    ASSERT_TRUE(settings.Save()) << settings.GetWriteError();
    EXPECT_EQ(imterm::test::ReadFile(path.string() + ".bak"), original);
    EXPECT_NO_THROW(static_cast<void>(toml::parse_file(path.string())));
    EXPECT_TRUE(settings.GetWarnings().empty());
}

TEST_F(CaptureSettingsTest, MissingKeysAndWrongTableTypesAreSafe)
{
    for (const auto* text : {"", "default = 3", "[default]\nserial = 'wrong'\ninput = false\nlogging = 0\ndisplay = []",
        "[default.serial]\nbaud = 57600"}) {
        Write(text);
        EXPECT_NO_THROW(settings.Load());
        EXPECT_FALSE(settings.GetWarnings().empty());
        EXPECT_TRUE(CaptureSettings::Validate(settings.Get()).empty());
        EXPECT_EQ(settings.Get().serial.baudrate, std::string(text).find("57600") == std::string::npos ? 115200u : 57600u);
        ASSERT_TRUE(settings.Save());
    }
}

TEST_F(CaptureSettingsTest, LoadsLegacyDisplayLabelsAndNumericStrings)
{
    Write("[default.serial]\nserial_port = '/dev/ttyUSB7'\nbaud = '921600'\ndata_bits = '7'\n"
          "stop_bits = '1.5'\nparity = 'Odd'\nflow_control = 'Hardware'\n"
          "[default.input]\nnew_line_mode = 'Add CR to LF'\n[default.display]\nzoom_factor = 1.13\n");
    settings.Load();
    const auto& values = settings.Get();
    EXPECT_EQ(values.serial.port, "/dev/ttyUSB7");
    EXPECT_EQ(values.serial.baudrate, 921600u);
    EXPECT_EQ(values.serial.bytesize, serial::sevenbits);
    EXPECT_EQ(values.serial.stopbits, serial::stopbits_one_point_five);
    EXPECT_EQ(values.serial.parity, serial::parity_odd);
    EXPECT_EQ(values.serial.flowcontrol, serial::flowcontrol_hardware);
    EXPECT_EQ(values.newlineMode, Newline::AddCrToLf);
    EXPECT_FLOAT_EQ(values.zoomFactor, 1.1f);
    EXPECT_TRUE(settings.GetWarnings().empty());
}

TEST_F(CaptureSettingsTest, InvalidFieldsFallBackIndependently)
{
    Write("[default.serial]\nserial_port = ''\nbaud = '-12'\ndata_bits = '9'\nstop_bits = '3'\n"
          "parity = 'invalid'\nflow_control = 'invalid'\n[default.input]\nnew_line_mode = 'invalid'\n"
          "[default.logging]\nenabled = 'yes'\npath = ''\nline_numbers = 1\ntimestamps = []\n"
          "[default.display]\nzoom_factor = inf\n");
    settings.Load();
    EXPECT_GE(settings.GetWarnings().size(), 12u);
    EXPECT_TRUE(CaptureSettings::Validate(settings.Get()).empty());
    EXPECT_EQ(settings.Get().serial.baudrate, 115200u);
    EXPECT_EQ(settings.Get().serial.stopbits, serial::stopbits_two);
    EXPECT_EQ(settings.Get().newlineMode, Newline::Strict);
    EXPECT_FALSE(settings.Get().logging.Enabled);
    EXPECT_FLOAT_EQ(settings.Get().zoomFactor, 1.0f);
}

TEST_F(CaptureSettingsTest, ExtremeBaudAndTextValuesCannotOverflowUiBuffers)
{
    for (const auto& baud : {"0", "-1", "+9600", " 9600", "9600x", "1.5", "4294967296", "9999999999999999999999999999"}) {
        EXPECT_FALSE(CaptureSettings::ParseBaud(baud)) << baud;
        Write("[default.serial]\nbaud = '" + std::string(baud) + "'\n");
        settings.Load();
        EXPECT_EQ(settings.Get().serial.baudrate, 115200u) << baud;
    }
    EXPECT_EQ(CaptureSettings::ParseBaud("1"), 1u);
    EXPECT_EQ(CaptureSettings::ParseBaud("4294967295"), std::numeric_limits<uint32_t>::max());
    Write("[default.serial]\nserial_port = '" + std::string(5000, 'p') + "'\nbaud = '" + std::string(10000, '9')
        + "'\n[default.logging]\npath = '" + std::string(5000, 'p') + "'\n");
    EXPECT_NO_THROW(settings.Load());
    EXPECT_EQ(settings.Get().serial.port, "COM1");
    EXPECT_EQ(settings.Get().serial.baudrate, 115200u);
    EXPECT_EQ(settings.Get().logPath, imterm::TerminalLogger::GetDefaultLogPath());
}

TEST_F(CaptureSettingsTest, FutureVersionUsesDefaultsAndCannotBeOverwritten)
{
    const std::string original = "version = 999\n[default.serial]\nbaud = 9600\n";
    Write(original);
    settings.Load();
    EXPECT_EQ(settings.Get().serial.baudrate, 115200u);
    EXPECT_FALSE(settings.GetWarnings().empty());
    auto values = settings.Get();
    values.serial.baudrate = 57600;
    EXPECT_TRUE(settings.Set(values));
    EXPECT_FALSE(settings.Save());
    EXPECT_NE(settings.GetWriteError().find("newer version"), std::string::npos);
    EXPECT_EQ(imterm::test::ReadFile(path), original);
}

TEST_F(CaptureSettingsTest, EverySettingRoundTripsWithCanonicalTypes)
{
    settings.Load();
    for (int i = 0; i < 5; ++i) {
        auto values = settings.Get();
        values.serial.port = "port with spaces";
        values.serial.baudrate = std::numeric_limits<uint32_t>::max();
        values.serial.bytesize = static_cast<serial::bytesize_t>(5 + i % 4);
        values.serial.stopbits = static_cast<serial::stopbits_t>(1 + i % 3);
        values.serial.parity = static_cast<serial::parity_t>(i);
        values.serial.flowcontrol = static_cast<serial::flowcontrol_t>(i % 3);
        values.newlineMode = static_cast<Newline>(i % 3);
        values.logging = {true, false, false};
        values.logPath = directory.Path() / "logs with spaces";
        values.zoomFactor = 1.24f;
        ASSERT_TRUE(settings.Set(values));
        ASSERT_TRUE(settings.Save()) << settings.GetWriteError();
        CaptureSettings reloaded(path);
        reloaded.Load();
        ASSERT_TRUE(reloaded.GetWarnings().empty());
        const auto& loaded = reloaded.Get();
        EXPECT_EQ(loaded.serial.port, values.serial.port);
        EXPECT_EQ(loaded.serial.baudrate, values.serial.baudrate);
        EXPECT_EQ(loaded.serial.bytesize, values.serial.bytesize);
        EXPECT_EQ(loaded.serial.stopbits, values.serial.stopbits);
        EXPECT_EQ(loaded.serial.parity, values.serial.parity);
        EXPECT_EQ(loaded.serial.flowcontrol, values.serial.flowcontrol);
        EXPECT_EQ(loaded.newlineMode, values.newlineMode);
        EXPECT_TRUE(loaded.logging.Enabled);
        EXPECT_FALSE(loaded.logging.LineNumbers);
        EXPECT_FALSE(loaded.logging.TimeStamps);
        EXPECT_EQ(loaded.logPath, values.logPath);
        EXPECT_FLOAT_EQ(loaded.zoomFactor, 1.25f);
        const auto table = toml::parse_file(path.string());
        EXPECT_TRUE(table["default"]["serial"]["baud"].is_integer());
        EXPECT_TRUE(table["default"]["serial"]["data_bits"].is_integer());
    }
}

TEST_F(CaptureSettingsTest, BadZoomValuesRecoverAndPresetsNormalize)
{
    for (const auto* zoom : {"'large'", "0.0", "-1.0", "nan", "1e100"}) {
        Write("[default.display]\nzoom_factor = " + std::string(zoom) + "\n");
        settings.Load();
        EXPECT_FLOAT_EQ(settings.Get().zoomFactor, 1.0f);
    }
    Write("[default.display]\nzoom_factor = 1.13\n");
    settings.Load();
    EXPECT_FLOAT_EQ(settings.Get().zoomFactor, 1.1f);
}

TEST_F(CaptureSettingsTest, InvalidUpdatesDoNotReplaceValidSettings)
{
    settings.Load();
    for (const auto& port : {std::string(), std::string(5000, 'p'), std::string("a\0b", 3)}) {
        auto invalid = settings.Get();
        invalid.serial.port = port;
        EXPECT_FALSE(settings.Set(invalid));
        EXPECT_EQ(settings.Get().serial.port, "COM1");
    }
    auto invalid = settings.Get();
    invalid.serial.baudrate = 0;
    EXPECT_FALSE(settings.Set(invalid));
    invalid = settings.Get();
    invalid.logPath.clear();
    EXPECT_FALSE(settings.Set(invalid));
    invalid = settings.Get();
    invalid.zoomFactor = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(settings.Set(invalid));
}

TEST_F(CaptureSettingsTest, WriteFailurePreservesOriginalAndRemovesTemporaryFiles)
{
    settings.Load();
    const auto original = imterm::test::ReadFile(path);
    // A directory at the backup path reliably blocks replacement even as root.
    std::filesystem::create_directory(path.string() + ".bak");
    auto values = settings.Get();
    values.serial.baudrate = 9600;
    ASSERT_TRUE(settings.Set(values));
    EXPECT_FALSE(settings.Save());
    EXPECT_FALSE(settings.GetWriteError().empty());
    EXPECT_EQ(imterm::test::ReadFile(path), original);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory.Path()),
        std::filesystem::directory_iterator{}), 2);
    std::filesystem::remove(path.string() + ".bak");
    EXPECT_TRUE(settings.Save());
    EXPECT_TRUE(settings.GetWriteError().empty());
}

TEST_F(CaptureSettingsTest, UnreadableOrUnwritablePathsStillLoadDefaults)
{
    for (const auto& destination : {directory.Path(), directory.Path() / "missing" / "settings.toml"}) {
        CaptureSettings unavailable(destination);
        EXPECT_NO_THROW(unavailable.Load());
        EXPECT_TRUE(CaptureSettings::Validate(unavailable.Get()).empty());
        EXPECT_FALSE(unavailable.Save());
        EXPECT_FALSE(unavailable.GetWriteError().empty());
    }
}

TEST_F(CaptureSettingsTest, InterruptedReplacementRecoversTheBackupOnStartup)
{
    settings.Load();
    auto values = settings.Get();
    values.serial.baudrate = 57600;
    ASSERT_TRUE(settings.Set(values));
    ASSERT_TRUE(settings.Save());
    ASSERT_TRUE(settings.Save());
    std::filesystem::remove(path);
    settings.Load();
    EXPECT_EQ(settings.Get().serial.baudrate, 57600u);
    EXPECT_TRUE(std::filesystem::is_regular_file(path));
    EXPECT_FALSE(settings.GetWarnings().empty());
    EXPECT_NE(settings.GetWarnings().back().find("Recovered"), std::string::npos);
}

TEST_F(CaptureSettingsTest, LogPathRejectsFilesAndAllowsDirectoriesToBeCreated)
{
    Write("[default.logging]\npath = '" + path.generic_string() + "'\n");
    settings.Load();
    EXPECT_EQ(settings.Get().logPath, imterm::TerminalLogger::GetDefaultLogPath());
    EXPECT_FALSE(settings.GetWarnings().empty());
    auto values = settings.Get();
    values.logPath = path;
    EXPECT_FALSE(settings.Set(values));
    values.logPath = directory.Path() / "new" / "nested logs";
    EXPECT_TRUE(settings.Set(values));
    EXPECT_FALSE(std::filesystem::exists(values.logPath));
    std::filesystem::create_directories(values.logPath);
    EXPECT_TRUE(settings.Set(values));
}
} // namespace
