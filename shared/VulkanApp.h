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
#include "shared/OpenXRArraySwapchain.h"
#include <shared/Bitmap.h>
#include <shared/Camera.h>
#include <shared/Graph.h>
#include <shared/Utils.h>
#include <shared/UtilsCubemap.h>

#include <functional>
#include <array>

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
    lvk::ContextConfig contextConfig = {.enableValidation = false, .enableValidationBestPractices = false};
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    bool enableOpenXR = false;
#endif
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
    lvk::Format getColorFormat() const;
    lvk::Dimensions getOutputDimensions() const;
    lvk::TextureHandle getCurrentOutputTexture();
    lvk::SubmitHandle submitFrame(lvk::ICommandBuffer &buf);
    bool isOpenXR() const;
    mat4 getEyeViewMatrix(uint32_t eye) const;
    mat4 getEyeProjectionMatrix(uint32_t eye, float zNear, float zFar) const;
    mat4 getEyeViewProjectionMatrix(uint32_t eye, float zNear,
                                    float zFar) const;
    vec3 getEyePosition(uint32_t eye) const;
    bool isXrVisibilityMaskSupported() const
    {
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
        return xrGetVisibilityMaskKHR_ != nullptr;
#else
        return false;
#endif
    }
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    void getXrVisibilityMask(uint32_t eye, std::vector<XrVector2f> &vertices,
                             std::vector<uint32_t> &indices) const;
#endif
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

    VulkanAppConfig cfg_ = {};

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
    lvk::TextureHandle currentOutputTexture_ = {};

#if !defined(ANDROID)
    std::vector<GLFWmousebuttonfun> callbacksMouseButton;
    std::vector<GLFWkeyfun> callbacksKey;
#endif

#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    void initOpenXR();
    void initXrSession();
    void initXrActions();
    void initXrSwapchain();
    void destroyOpenXR();
    void pollXrEvents();
    bool renderXrFrame(DrawFrameFunc &drawFrame);
    void syncXrActions();
    void updateXrUiInput(XrTime displayTime, float deltaSeconds);
    void updateXrLocomotion(float deltaSeconds);
    mat4 getXrWorldFromLocalMatrix() const;
    mat4 getXrLocalFromViewMatrix(uint32_t eye) const;
    mat4 getXrProjectionMatrix(uint32_t eye, float zNear, float zFar) const;
    vec3 getXrEyeWorldPosition(uint32_t eye) const;
    XrInstance xrInstance_ = XR_NULL_HANDLE;
    XrSystemId xrSystemId_ = XR_NULL_SYSTEM_ID;
    XrSession xrSession_ = XR_NULL_HANDLE;
    PFN_xrGetVisibilityMaskKHR xrGetVisibilityMaskKHR_ = nullptr;
    XrSpace xrAppSpace_ = XR_NULL_HANDLE;
    XrSessionState xrSessionState_ = XR_SESSION_STATE_UNKNOWN;
    bool xrSessionRunning_ = false;
    bool xrShouldQuit_ = false;
    double xrLastTimeStamp_ = 0.0;

    lvk::OpenXRVulkanExtensionStrings xrVulkanExts_;
    std::vector<XrViewConfigurationView> xrConfigViews_;
    XrView xrViews_[2] = {{.type = XR_TYPE_VIEW}, {.type = XR_TYPE_VIEW}};
    OpenXRArraySwapchain xrColorSwapchain_;
    XrActionSet xrActionSet_ = XR_NULL_HANDLE;
    XrAction xrMoveAction_ = XR_NULL_HANDLE;
    XrAction xrTurnAction_ = XR_NULL_HANDLE;
    XrAction xrUiAimAction_ = XR_NULL_HANDLE;
    XrAction xrUiClickAction_ = XR_NULL_HANDLE;
    XrSpace xrUiAimSpace_ = XR_NULL_HANDLE;
    XrPath xrLeftHandPath_ = XR_NULL_PATH;
    XrPath xrRightHandPath_ = XR_NULL_PATH;
    vec2 xrMoveInput_ = vec2(0.0f);
    vec2 xrTurnInput_ = vec2(0.0f);
    bool xrUiClickInput_ = false;
    bool xrUiPointerValid_ = false;
    vec3 xrPlayerPosition_ = vec3(0.0f);
    float xrPlayerYaw_ = 0.0f;
    bool xrSnapTurnReady_ = true;
    bool xrViewsValid_ = false;
#endif
};
