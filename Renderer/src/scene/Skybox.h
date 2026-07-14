#pragma once

#include "../DemoConfig.h"

class Skybox
{
  public:
    Skybox(const std::unique_ptr<lvk::IContext> &ctx, const char *skyboxTexture, const char *skyboxIrradiance,
           lvk::Format colorFormat, lvk::Format depthFormat, uint32_t numSamples = 1)
    {
        texSkybox = loadTexture(ctx, skyboxTexture, lvk::TextureType_Cube);
        texSkyboxIrradiance = loadTexture(ctx, skyboxIrradiance, lvk::TextureType_Cube);

        vertSkybox = loadShaderModule(ctx, "Renderer/shaders/skybox.vert");
        fragSkybox = loadShaderModule(ctx, "Renderer/shaders/skybox.frag");
        pipelineSkybox = ctx->createRenderPipeline({
            .smVert = vertSkybox,
            .smFrag = fragSkybox,
            .color = {{.format = colorFormat}},
            .depthFormat = depthFormat,
            .samplesCount = numSamples,
        });
    }

    void draw(lvk::ICommandBuffer &buf) const
    {
        buf.cmdPushDebugGroupLabel("Skybox", 0xff0000ff);
        buf.cmdBindRenderPipeline(pipelineSkybox);
        const struct
        {
            mat4 mvp[2];
            uint32_t texSkybox;
        } pc = {
            .mvp = {
                FinalDemo::gSettings.view.projection[0] * mat4(mat3(FinalDemo::gSettings.view.view[0])),
                FinalDemo::gSettings.view.projection[1] * mat4(mat3(FinalDemo::gSettings.view.view[1])),
            }, // discard the translation
            .texSkybox = texSkybox.index(),
        };
        buf.cmdPushConstants(pc);
        buf.cmdBindDepthState({.compareOp = lvk::CompareOp_LessEqual, .isDepthWriteEnabled = false});
        buf.cmdDraw(36);
        buf.cmdPopDebugGroupLabel();
    }

    lvk::Holder<lvk::TextureHandle> texSkybox;
    lvk::Holder<lvk::TextureHandle> texSkyboxIrradiance;
    lvk::Holder<lvk::ShaderModuleHandle> vertSkybox;
    lvk::Holder<lvk::ShaderModuleHandle> fragSkybox;
    lvk::Holder<lvk::RenderPipelineHandle> pipelineSkybox;
};
