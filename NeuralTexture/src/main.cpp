/* Minimal multi-channel neural texture compression for the Rendering Cookbook. */
#include "CooperativeDecoder.h"
#include "shared/VulkanApp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
struct Draw
{
    uint32_t first, count, material;
    float cutoff;
};
struct Frame
{
    mat4 viewProj;
    vec4 camera;
    uint32_t grid, materials, size, source;
    uint32_t channel;
    float width;
    uint32_t manualGamma;
    uint32_t secondLayerOffset;
};
struct Push
{
    uint64_t frame, vertices, model, reference, weights;
    uint32_t material;
    float cutoff;
};
static_assert(offsetof(Frame, grid) == 80 && offsetof(Frame, channel) == 96);
static_assert(offsetof(Frame, secondLayerOffset) == 108 && sizeof(Frame) == 112 && sizeof(Push) == 48);

std::vector<char> readFile(const std::string &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("Cannot open " + path);
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || size > std::numeric_limits<uint32_t>::max())
    {
        throw std::runtime_error("Invalid size: " + path);
    }
    std::vector<char> data(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(data.data(), size))
    {
        throw std::runtime_error("Cannot read " + path);
    }
    return data;
}
void validateDecoders(lvk::IContext *ctx, lvk::BufferHandle frame, lvk::BufferHandle model,
                      const CooperativeDecoder &cooperative, uint32_t materials, uint32_t grid)
{
    if (!cooperative.weights.valid())
    {
        throw std::runtime_error("--validate-decode requires FP16 Cooperative Vector support");
    }

    constexpr uint32_t size = 64;
    constexpr uint32_t channels = 9;
    const Frame data{
        .grid = grid, .materials = materials, .size = size, .secondLayerOffset = cooperative.secondLayerOffset};
    std::array<lvk::Holder<lvk::ShaderModuleHandle>, 3> shaders;
    std::array<lvk::Holder<lvk::ComputePipelineHandle>, 3> pipelines;
    const char *paths[] = {NTC_VALIDATE_SPV, NTC_VALIDATE_FP16_SPV, NTC_VALIDATE_COOP_SPV};
    for (uint32_t decoder = 0; decoder < 3; ++decoder)
    {
        const std::vector<char> code = readFile(paths[decoder]);
        shaders[decoder] =
            ctx->createShaderModule({code.data(), code.size(), lvk::Stage_Comp, "NTC decode validation"});
        pipelines[decoder] =
            ctx->createComputePipeline({.smComp = shaders[decoder], .debugName = "NTC decode validation"});
        if (!pipelines[decoder].valid())
        {
            throw std::runtime_error("Cannot create decode validation pipeline");
        }
    }
    lvk::Holder<lvk::BufferHandle> output = ctx->createBuffer({.usage = lvk::BufferUsageBits_Storage,
                                                               .storage = lvk::StorageType_HostVisible,
                                                               .size = size * size * channels * sizeof(float),
                                                               .debugName = "NTC validation output"});
    const std::vector<float> sentinel(size * size * channels, std::numeric_limits<float>::quiet_NaN());
    std::array<std::vector<float>, 3> decoded;
    std::array<float, 3> maxErrors = {};
    std::array<double, 3> squaredErrors = {};
    const uint32_t pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    for (uint32_t material = 0; material < materials; ++material)
    {
        for (uint32_t decoder = 0; decoder < 3; ++decoder)
        {
            const lvk::Result upload = ctx->upload(output, sentinel.data(), sentinel.size() * sizeof(float));
            if (!upload.isOk())
            {
                throw std::runtime_error("Cannot initialize decode validation output");
            }
            const lvk::BufferHandle weightBuffer =
                decoder == 2 ? lvk::BufferHandle(cooperative.weights) : lvk::BufferHandle(cooperative.scalarWeights);
            lvk::ICommandBuffer &cmd = ctx->acquireCommandBuffer();
            cmd.cmdUpdateBuffer(frame, data);
            cmd.cmdBindComputePipeline(pipelines[decoder]);
            const Push push{.frame = ctx->gpuAddress(frame),
                            .model = ctx->gpuAddress(model),
                            .reference = ctx->gpuAddress(output),
                            .weights = ctx->gpuAddress(weightBuffer),
                            .material = material};
            cmd.cmdPushConstants(push);
            cmd.cmdDispatch({size * size / 64}, {.buffers = {frame, model, output, weightBuffer}});
            cmd.cmdBarrier({.srcStages = lvk::PipelineStageBits_Compute,
                            .srcAccess = lvk::AccessBits_ShaderWrite,
                            .dstStages = lvk::PipelineStageBits_Host,
                            .dstAccess = lvk::AccessBits_HostRead});
            ctx->wait(ctx->submit(cmd));
            decoded[decoder].assign(size * size * channels, std::numeric_limits<float>::quiet_NaN());
            const lvk::Result result =
                ctx->download(output, decoded[decoder].data(), decoded[decoder].size() * sizeof(float), 0);
            if (!result.isOk())
            {
                throw std::runtime_error("Cannot read decode validation output");
            }
        }
        for (uint32_t pair = 0; pair < 3; ++pair)
        {
            for (size_t i = 0; i < decoded[0].size(); ++i)
            {
                const float first = decoded[pairs[pair][0]][i];
                const float second = decoded[pairs[pair][1]][i];
                if (!std::isfinite(first) || !std::isfinite(second))
                {
                    throw std::runtime_error("Non-finite decoded channel");
                }
                const float error = std::abs(first - second);
                maxErrors[pair] = std::max(maxErrors[pair], error);
                squaredErrors[pair] += double(error) * error;
            }
        }
    }
    const char *labels[] = {"Scalar FP32 vs Scalar FP16", "Scalar FP32 vs CoopVec FP16", "Scalar FP16 vs CoopVec FP16"};
    for (uint32_t pair = 0; pair < 3; ++pair)
    {
        const double rmse = std::sqrt(squaredErrors[pair] / (double(materials) * size * size * channels));
        std::printf("%s: %u materials, %u samples/material, max error %.7f, RMSE %.7f\n", labels[pair], materials,
                    size * size, maxErrors[pair], rmse);
        if (maxErrors[pair] > 0.01f)
        {
            throw std::runtime_error("Decoder error exceeds 0.01 on normalized material channels");
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    try
    {
        std::string path = "sponza.ntc";
        std::string requestedDecoder = "auto";
        bool validateDecode = false;
        for (int i = 1; i < argc; ++i)
        {
            if (!std::strcmp(argv[i], "--validate-decode"))
            {
                validateDecode = true;
            }
            else if (!std::strcmp(argv[i], "--ntc") || !std::strcmp(argv[i], "--decoder"))
            {
                const bool modelPath = !std::strcmp(argv[i], "--ntc");
                if (++i == argc)
                {
                    throw std::runtime_error("Missing command-line argument");
                }
                if (modelPath)
                {
                    path = argv[i];
                }
                else
                {
                    requestedDecoder = argv[i];
                }
            }
        }
        if (requestedDecoder != "auto" && requestedDecoder != "scalar" && requestedDecoder != "fp16" &&
            requestedDecoder != "fp32" && requestedDecoder != "coop")
        {
            throw std::runtime_error("--decoder must be auto, fp32, fp16 or coop");
        }
        const std::string stem = path.substr(0, path.find_last_of('.'));
        const std::vector<char> modelData = readFile(path), scene = readFile(stem + ".scene"),
                                reference = readFile(stem + ".ref");
        std::array<uint32_t, 8> h = {};
        std::array<uint32_t, 4> s = {};
        if (modelData.size() < sizeof(h) || scene.size() < sizeof(s))
        {
            throw std::runtime_error("Truncated package header");
        }
        std::memcpy(h.data(), modelData.data(), sizeof(h));
        std::memcpy(s.data(), scene.data(), sizeof(s));
        const uint64_t modelFloats = uint64_t(h[2]) * h[4] * h[4] * 8 + 8 * 32 + 32 + 32 * 9 + 9;
        if (h[0] != 0x3143544e || h[1] != 1 || !h[2] || !h[3] || !h[4] || h[5] != 8 || h[6] != 32 || h[7] != 9 ||
            modelData.size() != sizeof(h) + modelFloats * 4 || reference.size() != uint64_t(h[2]) * h[3] * h[3] * 9 ||
            s[0] != 0x3143534e || !s[1] || !s[2] || !s[3] ||
            scene.size() != 16 + uint64_t(s[1]) * 16 + uint64_t(s[2]) * 48 + uint64_t(s[3]) * 4)
        {
            throw std::runtime_error("Invalid NTC package or unsupported network");
        }
        std::vector<Draw> draws(s[1]);
        std::memcpy(draws.data(), scene.data() + 16, draws.size() * sizeof(Draw));
        for (const Draw &d : draws)
        {
            if (d.material >= h[2] || uint64_t(d.first) + d.count > s[3])
            {
                throw std::runtime_error("Invalid draw");
            }
        }
        const VulkanAppConfig cfg{.initialCameraPos = vec3(-8, 1.7f, 0), .initialCameraTarget = vec3(0, 1.7f, 0)};
        VULKAN_APP_DECLARE(app, cfg);
        lvk::IContext *ctx = app.ctx_.get();
        if (validateDecode)
        {
            glfwHideWindow(app.window_);
        }
        const float *parameters = reinterpret_cast<const float *>(modelData.data() + sizeof(h));
        const size_t firstWeightOffset = size_t(h[2]) * h[4] * h[4] * 8;
        const CooperativeDecoder cooperative =
            createCooperativeDecoder(ctx, parameters + firstWeightOffset, parameters + firstWeightOffset + 8 * 32 + 32);
        std::printf("%s\n", cooperative.status.c_str());
        int decoder = cooperative.weights.valid() ? 2 : 1;
        if (requestedDecoder == "fp32")
        {
            decoder = 0;
        }
        if (requestedDecoder == "fp16" || requestedDecoder == "scalar")
        {
            decoder = 1;
        }
        const uint64_t vertexOffset = 16 + draws.size() * sizeof(Draw);
        lvk::Holder<lvk::BufferHandle> vertices = ctx->createBuffer({.usage = lvk::BufferUsageBits_Storage,
                                                                     .storage = lvk::StorageType_Device,
                                                                     .size = uint64_t(s[2]) * 48,
                                                                     .data = scene.data() + vertexOffset,
                                                                     .debugName = "NTC vertices"});
        lvk::Holder<lvk::BufferHandle> indices =
            ctx->createBuffer({.usage = lvk::BufferUsageBits_Index,
                               .storage = lvk::StorageType_Device,
                               .size = uint64_t(s[3]) * 4,
                               .data = scene.data() + vertexOffset + uint64_t(s[2]) * 48,
                               .debugName = "NTC indices"});
        lvk::Holder<lvk::BufferHandle> model = ctx->createBuffer({.usage = lvk::BufferUsageBits_Storage,
                                                                  .storage = lvk::StorageType_Device,
                                                                  .size = modelData.size() - sizeof(h),
                                                                  .data = modelData.data() + sizeof(h),
                                                                  .debugName = "NTC latent and MLP"});
        lvk::Holder<lvk::BufferHandle> ref = ctx->createBuffer({.usage = lvk::BufferUsageBits_Storage,
                                                                .storage = lvk::StorageType_Device,
                                                                .size = reference.size(),
                                                                .data = reference.data(),
                                                                .debugName = "Uncompressed comparison only"});
        lvk::Holder<lvk::BufferHandle> frame = ctx->createBuffer({.usage = lvk::BufferUsageBits_Storage,
                                                                  .storage = lvk::StorageType_Device,
                                                                  .size = sizeof(Frame),
                                                                  .debugName = "NTC frame"});
        if (validateDecode)
        {
            validateDecoders(ctx, frame, model, cooperative, h[2], h[4]);
            return 0;
        }
        const std::vector<char> vertexCode = readFile(NTC_VERTEX_SPV);
        lvk::Holder<lvk::ShaderModuleHandle> vert =
            ctx->createShaderModule({vertexCode.data(), vertexCode.size(), lvk::Stage_Vert, "NTC vertex"});
        std::array<lvk::Holder<lvk::ShaderModuleHandle>, 3> fragments;
        std::array<lvk::Holder<lvk::RenderPipelineHandle>, 3> pipelines;
        const char *fragmentPaths[] = {NTC_FRAGMENT_SPV, NTC_FRAGMENT_FP16_SPV, NTC_FRAGMENT_COOP_SPV};
        const char *decoderNames[] = {"NTC scalar FP32", "NTC scalar FP16", "NTC CoopVec FP16"};
        const uint32_t decoderCount = cooperative.weights.valid() ? 3 : 2;
        for (uint32_t index = 0; index < decoderCount; ++index)
        {
            const std::vector<char> code = readFile(fragmentPaths[index]);
            fragments[index] =
                ctx->createShaderModule({code.data(), code.size(), lvk::Stage_Frag, "NTC fragment inference"});
            if (!vert.valid() || !fragments[index].valid())
            {
                throw std::runtime_error("Shader compilation failed");
            }
            pipelines[index] = ctx->createRenderPipeline({.smVert = vert,
                                                          .smFrag = fragments[index],
                                                          .color = {{.format = ctx->getSwapchainFormat()}},
                                                          .depthFormat = app.getDepthFormat(),
                                                          .cullMode = lvk::CullMode_None,
                                                          .debugName = decoderNames[index]});
            if (!pipelines[index].valid())
            {
                throw std::runtime_error("Pipeline creation failed");
            }
        }
        int source = 2, channel = 0;
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (!std::strcmp(argv[i], "--source"))
            {
                source = std::atoi(argv[++i]);
            }
        }
        if (source < 0 || source > 2)
        {
            throw std::runtime_error("--source must be 0, 1 or 2");
        }
        const double ratio = double(reference.size()) / modelData.size();
        std::printf("NTC: %u materials, %ux%u, %.2f KiB vs %.2f KiB raw, %.2fx (geometry excluded)\n", h[2], h[3], h[3],
                    modelData.size() / 1024.0, reference.size() / 1024.0, ratio);
        app.run(
            [&](uint32_t width, uint32_t height, float aspectRatio, float)
            {
                const Frame data{.viewProj = glm::perspective(glm::radians(60.0f), aspectRatio, .1f, 100.0f) *
                                             app.camera_.getViewMatrix(),
                                 .camera = vec4(app.positioner_.getPosition(), 1),
                                 .grid = h[4],
                                 .materials = h[2],
                                 .size = h[3],
                                 .source = uint32_t(source),
                                 .channel = uint32_t(channel),
                                 .width = float(width),
                                 .manualGamma = ctx->getSwapchainFormat() == lvk::Format_BGRA_UN8 ||
                                                ctx->getSwapchainFormat() == lvk::Format_RGBA_UN8,
                                 .secondLayerOffset = cooperative.secondLayerOffset};
                const lvk::BufferHandle weightBuffer = decoder == 2 ? lvk::BufferHandle(cooperative.weights)
                                                                    : lvk::BufferHandle(cooperative.scalarWeights);
                lvk::ICommandBuffer &cmd = ctx->acquireCommandBuffer();
                cmd.cmdUpdateBuffer(frame, data);
                const lvk::Framebuffer fb{.color = {{.texture = ctx->getCurrentSwapchainTexture()}},
                                          .depthStencil = {app.getDepthTexture()}};
                cmd.cmdBeginRendering({.color = {{.loadOp = lvk::LoadOp_Clear, .clearColor = {.03f, .03f, .03f, 1}}},
                                       .depth = {.loadOp = lvk::LoadOp_Clear, .clearDepth = 1}},
                                      fb, {.buffers = {vertices, model, ref, frame, weightBuffer}});
                cmd.cmdBindRenderPipeline(pipelines[decoder]);
                cmd.cmdBindViewport({0, 0, float(width), float(height)});
                cmd.cmdBindScissorRect({0, 0, width, height});
                cmd.cmdBindDepthState({.compareOp = lvk::CompareOp_Less, .isDepthWriteEnabled = true});
                cmd.cmdBindIndexBuffer(indices, lvk::IndexFormat_UI32);
                cmd.cmdPushDebugGroupLabel(decoderNames[decoder], 0xff40a0ff);
                for (const Draw &d : draws)
                {
                    const Push pc{.frame = ctx->gpuAddress(frame),
                                  .vertices = ctx->gpuAddress(vertices),
                                  .model = ctx->gpuAddress(model),
                                  .reference = ctx->gpuAddress(ref),
                                  .weights = ctx->gpuAddress(weightBuffer),
                                  .material = d.material,
                                  .cutoff = d.cutoff};
                    cmd.cmdPushConstants(pc);
                    cmd.cmdDrawIndexed(d.count, 1, d.first);
                }
                cmd.cmdPopDebugGroupLabel();
                app.imgui_->beginFrame(fb);
                ImGui::Begin("Minimal neural PBR textures");
                if (cooperative.weights.valid())
                {
                    ImGui::Combo("Decoder", &decoder, "Scalar FP32\0Scalar FP16\0Cooperative Vector FP16\0");
                }
                else
                {
                    ImGui::Combo("Decoder", &decoder, "Scalar FP32\0Scalar FP16\0");
                    ImGui::TextUnformatted(cooperative.status.c_str());
                }
                ImGui::Combo("Source", &source, "Reference\0Neural\0Split: reference | neural\0");
                ImGui::Combo("View", &channel, "PBR\0Base color\0Tangent normal\0Roughness\0Metallic\0AO\0Alpha\0");
                ImGui::Text("%u materials | %u x %u | latent %u x %u x 8 | MLP 8-32-9", h[2], h[3], h[3], h[4], h[4]);
                ImGui::Text("Compressed %.1f KiB / raw %.1f KiB = %.2fx", modelData.size() / 1024.0,
                            reference.size() / 1024.0, ratio);
                ImGui::TextUnformatted(
                    "FP32 latents; scalar FP32/FP16 or CoopVec FP16 MLP. No BC textures or mipmaps.");
                ImGui::End();
                app.drawFPS();
                app.imgui_->endFrame(cmd);
                cmd.cmdEndRendering();
                ctx->submit(cmd, ctx->getCurrentSwapchainTexture());
            });
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "%s\nRun train.py first, then --ntc <absolute path to sponza.ntc>\n", e.what());
        return 1;
    }
    VULKAN_APP_EXIT();
}
