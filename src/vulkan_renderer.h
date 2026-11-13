#pragma once

#include <functional>
#include <memory>
#include <string_view>

struct GLFWwindow;
struct ImDrawData;

namespace imterm {

class VulkanRenderer {
public:
    VulkanRenderer(GLFWwindow* window,
        const std::function<void(std::string_view)>& checkpoint,
        const std::function<void(std::string_view)>& graphicsError = {});
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // Returns false for a zero-sized framebuffer; callers should wait for events.
    bool PrepareFrame();
    bool Render(ImDrawData* data);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace imterm
