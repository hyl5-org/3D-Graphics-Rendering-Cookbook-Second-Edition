#pragma once

#include "shared/Utils.h"
#include <lvk/vulkan/VulkanClasses.h>

// Owns one shader stage. Render state and shader binding belong to the command buffer.
class ShaderObject final
{
  public:
    ShaderObject(const std::unique_ptr<lvk::IContext>& ctx, const char* filename) : ctx_(ctx), filename_(filename) {}
    ~ShaderObject() { static_cast<lvk::VulkanContext&>(*ctx_).destroyShader(shader_); }
    ShaderObject(const ShaderObject&) = delete;
    ShaderObject& operator=(const ShaderObject&) = delete;

    VkShaderEXT get() const
    {
        lvk::VulkanContext& vk = static_cast<lvk::VulkanContext&>(*ctx_);
        if (!vk.isShaderLayoutCompatible(shader_))
        {
            const VkShaderEXT replacement = loadShader(ctx_, filename_);
            if (!replacement) return VK_NULL_HANDLE;
            vk.destroyShader(shader_);
            shader_ = replacement;
        }
        return shader_;
    }

  private:
    const std::unique_ptr<lvk::IContext>& ctx_;
    const char* filename_;
    mutable VkShaderEXT shader_ = VK_NULL_HANDLE;
};
