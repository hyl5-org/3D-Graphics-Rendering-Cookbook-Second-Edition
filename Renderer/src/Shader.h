#pragma once

#include "RendererShaders.h"
#include <lvk/LVK.h>

inline lvk::Holder<lvk::ShaderModuleHandle> loadRendererShader(const std::unique_ptr<lvk::IContext> &ctx,
                                                               const char *name)
{
    for (const RendererShaders::Shader &shader : RendererShaders::shaders)
    {
        if (shader.name != name)
        {
            continue;
        }
        lvk::ShaderModuleDesc desc(reinterpret_cast<const char *>(shader.data), shader.stage, name);
        desc.dataSize = shader.size;
        return ctx->createShaderModule(desc);
    }
    LLOGW("Renderer shader not found: %s\n", name);
    return {};
}
