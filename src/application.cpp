#include "application.h"
#include "capture.h"
#include "vulkan_renderer.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace imterm {
namespace {

struct GlfwRuntime {
    GlfwRuntime() {
        glfwSetErrorCallback([](int code, const char* text) {
            std::fprintf(stderr, "GLFW error %d: %s\n", code, text);
        });
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    }
    ~GlfwRuntime() { glfwTerminate(); }
    GlfwRuntime(const GlfwRuntime&) = delete;
    GlfwRuntime& operator=(const GlfwRuntime&) = delete;
};

struct ImGuiContextOwner {
    ImGuiContextOwner() {
        IMGUI_CHECKVERSION();
        if (!ImGui::CreateContext()) throw std::runtime_error("ImGui context creation failed");
    }
    ~ImGuiContextOwner() { ImGui::DestroyContext(); }
    ImGuiContextOwner(const ImGuiContextOwner&) = delete;
    ImGuiContextOwner& operator=(const ImGuiContextOwner&) = delete;
};

struct ImGuiPlatformOwner {
    ImGuiPlatformOwner() = default;
    ImGuiPlatformOwner(const ImGuiPlatformOwner&) = delete;
    ImGuiPlatformOwner& operator=(const ImGuiPlatformOwner&) = delete;
    ~ImGuiPlatformOwner() {
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
    }
};

static void glfw_scroll_callback(
    GLFWwindow* window, double xoffset, double yoffset)
{
    const bool mac_shortcuts = ImGui::GetIO().ConfigMacOSXBehaviors;
    const int shortcut_left = mac_shortcuts
        ? GLFW_KEY_LEFT_SUPER : GLFW_KEY_LEFT_CONTROL;
    const int shortcut_right = mac_shortcuts
        ? GLFW_KEY_RIGHT_SUPER : GLFW_KEY_RIGHT_CONTROL;
    const int alternate_left = mac_shortcuts
        ? GLFW_KEY_LEFT_CONTROL : GLFW_KEY_LEFT_ALT;
    const int alternate_right = mac_shortcuts
        ? GLFW_KEY_RIGHT_CONTROL : GLFW_KEY_RIGHT_ALT;
    const bool shortcut_down = glfwGetKey(window, shortcut_left) == GLFW_PRESS
        || glfwGetKey(window, shortcut_right) == GLFW_PRESS;
    const bool alternate_down = glfwGetKey(window, alternate_left) == GLFW_PRESS
        || glfwGetKey(window, alternate_right) == GLFW_PRESS;

    if (shortcut_down && !alternate_down) {
        if (auto* capture = static_cast<imterm::Capture*>(glfwGetWindowUserPointer(window)))
            capture->QueueInterfaceZoomWheel(static_cast<float>(yoffset));
        return;
    }

    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
}

static std::filesystem::path GetExecutableDirectory()
{
#ifdef _WIN32
    wchar_t executable_path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
        return std::filesystem::path(executable_path, executable_path + length).parent_path();
#else
    std::error_code error;
    const std::filesystem::path executable_path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error)
        return executable_path.parent_path();
#endif

    return std::filesystem::current_path();
}

static float GetInterfaceScale(GLFWmonitor* monitor)
{
    if (monitor == nullptr)
        return 1.0f;

    // Wayland exposes window sizes in compositor-scaled logical pixels and
    // provides the corresponding framebuffer scale separately. Applying our
    // physical-DPI fallback as well would scale the whole interface twice.
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
        return 1.0f;
#endif

    const float content_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(monitor);
    if (content_scale > 1.01f)
        return content_scale;

    int width_mm = 0;
    int height_mm = 0;
    glfwGetMonitorPhysicalSize(monitor, &width_mm, &height_mm);
    const GLFWvidmode* video_mode = glfwGetVideoMode(monitor);
    if (video_mode == nullptr || width_mm <= 0 || height_mm <= 0)
        return 1.0f;

    const float dpi_x = static_cast<float>(video_mode->width) * 25.4f / static_cast<float>(width_mm);
    const float dpi_y = static_cast<float>(video_mode->height) * 25.4f / static_cast<float>(height_mm);
    const float physical_scale = ((dpi_x + dpi_y) * 0.5f) / 96.0f;
    if (physical_scale < 1.20f)
        return 1.0f;

    // Quarter steps avoid awkward fractional sizes while preserving common
    // 125%, 150%, 175%, and 200% display scales.
    const float rounded_scale = std::round(physical_scale * 4.0f) / 4.0f;
    return std::clamp(rounded_scale, 1.0f, 3.0f);
}


} // namespace

void RunApplication(const ApplicationOptions& options)
{
    const auto checkpoint = [&](std::string_view stage) {
        if (options.checkpoint) options.checkpoint(stage);
    };
    GlfwRuntime runtime;
    checkpoint("glfw");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    const float scale = GetInterfaceScale(glfwGetPrimaryMonitor());
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(static_cast<int>(1280 * scale), static_cast<int>(720 * scale),
            "imterm", nullptr, nullptr), glfwDestroyWindow);
    if (!window) throw std::runtime_error("GLFW window creation failed");
    checkpoint("window");

    // ImGui retains this pointer through DestroyContext() (which saves the INI).
    const std::string iniPath = options.iniPath.string();
    ImGuiContextOwner context;
    checkpoint("imgui-context");
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = iniPath.empty() ? nullptr : iniPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);

    ImGuiPlatformOwner platform;
    if (!ImGui_ImplGlfw_InitForVulkan(window.get(), true))
        throw std::runtime_error("ImGui GLFW initialization failed");
    checkpoint("imgui-platform");
    glfwSetScrollCallback(window.get(), glfw_scroll_callback);
    VulkanRenderer renderer(window.get(), checkpoint, options.graphicsError);

    const auto fontPath = GetExecutableDirectory() / "JetBrainsMono-Medium.ttf";
    const float fontSize = std::round(16.0f * scale);
    if (std::filesystem::exists(fontPath)) {
        if (!io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), fontSize))
            throw std::runtime_error("Could not load bundled font: " + fontPath.string());
    }
    else {
        std::fprintf(stderr, "Font not found: %s; using ImGui default\n", fontPath.string().c_str());
    }
    Capture capture(MakeSerialTransport, options.settingsPath);
    capture.InitializeInterfaceZoom();
    glfwSetWindowUserPointer(window.get(), &capture);
    checkpoint("capture");

    int frames = 0;
    bool initialSize = true;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        capture.Update();
        if (!renderer.PrepareFrame()) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }
        ImGui_ImplGlfw_NewFrame();
        capture.ApplyPendingInterfaceZoom();
        ImGui::NewFrame();
        if (initialSize) {
            initialSize = false;
            const ImVec2 text = ImGui::CalcTextSize(std::string(150, '0').c_str());
            const float zoom = capture.GetInterfaceZoomFactor();
            glfwSetWindowSize(window.get(), static_cast<int>(text.x / zoom),
                static_cast<int>(text.y * 40.0f / zoom));
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
#ifdef IMGUI_SHOW_DEMO_WINDOW
        ImGui::ShowDemoWindow();
#else
        capture.Render();
#endif
        ImGui::Render();
        if (!renderer.Render(ImGui::GetDrawData())) continue;
        checkpoint("frame");
        ++frames;
        if (options.exerciseResize && frames == 2)
            glfwSetWindowSize(window.get(), 900, 600);
        if (options.frameLimit > 0 && frames >= options.frameLimit) break;
    }
    capture.Shutdown();
    glfwSetWindowUserPointer(window.get(), nullptr);
}

} // namespace imterm
