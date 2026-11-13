#pragma once

#include <filesystem>
#include <functional>
#include <string_view>

namespace imterm {

// Test hooks run on the UI thread. Throwing from a checkpoint exercises normal
// exception unwinding, including initialization of individual GPU resources.
struct ApplicationOptions {
    std::filesystem::path settingsPath = "imterm.toml";
    std::filesystem::path iniPath = "imterm.ini";
    int frameLimit = 0;
    bool exerciseResize = false;
    std::function<void(std::string_view)> checkpoint;
    // Optional validation-layer error sink. Requires the Khronos validation
    // layer and VK_EXT_debug_utils; the callback must not throw.
    std::function<void(std::string_view)> graphicsError;
};

// Owns the platform/rendering lifetime for one application invocation. Failures
// propagate to the entry point after all initialized resources are released.
void RunApplication(const ApplicationOptions& options = {});

} // namespace imterm
