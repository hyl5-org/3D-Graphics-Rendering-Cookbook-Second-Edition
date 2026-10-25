#pragma once

#include "RendererShaders.h"
#include <lvk/LVK.h>

inline lvk::Holder<lvk::ShaderModuleHandle> loadRendererShader(const std::unique_ptr<lvk::IContext> &ctx,
                                                               const char *name, const char *entryPoint = "main")
{
    for (const RendererShaders::Shader &shader : RendererShaders::shaders)
    {
        if (shader.name != name || shader.entryPoint != entryPoint)
        {
            continue;
        }
        const lvk::ShaderModuleDesc desc(shader.data, shader.size, shader.stage, name);
        return ctx->createShaderModule(desc);
    }
    LLOGW("Renderer shader not found: %s (%s)\n", name, entryPoint);
    return {};
}
