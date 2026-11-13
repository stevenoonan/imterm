#include "vulkan_renderer.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace imterm {
namespace {

void Check(VkResult result)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error("Vulkan operation failed (VkResult "
            + std::to_string(result) + ")");
}

// Enumeration counts may change between calls. Never use an incomplete list.
template<typename T, typename Enumerate>
std::vector<T> EnumerateValues(Enumerate enumerate)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        uint32_t count = 0;
        Check(enumerate(&count, nullptr));
        std::vector<T> values(count);
        const VkResult result = enumerate(&count, values.data());
        if (result == VK_INCOMPLETE) continue;
        Check(result);
        values.resize(count);
        return values;
    }
    throw std::runtime_error("Vulkan enumeration did not stabilize");
}

} // namespace

struct VulkanRenderer::Impl {
    GLFWwindow* window;
    std::function<void(std::string_view)> checkpoint;
    std::function<void(std::string_view)> graphicsError;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    bool rebuild = false;
    struct Image {
        VkImage image = VK_NULL_HANDLE; // Borrowed from the swapchain.
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkSemaphore complete = VK_NULL_HANDLE;
    };
    std::vector<Image> images;
    uint32_t minImageCount = 2;

    Impl(GLFWwindow* nativeWindow, const std::function<void(std::string_view)>& hook,
        const std::function<void(std::string_view)>& errorSink)
        : window(nativeWindow), checkpoint(hook), graphicsError(errorSink) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl()
    {
        // Teardown must continue even after device loss. Never throw or abort.
        if (device) {
            const VkResult result = vkDeviceWaitIdle(device);
            if (result != VK_SUCCESS)
                std::fprintf(stderr, "Vulkan shutdown wait: %d\n", result);
            ShutdownBackend();
            DestroySwapchain();
            if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (debugMessenger) {
            const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy) destroy(instance, debugMessenger, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
    }

    void Stage(std::string_view name) const { if (checkpoint) checkpoint(name); }

    static VKAPI_ATTR VkBool32 VKAPI_CALL ValidationMessage(
        VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) noexcept
    {
        const auto* self = static_cast<const Impl*>(userData);
        try { self->graphicsError(data->pMessage ? data->pMessage : "Vulkan validation error"); }
        catch (...) { std::fputs("Vulkan diagnostic callback threw\n", stderr); }
        return VK_FALSE;
    }

    void ShutdownBackend() noexcept
    {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplVulkan_Shutdown();
    }

    void DestroySwapchain() noexcept
    {
        for (const auto& image : images) {
            if (image.framebuffer) vkDestroyFramebuffer(device, image.framebuffer, nullptr);
            if (image.view) vkDestroyImageView(device, image.view, nullptr);
            if (image.complete) vkDestroySemaphore(device, image.complete, nullptr);
        }
        images.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    void Initialize()
    {
        if (!glfwVulkanSupported()) throw std::runtime_error("GLFW: Vulkan unavailable");
        uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (!extensions || extensionCount == 0)
            throw std::runtime_error("GLFW: Vulkan surface extensions unavailable");
        std::vector<const char*> instanceExtensions(extensions, extensions + extensionCount);
        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
        if (graphicsError) {
            const auto layers = EnumerateValues<VkLayerProperties>([](uint32_t* count, VkLayerProperties* values) {
                return vkEnumerateInstanceLayerProperties(count, values);
            });
            if (std::none_of(layers.begin(), layers.end(), [&](const auto& layer) {
                return std::string_view(layer.layerName) == validationLayer;
            })) throw std::runtime_error("Requested Vulkan validation layer is unavailable");
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugInfo.pfnUserCallback = ValidationMessage;
            debugInfo.pUserData = this;
        }
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "imterm";
        application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &application;
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
        if (graphicsError) {
            instanceInfo.enabledLayerCount = 1;
            instanceInfo.ppEnabledLayerNames = &validationLayer;
            instanceInfo.pNext = &debugInfo;
        }
        Check(vkCreateInstance(&instanceInfo, nullptr, &instance));
        Stage("instance");
        if (graphicsError) {
            const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (!create) throw std::runtime_error("Vulkan debug-utils entry point unavailable");
            Check(create(instance, &debugInfo, nullptr, &debugMessenger));
            Stage("debug-messenger");
        }
        Check(glfwCreateWindowSurface(instance, window, nullptr, &surface));
        Stage("surface");
        SelectDevice();

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        Check(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device));
        Stage("device");
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
        if (!queue) throw std::runtime_error("Vulkan graphics queue unavailable");

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128},
            {VK_DESCRIPTOR_TYPE_SAMPLER, 16}};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 272;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
        poolInfo.pPoolSizes = poolSizes;
        Check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
        Stage("descriptor-pool");

        VkCommandPoolCreateInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandInfo.queueFamilyIndex = queueFamily;
        Check(vkCreateCommandPool(device, &commandInfo, nullptr, &commandPool));
        Stage("command-pool");
        VkCommandBufferAllocateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bufferInfo.commandPool = commandPool;
        bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferInfo.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(device, &bufferInfo, &commandBuffer));
        Stage("command-buffer");
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        Check(vkCreateFence(device, &fenceInfo, nullptr, &fence));
        Stage("fence");
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        Check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &acquired));
        Stage("acquire-semaphore");
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
            throw std::runtime_error("Initial framebuffer has no drawable area");
        CreateSwapchain();
        InitializeBackend();
    }

    void SelectDevice()
    {
        const auto devices = EnumerateValues<VkPhysicalDevice>([&](uint32_t* count, VkPhysicalDevice* values) {
            return vkEnumeratePhysicalDevices(instance, count, values);
        });
        int bestScore = -1;
        for (VkPhysicalDevice candidate : devices) {
            const auto extensions = EnumerateValues<VkExtensionProperties>([&](uint32_t* count, VkExtensionProperties* values) {
                return vkEnumerateDeviceExtensionProperties(candidate, nullptr, count, values);
            });
            if (std::none_of(extensions.begin(), extensions.end(), [](const auto& extension) {
                return std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            })) continue;
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            for (uint32_t index = 0; index < count; ++index) {
                VkBool32 present = VK_FALSE;
                Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface, &present));
                if (!(families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    || families[index].queueCount == 0 || !present) continue;
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(candidate, &properties);
                const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1 : 0;
                if (score > bestScore) {
                    bestScore = score;
                    physicalDevice = candidate;
                    queueFamily = index;
                }
            }
        }
        if (!physicalDevice)
            throw std::runtime_error("No Vulkan device supports graphics and presentation to this window");
        Stage("physical-device");
    }

    void CreateSwapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities));
        const auto formats = EnumerateValues<VkSurfaceFormatKHR>([&](uint32_t* count, VkSurfaceFormatKHR* values) {
            return vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, count, values);
        });
        if (formats.empty()) throw std::runtime_error("Vulkan surface has no formats");
        VkSurfaceFormatKHR format = formats.front();
        for (const auto& candidate : formats) {
            if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM
                && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                format = candidate;
                break;
            }
        }
        if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
        if (colorFormat != VK_FORMAT_UNDEFINED && colorFormat != format.format)
            throw std::runtime_error("Vulkan surface format changed; restart imterm");
        colorFormat = format.format;
        extent = capabilities.currentExtent;
        if (extent.width == std::numeric_limits<uint32_t>::max()) {
            extent.width = std::clamp(static_cast<uint32_t>(framebufferWidth),
                capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(framebufferHeight),
                capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        minImageCount = std::max(2u, capabilities.minImageCount);
        if (capabilities.maxImageCount != 0 && capabilities.maxImageCount < minImageCount)
            throw std::runtime_error("Vulkan surface requires fewer than two swapchain images");
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            throw std::runtime_error("Vulkan surface does not support color attachments");
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (const auto candidate : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                 VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                 VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
            if (capabilities.supportedCompositeAlpha & candidate) { alpha = candidate; break; }
        }
        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = surface;
        swapchainInfo.minImageCount = minImageCount;
        swapchainInfo.imageFormat = format.format;
        swapchainInfo.imageColorSpace = format.colorSpace;
        swapchainInfo.imageExtent = extent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = alpha;
        swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // Required by Vulkan.
        swapchainInfo.clipped = VK_TRUE;
        Check(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain));
        Stage("swapchain");
        const auto nativeImages = EnumerateValues<VkImage>([&](uint32_t* count, VkImage* values) {
            return vkGetSwapchainImagesKHR(device, swapchain, count, values);
        });
        if (nativeImages.size() < minImageCount)
            throw std::runtime_error("Vulkan swapchain returned too few images");
        images.resize(nativeImages.size());

        if (!renderPass) {
            VkAttachmentDescription attachment{};
            attachment.format = format.format;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &reference;
            VkSubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VkRenderPassCreateInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            passInfo.attachmentCount = 1;
            passInfo.pAttachments = &attachment;
            passInfo.subpassCount = 1;
            passInfo.pSubpasses = &subpass;
            passInfo.dependencyCount = 1;
            passInfo.pDependencies = &dependency;
            Check(vkCreateRenderPass(device, &passInfo, nullptr, &renderPass));
            Stage("render-pass");
        }
        for (size_t index = 0; index < images.size(); ++index) {
            Image& image = images[index];
            image.image = nativeImages[index];
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format.format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            Check(vkCreateImageView(device, &viewInfo, nullptr, &image.view));
            Stage("image-view");
            VkFramebufferCreateInfo frameInfo{};
            frameInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            frameInfo.renderPass = renderPass;
            frameInfo.attachmentCount = 1;
            frameInfo.pAttachments = &image.view;
            frameInfo.width = extent.width;
            frameInfo.height = extent.height;
            frameInfo.layers = 1;
            Check(vkCreateFramebuffer(device, &frameInfo, nullptr, &image.framebuffer));
            Stage("framebuffer");
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            Check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &image.complete));
            Stage("present-semaphore");
        }
    }

    void InitializeBackend()
    {
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_0;
        info.Instance = instance;
        info.PhysicalDevice = physicalDevice;
        info.Device = device;
        info.QueueFamily = queueFamily;
        info.Queue = queue;
        info.DescriptorPool = descriptorPool;
        info.MinImageCount = minImageCount;
        info.ImageCount = static_cast<uint32_t>(images.size());
        info.PipelineInfoMain.RenderPass = renderPass;
        info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.CheckVkResultFn = Check;
        if (!ImGui_ImplVulkan_Init(&info))
            throw std::runtime_error("ImGui Vulkan initialization failed");
        Stage("imgui-renderer");
    }

    bool PrepareFrame()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0) return false;
        if (rebuild || width != framebufferWidth || height != framebufferHeight) {
            Check(vkDeviceWaitIdle(device));
            // Keep the compatible render pass and ImGui backend alive. Shutting
            // down the backend also destroys GLFW's viewport bindings. ImGui's
            // internal render-buffer ring is independent of swapchain indices;
            // our single in-flight submission makes it safe across image counts.
            DestroySwapchain();
            framebufferWidth = width;
            framebufferHeight = height;
            CreateSwapchain();
            rebuild = false;
            Stage("resize");
        }
        ImGui_ImplVulkan_NewFrame();
        return true;
    }

    bool Render(ImDrawData* data)
    {
        if (!data || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0) return false;
        Check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
        uint32_t imageIndex = 0;
        const VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            acquired, VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) { rebuild = true; return false; }
        if (acquireResult == VK_SUBOPTIMAL_KHR) rebuild = true;
        else Check(acquireResult);
        if (imageIndex >= images.size()) throw std::runtime_error("Invalid Vulkan swapchain image index");
        const Image& image = images[imageIndex];
        Check(vkResetCommandPool(device, commandPool, 0));
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        const VkClearValue clear{{{0.45f, 0.55f, 0.60f, 1.0f}}};
        VkRenderPassBeginInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        passInfo.renderPass = renderPass;
        passInfo.framebuffer = image.framebuffer;
        passInfo.renderArea.extent = extent;
        passInfo.clearValueCount = 1;
        passInfo.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(data, commandBuffer);
        vkCmdEndRenderPass(commandBuffer);
        Check(vkEndCommandBuffer(commandBuffer));
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &image.complete;
        Check(vkResetFences(device, 1, &fence));
        Check(vkQueueSubmit(queue, 1, &submit, fence));
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &image.complete;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        const VkResult result = vkQueuePresentKHR(queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) rebuild = true;
        else Check(result);
        return true;
    }
};

VulkanRenderer::VulkanRenderer(GLFWwindow* window,
    const std::function<void(std::string_view)>& checkpoint,
    const std::function<void(std::string_view)>& graphicsError)
    : mImpl(std::make_unique<Impl>(window, checkpoint, graphicsError))
{
    mImpl->Initialize();
}
VulkanRenderer::~VulkanRenderer() = default;
bool VulkanRenderer::PrepareFrame() { return mImpl->PrepareFrame(); }
bool VulkanRenderer::Render(ImDrawData* data) { return mImpl->Render(data); }

} // namespace imterm
