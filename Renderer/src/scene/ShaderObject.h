#pragma once

#include "../Shader.h"
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
            VkShaderEXT replacement = VK_NULL_HANDLE;
            for (const RendererShaders::Shader& shader : RendererShaders::shaders)
            {
                if (shader.name != filename_) continue;
                lvk::ShaderDesc desc(reinterpret_cast<const char*>(shader.data), shader.stage, filename_);
                desc.dataSize = shader.size;
                replacement = vk.createShader(desc);
                break;
            }
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
