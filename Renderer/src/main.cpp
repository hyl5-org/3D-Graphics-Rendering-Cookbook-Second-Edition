#include "DemoUI.h"
#include "RenderPasses.h"

#include "scene/VKMesh11Lazy.h"

#include <algorithm>
#include <chrono>
#include <lvk/vulkan/VulkanClasses.h>

using namespace FinalDemo;

// Run before acquiring the frame command buffer. LVK defers native compilation
// until getVkPipeline(); createRenderPipeline() alone only allocates a handle.
static double recompilePsos(lvk::IContext &ctx, lvk::RenderPipelineDesc desc, uint32_t &compiled,
                           uint32_t count = 30000)
{
    auto &vk = static_cast<lvk::VulkanContext &>(ctx);
    compiled = 0;
    const auto start = std::chrono::steady_clock::now();
    LVK_PROFILER_ZONE("PSO Recompile", LVK_PROFILER_COLOR_CMD_DRAW);
    const lvk::BlendFactor factors[] = {lvk::BlendFactor_One, lvk::BlendFactor_SrcColor,
                                       lvk::BlendFactor_OneMinusSrcColor, lvk::BlendFactor_DstColor,
                                       lvk::BlendFactor_OneMinusDstColor};
    for (uint32_t i = 0; i < count; ++i)
    {
        // 30 VS x 40 PS x (5 source blend factors x 5 destination factors) = 30000.
        const uint32_t vs = i % 30, ps = (i / 30) % 40, state = (i / 1200) % 25;
        const float constants[] = {1.0f + float(ps + 1) / 32.0f, 1.0f + float(vs + 1) / 32.0f};
        desc.specInfo = {.entries = {{.constantId = 0, .size = sizeof(float)},
                                     {.constantId = 1, .offset = sizeof(float), .size = sizeof(float)}},
                         .data = constants, .dataSize = sizeof(constants)};
        desc.color[0].blendEnabled = true;
        desc.color[0].srcRGBBlendFactor = factors[state % 5];
        desc.color[0].dstRGBBlendFactor = factors[state / 5];
        LVK_PROFILER_ZONE("PSO Create", LVK_PROFILER_COLOR_CMD_DRAW);
        auto pipeline = ctx.createRenderPipeline(desc);
        if (!pipeline.valid() || vk.getVkPipeline(pipeline, 0) == VK_NULL_HANDLE)
        {
            break;
        }
        ++compiled;
        pipeline.reset();
        LVK_PROFILER_ZONE_END();
        // Submit/retire deferred destruction in batches instead of retaining 30k PSOs.
        if (compiled % 256 == 0)
        {
            LVK_PROFILER_ZONE("PSO Batch Cleanup", LVK_PROFILER_COLOR_WAIT);
            ctx.wait(ctx.submit(ctx.acquireCommandBuffer()));
            LVK_PROFILER_ZONE_END();
        }
    }
    LVK_PROFILER_ZONE("PSO Final Cleanup", LVK_PROFILER_COLOR_WAIT);
    ctx.wait(ctx.submit(ctx.acquireCommandBuffer()));
    ctx.wait(ctx.submit(ctx.acquireCommandBuffer())); // Drain the last batch after its fence has completed.
    LVK_PROFILER_ZONE_END();
    LVK_PROFILER_ZONE_END();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

VULKAN_APP_MAIN
{
    const VulkanAppConfig appConfig{
        .initialCameraPos = vec3(-18.621f, 4.621f, -6.359f),
        .initialCameraTarget = vec3(0, +5.0f, 0),
        // VVL 1.4.357 + RTX 3080 (596.49): ray-query instrumentation loses the device
        // on both GPL and shader-object paths. Keep core validation and other GPU-AV checks.
        .enableGPUAVRayTracing = false,
    };

    VULKAN_APP_DECLARE(app, appConfig);

    app.positioner_.maxSpeed_ = 1.5f;
    // app.maxFPS_ = 60;
    installKeyboardShortcuts(app);

    LoadedScene loadedScene = loadDemoScene();
    LineCanvas3D canvas3d;

    std::unique_ptr<lvk::IContext> &ctx = app.ctx_;
    FrameTargets targets = createGBufferTargets(ctx, app.getDepthFormat());
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
    RenderShaders shaders(ctx);

    SSAOPass ssao(ctx, targets, samplerClamp, ctx->getSwapchainFormat());

    RTShadowAOPass rtShadowAO(ctx, targets, ctx->getSwapchainFormat());
    LightingPass lighting(ctx, targets, samplerClamp, ctx->getSwapchainFormat());

    OITPass oit(ctx, targets.sizeFb);

    HDRPass hdr(ctx, targets, samplerClamp, ctx->getSwapchainFormat());
    const Skybox skyBox(ctx, "data/immenstadter_horn_2k_prefilter.ktx", "data/immenstadter_horn_2k_irradiance.ktx",
                        kOffscreenFormat, app.getDepthFormat(), kNumSamples);
    VKMesh11Lazy mesh(ctx, loadedScene.meshData, loadedScene.scene);
    SceneDrawLists drawLists(ctx, loadedScene.meshData, mesh);
    SceneCulling culling(ctx, loadedScene, mesh);

    bool recompileRequested = false;
    uint32_t compiledPsos = 0;
    double psoCompileMs = 0;

    app.run(
        [&](uint32_t width, uint32_t height, float aspectRatio, float deltaSeconds)
        {
            LVK_PROFILER_FUNCTION();

            if (recompileRequested)
            {
                recompileRequested = false;
                psoCompileMs = recompilePsos(*ctx, {.smVert = hdr.vertToneMap,
                                                   .smFrag = hdr.fragToneMap,
                                                   .color = {{.format = ctx->getSwapchainFormat()}},
                                                   .debugName = "PSO benchmark"}, compiledPsos);
            }

            const mat4 view = app.camera_.getViewMatrix();
            const mat4 proj = glm::perspective(45.0f, aspectRatio, 0.01f, 200.0f);
            const CullingData cullingData = culling.prepare(proj, view, drawLists);
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
                culling.execute(ctx, buf, loadedScene, mesh, drawLists, cullingPipeline.pipeline, cullingData);
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
                shadows.updateIfNeeded(buf, mesh, shaders, lightFrame);
                buf.cmdPopDebugGroupLabel();
                LVK_PROFILER_ZONE_END();

                LVK_PROFILER_ZONE("Geometry pass", LVK_PROFILER_COLOR_CMD_DRAW);
                renderGbufferPass(ctx, app, buf, targets, loadedScene, skyBox, mesh, shaders, drawLists, oit, shadows,
                                  canvas3d, view, proj, lightFrame);
                LVK_PROFILER_ZONE_END();
                lvk::TextureHandle rtTex;
                LVK_PROFILER_ZONE("RT Shadow/AO", LVK_PROFILER_COLOR_CMD_DRAW);

                if (mesh.rayTracing_.valid() && (gSettings.rayTracing.shadows || gSettings.rayTracing.ao))
                {
                    rtShadowAO.execute(ctx, buf, targets, shadows, mesh.rayTracing_, view, proj, samplerClamp);
                    rtTex = rtShadowAO.denoise(buf, targets, view, proj, samplerClamp);
                }
                LVK_PROFILER_ZONE_END();

                lvk::TextureHandle currentColor;
                LVK_PROFILER_ZONE("Lighting pass", LVK_PROFILER_COLOR_CMD_DRAW);
                currentColor = lighting.execute(ctx, buf, targets, skyBox, shadows, view, proj, samplerClamp, rtTex);
                LVK_PROFILER_ZONE_END();

                LVK_PROFILER_ZONE("Skybox pass", LVK_PROFILER_COLOR_CMD_DRAW);
                renderSkyboxPass(buf, targets, skyBox, view, proj);
                LVK_PROFILER_ZONE_END();

                if (useOIT)
                {
                    LVK_PROFILER_ZONE("Transparent pass", LVK_PROFILER_COLOR_CMD_DRAW);
                    renderTransparentPass(ctx, app, buf, targets, skyBox, mesh, shaders, drawLists, oit, shadows,
                                          view, proj);
                    LVK_PROFILER_ZONE_END();
                }

                LVK_PROFILER_ZONE("Post process", LVK_PROFILER_COLOR_CMD_DISPATCH);
                currentColor = useOIT ? oit.combine(ctx, buf, targets, currentColor) : currentColor;

                hdr.execute(buf, currentColor, deltaSeconds, samplerClamp);
                LVK_PROFILER_ZONE_END();

                lvk::TextureHandle swapchainTexture;
                LVK_PROFILER_ZONE("Acquire swapchain texture", LVK_PROFILER_COLOR_PRESENT);
                swapchainTexture = ctx->getCurrentSwapchainTexture();
                LVK_PROFILER_ZONE_END();
                const lvk::Framebuffer framebufferMain = {
                    .color = {{.texture = swapchainTexture}},
                };

                LVK_PROFILER_ZONE("Tone map", LVK_PROFILER_COLOR_CMD_DRAW);
                hdr.toneMap(buf, framebufferMain);
                LVK_PROFILER_ZONE_END();

                LVK_PROFILER_ZONE("ImGui", 0xff40ff40);
                buf.cmdPushDebugGroupLabel("ImGui", 0xff40ff40);
                app.imgui_->beginFrame(framebufferMain);
                app.drawFPS();
                app.drawMemo();
                drawControls(width, height, aspectRatio, culling, shadows, ssao, hdr, app);
                ImGui::Begin("PSO benchmark");
                recompileRequested = ImGui::Button("Recompile 30000 PSOs (blocks)");
                ImGui::TextUnformatted("30 VS x 40 PS x 25 blend states = 30000");
                ImGui::Text("Compiled: %u / 30000", compiledPsos);
                ImGui::Text("Total CPU time: %.3f ms (%.2f s)", psoCompileMs, psoCompileMs / 1000.0);
                ImGui::TextUnformatted("Includes creation + cleanup; driver cache retained.");
                ImGui::End();
#if defined(ANDROID)
                const float screenWidth = ImGui::GetIO().DisplaySize.x;
                const float screenHeight = ImGui::GetIO().DisplaySize.y;
                const float buttonSize = std::clamp(screenWidth * 0.11f, 88.0f, 150.0f);
                const float gapX = ImGui::GetStyle().ItemSpacing.x;
                const float gapY = ImGui::GetStyle().ItemSpacing.y;
                const ImVec2 moveButtonSize(buttonSize, buttonSize);
                auto movementButton = [](const char *label, const ImVec2 &size)
                {
                    ImGui::Button(label, size);
                    return ImGui::IsItemActive();
                };

                ImGui::SetNextWindowPos(ImVec2(18.0f, screenHeight - (2.0f * buttonSize + gapY) - 28.0f),
                                        ImGuiCond_Always);
                ImGui::Begin("##movement", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavInputs |
                                 ImGuiWindowFlags_NoSavedSettings);
                ImGui::Dummy(moveButtonSize);
                ImGui::SameLine(0.0f, gapX);
                app.positioner_.movement_.forward_ = movementButton("W", moveButtonSize);
                ImGui::SameLine(0.0f, gapX);
                ImGui::Dummy(moveButtonSize);
                app.positioner_.movement_.left_ = movementButton("A", moveButtonSize);
                ImGui::SameLine(0.0f, gapX);
                app.positioner_.movement_.backward_ = movementButton("S", moveButtonSize);
                ImGui::SameLine(0.0f, gapX);
                app.positioner_.movement_.right_ = movementButton("D", moveButtonSize);
                ImGui::End();

                const ImVec2 verticalButtonSize(buttonSize * 1.25f, buttonSize);
                ImGui::SetNextWindowPos(ImVec2(screenWidth - verticalButtonSize.x - 18.0f,
                                               screenHeight - (2.0f * buttonSize + gapY) - 28.0f),
                                        ImGuiCond_Always);
                ImGui::Begin("##vertical-movement", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavInputs |
                                 ImGuiWindowFlags_NoSavedSettings);
                app.positioner_.movement_.up_ = movementButton("Up", verticalButtonSize);
                app.positioner_.movement_.down_ = movementButton("Down", verticalButtonSize);
                ImGui::End();
#endif
                app.imgui_->endFrame(buf);
                buf.cmdEndRendering();
                buf.cmdPopDebugGroupLabel();
                LVK_PROFILER_ZONE_END();
            }
            buf.cmdPopDebugGroupLabel();

            LVK_PROFILER_ZONE("Submit frame", LVK_PROFILER_COLOR_SUBMIT);
            culling.storeSubmitHandle(ctx->submit(buf, ctx->getCurrentSwapchainTexture()));
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
