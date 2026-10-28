#pragma once

#include "../Shader.h"

class Skybox
{
  public:
    Skybox(const std::unique_ptr<lvk::IContext> &ctx, const char *skyboxTexture, const char *skyboxIrradiance,
           lvk::Format colorFormat, lvk::Format depthFormat, uint32_t numSamples = 1)
    {
        texSkybox = loadTexture(ctx, skyboxTexture, lvk::TextureType_Cube);
        texSkyboxIrradiance = loadTexture(ctx, skyboxIrradiance, lvk::TextureType_Cube);
        texBRDF_LUT = loadTexture(ctx, "data/brdfLUT.ktx");

        vertSkybox = loadRendererShader(ctx, "skybox.vert.slang");
        fragSkybox = loadRendererShader(ctx, "skybox.frag.slang");
        pipelineSkybox = ctx->createRenderPipeline({
            .smVert = vertSkybox,
            .smFrag = fragSkybox,
            .color = {{.format = colorFormat}},
            .depthFormat = depthFormat,
            .samplesCount = numSamples,
        });
    }

    void draw(lvk::ICommandBuffer &buf, const mat4 &view, const mat4 &proj, float environmentIntensity = 1.0f) const
    {
        buf.cmdPushDebugGroupLabel("Skybox", 0xff0000ff);
        buf.cmdBindRenderPipeline(pipelineSkybox);
        const struct
        {
            mat4 mvp;
            uint32_t texSkybox;
            float environmentIntensity;
        } pc = {
            .mvp = proj * mat4(mat3(view)), // discard the translation
            .texSkybox = texSkybox.index(),
            .environmentIntensity = environmentIntensity,
        };
        buf.cmdPushConstants(pc);
        buf.cmdBindDepthState({.compareOp = lvk::CompareOp_LessEqual, .isDepthWriteEnabled = false});
        buf.cmdDraw(36);
        buf.cmdPopDebugGroupLabel();
    }

    lvk::Holder<lvk::TextureHandle> texSkybox;
    lvk::Holder<lvk::TextureHandle> texSkyboxIrradiance;
    lvk::Holder<lvk::TextureHandle> texBRDF_LUT;
    lvk::Holder<lvk::ShaderModuleHandle> vertSkybox;
    lvk::Holder<lvk::ShaderModuleHandle> fragSkybox;
    lvk::Holder<lvk::RenderPipelineHandle> pipelineSkybox;
};
