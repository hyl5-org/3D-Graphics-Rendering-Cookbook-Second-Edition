#pragma once

#include <lvk/HelpersImGui.h>
#include <lvk/LVK.h>

#include <implot/implot.h>

#if defined(ANDROID)
#include <android_native_app_glue.h>
double glfwGetTime();
#else
#include <GLFW/glfw3.h>
#endif

#include <glm/ext.hpp>
#include <glm/glm.hpp>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "shared/UtilsFPS.h"
#include <shared/Bitmap.h>
#include <shared/Camera.h>
#include <shared/Graph.h>
#include <shared/Utils.h>
#include <shared/UtilsCubemap.h>

#include <functional>

using glm::mat3;
using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

#if defined(ANDROID)
#define VULKAN_APP_MAIN void android_main(android_app *androidApp)
#define VULKAN_APP_DECLARE(app, config) VulkanApp app(androidApp, config)
#define VULKAN_APP_EXIT() return
#else
#define VULKAN_APP_MAIN int main()
#define VULKAN_APP_DECLARE(app, config) VulkanApp app(config)
#define VULKAN_APP_EXIT() return 0
#endif

using DrawFrameFunc = std::function<void(uint32_t width, uint32_t height, float aspectRatio, float deltaSeconds)>;

struct GLTFMaterialIntro
{
    std::string name;
    uint32_t materialMask;
    uint32_t currentMaterialMask;
    bool modified = false;
};

struct GLTFIntrospective
{
    std::vector<std::string> cameras;
    uint32_t activeCamera = ~0u;

    std::vector<std::string> animations;
    std::vector<uint32_t> activeAnim;

    std::vector<std::string> extensions;
    std::vector<uint32_t> activeExtension;

    std::vector<GLTFMaterialIntro> materials;
    std::vector<bool> modifiedMaterial;

    float blend = 0.5f;

    bool showAnimations = false;
    bool showAnimationBlend = false;
    bool showCameras = false;
    bool showMaterials = true;
};

struct VulkanAppConfig
{
    vec3 initialCameraPos = vec3(0.0f, 0.0f, -2.5f);
    vec3 initialCameraTarget = vec3(0.0f, 0.0f, 0.0f);
    bool showGLTFInspector = false;
};

class VulkanApp
{
  public:
#if defined(ANDROID)
    explicit VulkanApp(android_app *androidApp, const VulkanAppConfig &cfg = {});
#else
    explicit VulkanApp(const VulkanAppConfig &cfg = {});
#endif
    virtual ~VulkanApp();

    virtual void run(DrawFrameFunc drawFrame);
    virtual void drawGrid(lvk::ICommandBuffer &buf, const mat4 &proj, const vec3 &origin = vec3(0.0f),
                          uint32_t numSamples = 1, lvk::Format colorFormat = lvk::Format_Invalid);
    virtual void drawGrid(lvk::ICommandBuffer &buf, const mat4 &mvp, const vec3 &origin, const vec3 &camPos,
                          uint32_t numSamples = 1, lvk::Format colorFormat = lvk::Format_Invalid);
    virtual void drawFPS();
    virtual void drawMemo();
    virtual void drawGTFInspector(GLTFIntrospective &intro);
    virtual void drawGTFInspector_Animations(GLTFIntrospective &intro);
    virtual void drawGTFInspector_Materials(GLTFIntrospective &intro);
    virtual void drawGTFInspector_Cameras(GLTFIntrospective &intro);

    lvk::Format getDepthFormat() const;
    lvk::TextureHandle getDepthTexture() const
    {
        return depthTexture_;
    }

#if !defined(ANDROID)
    void addMouseButtonCallback(GLFWmousebuttonfun cb)
    {
        callbacksMouseButton.push_back(cb);
    }
    void addKeyCallback(GLFWkeyfun cb)
    {
        callbacksKey.push_back(cb);
    }
#endif

  public:
    lvk::LVKwindow *window_ = nullptr;
#if defined(ANDROID)
    android_app *androidApp_ = nullptr;
#endif
    int width_ = 0;
    int height_ = 0;
    std::unique_ptr<lvk::IContext> ctx_;
    lvk::Holder<lvk::TextureHandle> depthTexture_;
    FramesPerSecondCounter fpsCounter_ = FramesPerSecondCounter(0.5f);
    double maxFPS_ = 0;
    std::unique_ptr<lvk::ImGuiRenderer> imgui_;
    ImPlotContext *implotCtx_ = nullptr;

    const VulkanAppConfig cfg_ = {};

    CameraPositioner_FirstPerson positioner_ = {cfg_.initialCameraPos, cfg_.initialCameraTarget,
                                                vec3(0.0f, 1.0f, 0.0f)};
    Camera camera_ = Camera(positioner_);

    struct MouseState
    {
        vec2 pos = vec2(0.0f);
        bool pressedLeft = false;
    } mouseState_;

  protected:
    lvk::Holder<lvk::ShaderModuleHandle> gridVert = {};
    lvk::Holder<lvk::ShaderModuleHandle> gridFrag = {};
    lvk::Holder<lvk::RenderPipelineHandle> gridPipeline = {};

    uint32_t pipelineSamples = 1;

#if !defined(ANDROID)
    std::vector<GLFWmousebuttonfun> callbacksMouseButton;
    std::vector<GLFWkeyfun> callbacksKey;
#endif
};
