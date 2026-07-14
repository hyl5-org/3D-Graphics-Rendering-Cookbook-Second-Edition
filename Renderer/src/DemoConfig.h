#pragma once

#include "shared/Tonemap.h"
#include "shared/VulkanApp.h"

#define DEMO_TEXTURE_MAX_SIZE 512
#if defined(ANDROID) || defined(COOKBOOK_ANDROID_TEXTURE)
#define DEMO_TEXTURE_CACHE_FOLDER ".cache/out_textures_11_astc/"

#define DEMO_TEXTURE_COMPRESSION_ASTC 1

// #define DEMO_TEXTURE_COMPRESSION_ASTC_FORMAT_4x4
#define DEMO_TEXTURE_COMPRESSION_ASTC_FORMAT_6x6 1
// #define DEMO_TEXTURE_COMPRESSION_ASTC_FORMAT_8x8

#if DEMO_TEXTURE_COMPRESSION_ASTC_FORMAT_6x6
#define DEMO_TEXTURE_GL_INTERNAL_FORMAT GL_COMPRESSED_RGBA_ASTC_6x6_KHR
#define DEMO_TEXTURE_VK_FORMAT VK_FORMAT_ASTC_6x6_UNORM_BLOCK
#define DEMO_TEXTURE_KTX_FORMAT KTX_PACK_ASTC_BLOCK_DIMENSION_6x6

#elif DEMO_TEXTURE_COMPRESSION_ASTC_FORMAT_4x4

#define DEMO_TEXTURE_GL_INTERNAL_FORMAT GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define DEMO_TEXTURE_VK_FORMAT VK_FORMAT_ASTC_4x4_UNORM_BLOCK
#define DEMO_TEXTURE_KTX_FORMAT KTX_PACK_ASTC_BLOCK_DIMENSION_4x4

#elif DEMO_TEXTURE_COMPRESSION_ASTC_FORMAT_8x8

#define DEMO_TEXTURE_GL_INTERNAL_FORMAT GL_COMPRESSED_RGBA_ASTC_8x8_KHR
#define DEMO_TEXTURE_VK_FORMAT VK_FORMAT_ASTC_8x8_UNORM_BLOCK
#define DEMO_TEXTURE_KTX_FORMAT KTX_PACK_ASTC_BLOCK_DIMENSION_8x8

#endif

#define fileNameCachedMeshes ".cache/ch11_bistro_android.meshes"
#define fileNameCachedMaterials ".cache/ch11_bistro_android.materials"
#define fileNameCachedHierarchy ".cache/ch11_bistro_android.scene"
#else
#define DEMO_TEXTURE_CACHE_FOLDER ".cache/out_textures_11/"
#define fileNameCachedMeshes ".cache/ch11_bistro.meshes"
#define fileNameCachedMaterials ".cache/ch11_bistro.materials"
#define fileNameCachedHierarchy ".cache/ch11_bistro.scene"
#endif

namespace FinalDemo
{

constexpr uint32_t kNumSamples = 1;
constexpr lvk::Format kOffscreenFormat = lvk::Format_R11G11B10_F;
constexpr lvk::Format kHDRBloomFormat = lvk::Format_RGBA_F16;
constexpr lvk::Dimensions kBloomSize = {512, 512};
constexpr uint32_t kHorizontal = 1;
constexpr uint32_t kVertical = 0;
//constexpr uint32_t kMultiViewLayerCount = 1;
constexpr uint32_t kMultiViewLayerCount = 2; // 2 views for stereo rendering
constexpr uint32_t kMultiViewViewMask  = 0b11; // 2 views for stereo rendering
enum CullingMode
{
    CullingMode_None = 0,
    CullingMode_CPU = 1,
    //CullingMode_GPU = 2,
};

struct DrawSettings
{
    bool meshesOpaque = true;
    bool meshesTransparent = false;
    bool boxes = false;
    bool lightFrustum = false;
};

struct OITSettings
{
    bool showHeatmap = false;
    float opacityBoost = 0.0f;
};

struct HDRSettings
{
    bool drawCurves = false;
    bool enableBloom = true;
    bool enableAdaptation = true;
    float bloomStrength = 0.01f;
    int numBloomPasses = 2;
    float adaptationSpeed = 3.0f;
};

struct CullingSettings
{
    //mat4 view = mat4(1.0f);
    int mode = CullingMode_CPU;
    bool freezeView = false;
};

struct LightParams
{
    float theta = +90.0f;
    float phi = -26.0f;
    vec3 color = vec3(1.0f);
    float intensity = 1.0f;
    float iblIntensity = 0.25f;
    float depthBiasConst = 1.1f;
    float depthBiasSlope = 2.0f;

    bool operator==(const LightParams &) const = default;
};

struct ViewContext
{
    std::array<mat4, kMultiViewLayerCount> view = {mat4(1.0f), mat4(1.0f)};
    std::array<mat4, kMultiViewLayerCount> projection = {mat4(1.0f), mat4(1.0f)};
    std::array<mat4, kMultiViewLayerCount> viewProjection = {mat4(1.0f), mat4(1.0f)};
    std::array<mat4, kMultiViewLayerCount> inverseViewProjection = {mat4(1.0f), mat4(1.0f)};
    std::array<vec4, kMultiViewLayerCount> cameraPosition = {vec4(0.0f), vec4(0.0f)};

    void update(const VulkanApp &app, float zNear, float zFar);
};

struct DemoSettings
{
    DrawSettings draw;
    OITSettings oit;
    HDRSettings hdr;
    CullingSettings culling;
    LightParams light;
    ViewContext view;
};

struct FrameTargets
{
    lvk::Dimensions sizeFb;
    lvk::Holder<lvk::TextureHandle> sceneColor;    // R11G11B10F scene color
    lvk::Holder<lvk::TextureHandle> lightingColor; // R11G11B10F deferred lighting output
    lvk::Holder<lvk::TextureHandle> gbufferRT1; // A2B10G10R10_UNORM octahedron encoding world normal : xy | unused : zw
    lvk::Holder<lvk::TextureHandle> gbufferRT2; // RGBA8_UNORMmetallic, specular, roughness, shadingmodelid
    lvk::Holder<lvk::TextureHandle> gbufferRT3; // RGBA8_UNORM basecolor : xyz | ao : w

    lvk::Holder<lvk::TextureHandle> opaqueDepth; // D24S8
};

struct LightFrame
{
    vec3 dir;
    mat4 view;
    mat4 proj;
};

extern DemoSettings gSettings;

void installKeyboardShortcuts(VulkanApp &app);

} // namespace FinalDemo
