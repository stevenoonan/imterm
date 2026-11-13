#include "application.h"
#include "test_support.h"

#include <gtest/gtest.h>
#include "imgui.h"
#include <cstdlib>
#include <vector>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class InjectedFailure : public std::runtime_error {
public:
    explicit InjectedFailure(std::string_view stage) : std::runtime_error(std::string(stage)) {}
};

bool ValidationEnabled()
{
    const char* value = std::getenv("IMTERM_VALIDATE_VULKAN");
    return value && std::string_view(value) == "1";
}

void ExpectNoValidationErrors(std::vector<std::string>& errors)
{
    EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
    errors.clear();
}

imterm::ApplicationOptions Options(const imterm::test::TemporaryDirectory& directory,
    std::vector<std::string>& errors)
{
    imterm::ApplicationOptions options;
    options.settingsPath = directory.Path() / "settings.toml";
    options.iniPath = directory.Path() / "imgui.ini";
    options.frameLimit = 6;
    options.exerciseResize = true;
    if (ValidationEnabled()) {
        options.graphicsError = [&](std::string_view message) { errors.emplace_back(message); };
    }
    return options;
}

TEST(ApplicationLifecycle, PartialInitializationUnwindsAndCanRestart)
{
    // Fail immediately after each allocation, including a later image in the
    // swapchain. The next iteration uses the same process and native libraries.
    std::vector<std::string_view> stages = {
        "glfw", "window", "imgui-context", "imgui-platform", "instance", "surface",
        "physical-device", "device", "descriptor-pool", "command-pool", "command-buffer",
        "fence", "acquire-semaphore", "swapchain", "render-pass", "image-view",
        "framebuffer", "present-semaphore", "imgui-renderer", "capture", "frame", "resize"};
    imterm::test::TemporaryDirectory directory;
    std::vector<std::string> errors;
    if (ValidationEnabled()) stages.push_back("debug-messenger");
    for (const std::string_view stage : stages) {
        SCOPED_TRACE(stage);
        auto options = Options(directory, errors);
        bool reached = false;
        options.checkpoint = [&](std::string_view current) {
            if (current == stage) { reached = true; throw InjectedFailure(stage); }
        };
        EXPECT_THROW(imterm::RunApplication(options), InjectedFailure);
        EXPECT_TRUE(reached);
        EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
        ExpectNoValidationErrors(errors);
    }
    for (const std::string_view stage : {"image-view", "framebuffer", "present-semaphore"}) {
        SCOPED_TRACE(stage);
        auto options = Options(directory, errors);
        int seen = 0;
        options.checkpoint = [&](std::string_view current) {
            if (current == stage && ++seen == 2) throw InjectedFailure(stage);
        };
        EXPECT_THROW(imterm::RunApplication(options), InjectedFailure);
        EXPECT_EQ(seen, 2);
        ExpectNoValidationErrors(errors);
    }
    EXPECT_NO_THROW(imterm::RunApplication(Options(directory, errors)));
    ExpectNoValidationErrors(errors);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}

TEST(ApplicationLifecycle, RepeatedLaunchResizeRenderAndShutdown)
{
    imterm::test::TemporaryDirectory directory;
    std::vector<std::string> errors;
    for (int invocation = 0; invocation < 3; ++invocation) {
        auto options = Options(directory, errors);
        int frames = 0;
        int resizes = 0;
        options.checkpoint = [&](std::string_view stage) {
            if (stage == "frame") ++frames;
            if (stage == "resize") ++resizes;
        };
        ASSERT_NO_THROW(imterm::RunApplication(options));
        EXPECT_EQ(frames, options.frameLimit);
        EXPECT_GT(resizes, 0);
        EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
        ExpectNoValidationErrors(errors);
        EXPECT_TRUE(std::filesystem::exists(options.iniPath));
    }
}

} // namespace
