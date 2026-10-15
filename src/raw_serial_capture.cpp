#include "raw_serial_capture.h"

#include <algorithm>
#include <limits>

namespace imterm {

bool RawSerialCapture::Start(const std::filesystem::path& path)
{
    if (IsActive()) {
        mError = "Stop the current raw capture before starting another.";
        return false;
    }
    mError.clear();
    mPath = path;
    mByteCount = 0;
    mOutput.clear();
    mOutput.open(path, std::ios::binary | std::ios::trunc);
    if (!mOutput) {
        mError = "Could not open raw capture file: " + path.string();
        return false;
    }
    return true;
}

bool RawSerialCapture::Append(std::span<const uint8_t> bytes)
{
    if (!IsActive() || bytes.empty()) return true;
    const size_t byteCount = bytes.size();
    while (!bytes.empty() && mOutput) {
        const size_t count = std::min(bytes.size(),
            static_cast<size_t>(std::numeric_limits<std::streamsize>::max()));
        mOutput.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(count));
        bytes = bytes.subspan(count);
    }
    // Keep a running capture inspectable and surface disk errors at the read
    // boundary. Capture failure must not prevent the same bytes being rendered.
    mOutput.flush();
    if (!mOutput) {
        mError = "Raw capture stopped: could not write to " + mPath.string();
        mOutput.close();
        return false;
    }
    mByteCount += byteCount;
    return true;
}

bool RawSerialCapture::Stop()
{
    if (!IsActive()) return mError.empty();
    mOutput.close();
    if (!mOutput) {
        mError = "Could not finish raw capture file: " + mPath.string();
        return false;
    }
    return true;
}

} // namespace imterm
