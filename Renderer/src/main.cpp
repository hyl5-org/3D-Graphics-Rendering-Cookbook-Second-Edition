#include "DemoUI.h"
#include "RenderPasses.h"

#include "scene/VKMesh11Lazy.h"

#include <algorithm>
#include <cstdlib>

using namespace FinalDemo;

VULKAN_APP_MAIN
{
    const VulkanAppConfig appConfig{
        .initialCameraPos = vec3(-18.621f, 4.621f, -6.359f),
        .initialCameraTarget = vec3(0, +5.0f, 0),
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
        .showGLTFInspector = false,
        .contextConfig = {.enableValidation = true, .enableValidationBestPractices = false},
        .enableOpenXR = true,
#endif
    };

    VULKAN_APP_DECLARE(app, appConfig);

    app.positioner_.maxSpeed_ = 1.5f;
    app.maxFPS_ = 30;
    installKeyboardShortcuts(app);

    LoadedScene loadedScene = loadDemoScene();
    LineCanvas3D canvas3d;

    std::unique_ptr<lvk::IContext> &ctx = app.ctx_;
    const lvk::Dimensions renderSize = app.getOutputDimensions();
    LVK_ASSERT(renderSize.width > 0 && renderSize.height > 0);
    FrameTargets targets = createGBufferTargets(ctx, app.getDepthFormat(), renderSize);
    lvk::Holder<lvk::SamplerHandle> samplerClamp = ctx->createSampler({
        .wrapU = lvk::SamplerWrap_Clamp,
        .wrapV = lvk::SamplerWrap_Clamp,
        .wrapW = lvk::SamplerWrap_Clamp,
    });

    // Shadow Map
    ShadowPass shadows(ctx);

    // Compute Frustum Cull
    CullingPipeline cullingPipeline(ctx);

    // Geometry
    RenderPipelines pipelines(ctx, loadedScene.meshData, app.getDepthFormat(), ctx->getFormat(shadows.map));

    LightingPass lighting(ctx, targets, samplerClamp, app.getColorFormat());

    OITPass oit(ctx, targets.sizeFb);

    HDRPass hdr(ctx, targets, samplerClamp, app.getColorFormat());
    const Skybox skyBox(ctx, "data/immenstadter_horn_2k_prefilter.ktx", "data/immenstadter_horn_2k_irradiance.ktx",
                        kOffscreenFormat, app.getDepthFormat(), kNumSamples);
    VKMesh11Lazy mesh(ctx, loadedScene.meshData, loadedScene.scene);
    SceneDrawLists drawLists(ctx, loadedScene.meshData, mesh);
    SceneCulling culling(ctx, loadedScene, mesh);

    app.run(
        [&](uint32_t width, uint32_t height, float aspectRatio, float deltaSeconds)
        {
            LVK_PROFILER_FUNCTION();

            gSettings.view.update(app, 0.01f, 2000.0f);
            const CullingDataXr cullingData =
                culling.prepareXr(gSettings.view.projection[0], gSettings.view.view[0],
                                  gSettings.view.projection[1], gSettings.view.view[1], drawLists);
            const LightFrame lightFrame = buildLightFrame(gSettings.light, loadedScene.worldBounds);

            lvk::ICommandBuffer *commandBuffer = nullptr;
            LVK_PROFILER_ZONE("Acquire command buffer", LVK_PROFILER_COLOR_WAIT);
            commandBuffer = &ctx->acquireCommandBuffer();
            LVK_PROFILER_ZONE_END();
            lvk::ICommandBuffer &buf = *commandBuffer;

            buf.cmdPushDebugGroupLabel("Frame", 0xffffffff);
            LVK_PROFILER_ZONE("Texture uploads", 0xff80ffff);
            buf.cmdPushDebugGroupLabel("Texture Uploads", 0xff80ffff);
            mesh.processLoadedTextures(buf);
            buf.cmdPopDebugGroupLabel();
            LVK_PROFILER_ZONE_END();
            {
                LVK_PROFILER_ZONE("Culling", 0xff00ffff);
                culling.executeXr(ctx, buf, loadedScene, mesh, drawLists, cullingPipeline.pipeline, cullingData);
                LVK_PROFILER_ZONE_END();

                const bool useOIT = gSettings.draw.meshesTransparent;
                if (useOIT)
                {
                   LVK_PROFILER_ZONE("OIT clear", 0xff80ff80);
                   oit.clear(buf);
                   LVK_PROFILER_ZONE_END();
                }

                LVK_PROFILER_ZONE("Shadow and draw stats", 0xff8080ff);
                buf.cmdPushDebugGroupLabel("Shadow", 0xff8080ff);
                shadows.updateIfNeeded(buf, mesh, pipelines, lightFrame);
                buf.cmdPopDebugGroupLabel();
                LVK_PROFILER_ZONE_END();

                LVK_PROFILER_ZONE("Geometry pass", LVK_PROFILER_COLOR_CMD_DRAW);
                renderGbufferPass(ctx, buf, targets, loadedScene, skyBox, mesh, pipelines, drawLists, shadows,
                                  canvas3d, lightFrame);
                LVK_PROFILER_ZONE_END();

                lvk::TextureHandle currentColor;
                LVK_PROFILER_ZONE("Lighting pass", LVK_PROFILER_COLOR_CMD_DRAW);
                currentColor = lighting.execute(ctx, buf, targets, skyBox, shadows, samplerClamp);
                LVK_PROFILER_ZONE_END();

                LVK_PROFILER_ZONE("Skybox pass", LVK_PROFILER_COLOR_CMD_DRAW);
                renderSkyboxPass(buf, targets, skyBox);
                LVK_PROFILER_ZONE_END();

                if (useOIT)
                {
                   LVK_PROFILER_ZONE("Transparent pass", LVK_PROFILER_COLOR_CMD_DRAW);
                   renderTransparentPass(ctx, buf, targets, skyBox, mesh, pipelines, drawLists, oit, shadows);
                   LVK_PROFILER_ZONE_END();
                }

                LVK_PROFILER_ZONE("Post process", LVK_PROFILER_COLOR_CMD_DISPATCH);
                currentColor = useOIT ? oit.combine(ctx, buf, targets, currentColor) : currentColor;

                hdr.execute(buf, currentColor, deltaSeconds, samplerClamp);
                LVK_PROFILER_ZONE_END();

                lvk::TextureHandle outputTexture;
                LVK_PROFILER_ZONE("Acquire output texture", LVK_PROFILER_COLOR_PRESENT);
                outputTexture = app.getCurrentOutputTexture();
                LVK_PROFILER_ZONE_END();
                const lvk::Framebuffer framebufferMain = {
                    .color = {{.texture = outputTexture}},
                };

                LVK_PROFILER_ZONE("Tone map", LVK_PROFILER_COLOR_CMD_DRAW);
                if (app.isOpenXR() && std::getenv("XR_DEBUG_CLEAR"))
                {
                    buf.cmdBeginRendering(
                        {.color = {{.loadOp = lvk::LoadOp_Clear,
                                    .storeOp = lvk::StoreOp_Store,
                                    .clearColor = {1.0f, 0.0f, 1.0f, 1.0f}}},
                         .viewMask = kMultiViewViewMask},
                        framebufferMain);
                    buf.cmdEndRendering();
                }
                else
                {
                    hdr.toneMap(buf, framebufferMain, currentColor);
                }
                LVK_PROFILER_ZONE_END();

//                if (!app.isOpenXR())
//                {
//                    LVK_PROFILER_ZONE("ImGui", 0xff40ff40);
//                    buf.cmdPushDebugGroupLabel("ImGui", 0xff40ff40);
//                    app.imgui_->beginFrame(framebufferMain);
//                    app.drawFPS();
//                    app.drawMemo();
//                    drawControls(width, height, aspectRatio, culling, shadows, hdr, app);
//#if defined(ANDROID)
//                    const float screenWidth = ImGui::GetIO().DisplaySize.x;
//                    const float screenHeight = ImGui::GetIO().DisplaySize.y;
//                    const float buttonSize = std::clamp(screenWidth * 0.11f, 88.0f, 150.0f);
//                    const float gapX = ImGui::GetStyle().ItemSpacing.x;
//                    const float gapY = ImGui::GetStyle().ItemSpacing.y;
//                    const ImVec2 moveButtonSize(buttonSize, buttonSize);
//                    auto movementButton = [](const char *label, const ImVec2 &size)
//                    {
//                        ImGui::Button(label, size);
//                        return ImGui::IsItemActive();
//                    };
//
//                    ImGui::SetNextWindowPos(ImVec2(18.0f, screenHeight - (2.0f * buttonSize + gapY) - 28.0f),
//                                            ImGuiCond_Always);
//                    ImGui::Begin("##movement", nullptr,
//                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
//                                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavInputs |
//                                     ImGuiWindowFlags_NoSavedSettings);
//                    ImGui::Dummy(moveButtonSize);
//                    ImGui::SameLine(0.0f, gapX);
//                    app.positioner_.movement_.forward_ = movementButton("W", moveButtonSize);
//                    ImGui::SameLine(0.0f, gapX);
//                    ImGui::Dummy(moveButtonSize);
//                    app.positioner_.movement_.left_ = movementButton("A", moveButtonSize);
//                    ImGui::SameLine(0.0f, gapX);
//                    app.positioner_.movement_.backward_ = movementButton("S", moveButtonSize);
//                    ImGui::SameLine(0.0f, gapX);
//                    app.positioner_.movement_.right_ = movementButton("D", moveButtonSize);
//                    ImGui::End();
//
//                    const ImVec2 verticalButtonSize(buttonSize * 1.25f, buttonSize);
//                    ImGui::SetNextWindowPos(ImVec2(screenWidth - verticalButtonSize.x - 18.0f,
//                                                   screenHeight - (2.0f * buttonSize + gapY) - 28.0f),
//                                            ImGuiCond_Always);
//                    ImGui::Begin("##vertical-movement", nullptr,
//                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
//                                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavInputs |
//                                     ImGuiWindowFlags_NoSavedSettings);
//                    app.positioner_.movement_.up_ = movementButton("Up", verticalButtonSize);
//                    app.positioner_.movement_.down_ = movementButton("Down", verticalButtonSize);
//                    ImGui::End();
//#endif
//                    app.imgui_->endFrame(buf);
//                    buf.cmdEndRendering();
//                    buf.cmdPopDebugGroupLabel();
//                    LVK_PROFILER_ZONE_END();
//                }
//                else
//                {
//                    buf.cmdEndRendering();
//                }
            }
            buf.cmdPopDebugGroupLabel();

            LVK_PROFILER_ZONE("Submit frame", LVK_PROFILER_COLOR_SUBMIT);
            culling.storeSubmitHandle(app.submitFrame(buf));
            LVK_PROFILER_ZONE_END();

            LVK_PROFILER_ZONE("Read GPU stats", LVK_PROFILER_COLOR_WAIT);
            culling.retrieveGpuStats(ctx, app.fpsCounter_.numFrames_);
            LVK_PROFILER_ZONE_END();

            LVK_PROFILER_ZONE("Swap HDR luminance", 0xffffff80);
            hdr.swapAdaptedLuminance();
            LVK_PROFILER_ZONE_END();
        });

    VULKAN_APP_EXIT();
}
