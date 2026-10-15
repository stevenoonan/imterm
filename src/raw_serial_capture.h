#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace imterm {

// An opt-in, receive-only byte dump. No decoding, control handling, or metadata.
// Call Append immediately after a serial read, before terminal processing.
class RawSerialCapture {
public:
    bool Start(const std::filesystem::path& path);
    bool Append(std::span<const uint8_t> bytes);
    bool Stop();

    bool IsActive() const { return mOutput.is_open(); }
    const std::filesystem::path& GetPath() const { return mPath; }
    uint64_t GetByteCount() const { return mByteCount; }
    const std::string& GetError() const { return mError; }

private:
    std::ofstream mOutput;
    std::filesystem::path mPath;
    uint64_t mByteCount = 0;
    std::string mError;
};

} // namespace imterm
