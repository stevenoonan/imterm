#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "terminal_data.h"

namespace imterm::test {

inline std::vector<uint8_t> Bytes(std::string_view value)
{
    return std::vector<uint8_t>(value.begin(), value.end());
}

inline std::vector<uint8_t> Bytes(std::initializer_list<uint8_t> value)
{
    return std::vector<uint8_t>(value);
}

inline std::string LineText(const Line& line)
{
    std::string result;
    result.reserve(line.size());
    for (const Glyph& glyph : line) {
        result.push_back(static_cast<char>(glyph.mChar));
    }
    return result;
}

inline std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto unique_value = std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count();
        mPath = std::filesystem::temp_directory_path()
            / ("imterm-tests-" + std::to_string(unique_value));
        std::filesystem::create_directories(mPath);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(mPath, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& Path() const
    {
        return mPath;
    }

private:
    std::filesystem::path mPath;
};

} // namespace imterm::test
