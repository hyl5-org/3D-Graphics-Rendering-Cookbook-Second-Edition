#pragma once

#include <lvk/LVK.h>

#include <cstdint>
#include <vector>

#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
#include <lvk/vulkan/XrUtils.h>

struct OpenXRArraySwapchain
{
    bool create(lvk::IContext *ctx, XrSession session, const XrViewConfigurationView *views, const char *debugName);
    void destroy(lvk::IContext *ctx);

    bool acquire();
    void release();

    lvk::TextureHandle currentTexture() const;
    void fillProjectionViews(const XrView *xrViews, XrCompositionLayerProjectionView *projectionViews) const;

    XrSwapchain swapchain = XR_NULL_HANDLE;
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t currentImageIndex = 0;
    bool acquired = false;

    std::vector<XrSwapchainImageVulkanKHR> images;
    std::vector<lvk::Holder<lvk::TextureHandle>> textures;
};
#endif
