#include "OpenXRArraySwapchain.h"

#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR

#include <lvk/vulkan/VulkanUtils.h>

#include <algorithm>
#include <cstdio>

namespace
{
VkFormat chooseSwapchainFormat(XrSession session)
{
    uint32_t numFormats = 0;
    XR_ASSERT(xrEnumerateSwapchainFormats(session, 0, &numFormats, nullptr));

    std::vector<int64_t> formats(numFormats);
    XR_ASSERT(xrEnumerateSwapchainFormats(session, numFormats, &numFormats, formats.data()));

    const VkFormat preferred[] = {
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
    };

    for (const VkFormat format : preferred)
    {
        if (std::find(formats.begin(), formats.end(), static_cast<int64_t>(format)) != formats.end())
        {
            return format;
        }
    }

    return formats.empty() ? VK_FORMAT_UNDEFINED : static_cast<VkFormat>(formats.front());
}
} // namespace

bool OpenXRArraySwapchain::create(lvk::IContext *ctx, XrSession session, const XrViewConfigurationView *views,
                                  const char *debugName)
{
    if (!ctx || !session || !views)
    {
        return false;
    }

    vkFormat = chooseSwapchainFormat(session);
    if (vkFormat == VK_FORMAT_UNDEFINED)
    {
        return false;
    }

    width = std::max(views[0].recommendedImageRectWidth, views[1].recommendedImageRectWidth);
    height = std::max(views[0].recommendedImageRectHeight, views[1].recommendedImageRectHeight);

    const XrSwapchainCreateInfo swapchainCI = {
        .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
        .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT,
        .format = vkFormat,
        .sampleCount = 1,
        .width = width,
        .height = height,
        .faceCount = 1,
        .arraySize = 2,
        .mipCount = 1,
    };

    XR_ASSERT(xrCreateSwapchain(session, &swapchainCI, &swapchain));

    uint32_t numImages = 0;
    XR_ASSERT(xrEnumerateSwapchainImages(swapchain, 0, &numImages, nullptr));
    images.resize(numImages, {.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    XR_ASSERT(xrEnumerateSwapchainImages(swapchain, numImages, &numImages,
                                         reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data())));

    textures.reserve(numImages);
    for (uint32_t i = 0; i != numImages; i++)
    {
        char name[128] = {};
        std::snprintf(name, sizeof(name) - 1, "%s %u", debugName ? debugName : "OpenXR array swapchain", i);

        lvk::Result result;
        textures.push_back(lvk::createTextureFromVkImage(
            ctx,
            {
                .image = images[i].image,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = vkFormat,
                .extent = {.width = width, .height = height, .depth = 1},
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .numLevels = 1,
                .numLayers = 2,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .isSwapchainImage = true,
                .debugName = name,
            },
            &result));
        if (!result.isOk() || textures.back().empty())
        {
            destroy(ctx);
            return false;
        }
    }

    return true;
}

void OpenXRArraySwapchain::destroy(lvk::IContext *ctx)
{
    if (acquired)
    {
        release();
    }

    textures.clear();
    images.clear();

    if (swapchain)
    {
        xrDestroySwapchain(swapchain);
        swapchain = XR_NULL_HANDLE;
    }

    vkFormat = VK_FORMAT_UNDEFINED;
    width = 0;
    height = 0;
    currentImageIndex = 0;
    (void)ctx;
}

bool OpenXRArraySwapchain::acquire()
{
    if (!swapchain || acquired)
    {
        return false;
    }

    const XrSwapchainImageAcquireInfo acquireInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XR_ASSERT(xrAcquireSwapchainImage(swapchain, &acquireInfo, &currentImageIndex));

    const XrSwapchainImageWaitInfo waitInfo = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
        .timeout = XR_INFINITE_DURATION,
    };
    XR_ASSERT(xrWaitSwapchainImage(swapchain, &waitInfo));

    acquired = true;
    return true;
}

void OpenXRArraySwapchain::release()
{
    if (!swapchain || !acquired)
    {
        return;
    }

    const XrSwapchainImageReleaseInfo releaseInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XR_ASSERT(xrReleaseSwapchainImage(swapchain, &releaseInfo));
    acquired = false;
}

lvk::TextureHandle OpenXRArraySwapchain::currentTexture() const
{
    if (!acquired || currentImageIndex >= textures.size())
    {
        return {};
    }
    return textures[currentImageIndex];
}

void OpenXRArraySwapchain::fillProjectionViews(const XrView *xrViews,
                                               XrCompositionLayerProjectionView *projectionViews) const
{
    if (!xrViews || !projectionViews)
    {
        return;
    }

    for (uint32_t eye = 0; eye != 2; eye++)
    {
        projectionViews[eye] = {
            .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
            .pose = xrViews[eye].pose,
            .fov = xrViews[eye].fov,
            .subImage =
                {
                    .swapchain = swapchain,
                    .imageRect =
                        {
                            .offset = {0, 0},
                            .extent = {static_cast<int32_t>(width), static_cast<int32_t>(height)},
                        },
                    .imageArrayIndex = eye,
                },
        };
    }
}

#endif
