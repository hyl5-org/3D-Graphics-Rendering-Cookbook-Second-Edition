#include "VulkanApp.h"

#include "UtilsGLTF.h"
#include <lvk/vulkan/VulkanUtils.h>
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

#if defined(ANDROID)
#include <android/native_window.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace
{
#ifndef VULKAN_APP_ANDROID_USE_NATIVE_RESOLUTION
#define VULKAN_APP_ANDROID_USE_NATIVE_RESOLUTION 0
#endif

#ifndef VULKAN_APP_ANDROID_WINDOW_WIDTH
#define VULKAN_APP_ANDROID_WINDOW_WIDTH 1600
#endif

#ifndef VULKAN_APP_ANDROID_WINDOW_HEIGHT
#define VULKAN_APP_ANDROID_WINDOW_HEIGHT 720
#endif

static void setAndroidWindowGeometry(ANativeWindow *window)
{
#if !VULKAN_APP_ANDROID_USE_NATIVE_RESOLUTION
    if (window)
    {
        ANativeWindow_setBuffersGeometry(window, VULKAN_APP_ANDROID_WINDOW_WIDTH, VULKAN_APP_ANDROID_WINDOW_HEIGHT, 0);
        LLOGL("Android window requested size: %dx%d", VULKAN_APP_ANDROID_WINDOW_WIDTH,
              VULKAN_APP_ANDROID_WINDOW_HEIGHT);
    }
#else
    if (window)
    {
        ANativeWindow_setBuffersGeometry(window, 0, 0, 0);
        LLOGL("Android window requested size: native");
    }
#endif
}

static void updateAndroidWindowSize(VulkanApp *app, ANativeWindow *window)
{
    setAndroidWindowGeometry(window);
    const int nativeWidth = ANativeWindow_getWidth(window);
    const int nativeHeight = ANativeWindow_getHeight(window);
#if !VULKAN_APP_ANDROID_USE_NATIVE_RESOLUTION
    app->width_ = VULKAN_APP_ANDROID_WINDOW_WIDTH;
    app->height_ = VULKAN_APP_ANDROID_WINDOW_HEIGHT;
#else
    app->width_ = nativeWidth;
    app->height_ = nativeHeight;
#endif
    LLOGL("Android native window size: %dx%d", nativeWidth, nativeHeight);
    LLOGL("Android render size: %dx%d", app->width_, app->height_);
}
} // namespace

double glfwGetTime()
{
    timespec t = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<double>(t.tv_sec) + 1.0e-9 * static_cast<double>(t.tv_nsec);
}

static void ensureDirectory(const char *path)
{
    mkdir(path, 0777);
}

static std::string setAndroidWorkingDirectory()
{
    const char *externalStorage = getenv("EXTERNAL_STORAGE");
    if (!externalStorage)
    {
        externalStorage = "/sdcard";
    }

    std::string root = std::string(externalStorage) + "/vrgraphics";
    ensureDirectory(root.c_str());
    ensureDirectory((root + "/logs").c_str());
    ensureDirectory((root + "/.cache").c_str());
    ensureDirectory((root + "/.cache/out_textures_11").c_str());
    ensureDirectory((root + "/.cache/out_textures_11_astc").c_str());
    chdir(root.c_str());
    return root;
}

static void createAppDepthTexture(VulkanApp *app)
{
    if (!app->ctx_ || !app->depthTexture_.empty() || app->width_ <= 0 || app->height_ <= 0)
    {
        return;
    }

    app->depthTexture_ = app->ctx_->createTexture({
        .type = lvk::TextureType_2D,
        .format = lvk::Format_Z_F32,
        .dimensions = {(uint32_t)app->width_, (uint32_t)app->height_},
        .usage = lvk::TextureUsageBits_Attachment,
        .debugName = "Depth buffer",
    });
}

static void handleAndroidCommand(android_app *androidApp, int32_t cmd)
{
    VulkanApp *app = (VulkanApp *)androidApp->userData;
    if (!app)
    {
        return;
    }

    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
        if (androidApp->window)
        {
            app->window_ = androidApp->window;
            updateAndroidWindowSize(app, androidApp->window);
            if (!app->ctx_)
            {
                app->ctx_ = lvk::createVulkanContextWithSwapchain(androidApp->window, app->width_, app->height_,
                                                                  {
                                                                      .enableValidation = false,
                                                                      .enableValidationBestPractices = false,
                                                                  });
                createAppDepthTexture(app);
            }
        }
        break;
    case APP_CMD_TERM_WINDOW:
        app->window_ = nullptr;
        androidApp->destroyRequested = 1;
        break;
    default:
        break;
    }
}

static int32_t handleAndroidInput(android_app *androidApp, AInputEvent *event)
{
    VulkanApp *app = (VulkanApp *)androidApp->userData;
    if (!app || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION || app->width_ <= 0 || app->height_ <= 0)
    {
        return 0;
    }

    const int32_t action = AMotionEvent_getAction(event);
    const int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
    const float x = AMotionEvent_getX(event, 0);
    const float y = AMotionEvent_getY(event, 0);
    const int nativeWidth = app->window_ ? ANativeWindow_getWidth(app->window_) : app->width_;
    const int nativeHeight = app->window_ ? ANativeWindow_getHeight(app->window_) : app->height_;
    const float inputWidth = static_cast<float>(nativeWidth > 0 ? nativeWidth : app->width_);
    const float inputHeight = static_cast<float>(nativeHeight > 0 ? nativeHeight : app->height_);
    const float renderX = x * static_cast<float>(app->width_) / inputWidth;
    const float renderY = y * static_cast<float>(app->height_) / inputHeight;

    app->mouseState_.pos.x = std::clamp(renderX / static_cast<float>(app->width_), 0.0f, 1.0f);
    app->mouseState_.pos.y = 1.0f - std::clamp(renderY / static_cast<float>(app->height_), 0.0f, 1.0f);
    ImGuiIO *io = ImGui::GetCurrentContext() ? &ImGui::GetIO() : nullptr;
    if (io)
    {
        io->MousePos = ImVec2(renderX, renderY);
    }

    switch (actionMasked)
    {
    case AMOTION_EVENT_ACTION_DOWN:
        if (io)
        {
            io->MouseDown[0] = true;
        }
        app->positioner_.resetMousePosition(app->mouseState_.pos);
        app->mouseState_.pressedLeft = true;
        return 1;
    case AMOTION_EVENT_ACTION_MOVE:
        return 1;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
        if (io)
        {
            io->MouseDown[0] = false;
        }
        app->mouseState_.pressedLeft = false;
        return 1;
    default:
        return 0;
    }
}

static void handleAndroidResize(ANativeActivity *activity, ANativeWindow *window)
{
    VulkanApp *app = (VulkanApp *)activity->instance;
    if (!app || !window || !app->ctx_)
    {
        return;
    }

    setAndroidWindowGeometry(window);
    const int nativeWidth = ANativeWindow_getWidth(window);
    const int nativeHeight = ANativeWindow_getHeight(window);
#if !VULKAN_APP_ANDROID_USE_NATIVE_RESOLUTION
    const int width = VULKAN_APP_ANDROID_WINDOW_WIDTH;
    const int height = VULKAN_APP_ANDROID_WINDOW_HEIGHT;
#else
    const int width = nativeWidth;
    const int height = nativeHeight;
#endif
    if (width > 0 && height > 0 && (app->width_ != width || app->height_ != height))
    {
        app->width_ = width;
        app->height_ = height;
        LLOGL("Android native window resized: %dx%d", nativeWidth, nativeHeight);
        LLOGL("Android render resized: %dx%d", app->width_, app->height_);
        app->ctx_->recreateSwapchain(width, height);
        app->depthTexture_ = nullptr;
    }
}
#endif

#if defined(ANDROID)
VulkanApp::VulkanApp(android_app *androidApp, const VulkanAppConfig &cfg) : androidApp_(androidApp), cfg_(cfg)
{
    const std::string root = setAndroidWorkingDirectory();
    const std::string logFile = root + "/logs/Renderer.log";
    if (!minilog::initialize(logFile.c_str(), {.threadNames = false}))
    {
        minilog::initialize(nullptr, {.threadNames = false});
    }
    LLOGL("Android log file: %s", logFile.c_str());

    androidApp_->userData = this;
    androidApp_->onAppCmd = handleAndroidCommand;
    androidApp_->onInputEvent = handleAndroidInput;

    int events = 0;
    android_poll_source *source = nullptr;
    while (!androidApp_->destroyRequested && !ctx_)
    {
        if (ALooper_pollOnce(1, nullptr, &events, (void **)&source) >= 0 && source)
        {
            source->process(androidApp_, source);
        }
    }

    if (!ctx_)
    {
        return;
    }

    androidApp_->activity->instance = this;
    androidApp_->activity->callbacks->onNativeWindowResized = handleAndroidResize;

    imgui_ = std::make_unique<lvk::ImGuiRenderer>(*ctx_, nullptr, "data/OpenSans-Light.ttf", 30.0f);
    implotCtx_ = ImPlot::CreateContext();
}
#else

VulkanApp::VulkanApp(const VulkanAppConfig &cfg) : cfg_(cfg)
{
    minilog::initialize(nullptr, {.threadNames = false});

    int width = -95;
    int height = -90;
    // width = 1280;
    // height = 720;

#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR)
    {
        glfwInit();
        initOpenXR();
        initXrSession();
        initXrSwapchain();
        width_ = static_cast<int>(xrColorSwapchain_.width);
        height_ = static_cast<int>(xrColorSwapchain_.height);
        depthTexture_ = ctx_->createTexture({
            .type = lvk::TextureType_2D,
            .format = lvk::Format_Z_F32,
            .dimensions = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)},
            .numLayers = 2,
            .usage = lvk::TextureUsageBits_Attachment,
            .debugName = "Depth buffer",
        });
        xrLastTimeStamp_ = glfwGetTime();
        imgui_ = std::make_unique<lvk::ImGuiRenderer>(*ctx_, nullptr, "data/OpenSans-Light.ttf", 30.0f);
        implotCtx_ = ImPlot::CreateContext();
        return;
    }
#endif

    window_ = lvk::initWindow("Simple example", width, height);
    ctx_ = lvk::createVulkanContextWithSwapchain(window_, width, height, cfg_.contextConfig);
    width_ = width;
    height_ = height;
    // recommend to use vulkan configurator on windows
    depthTexture_ = ctx_->createTexture({
        .type = lvk::TextureType_2D,
        .format = lvk::Format_Z_F32,
        .dimensions = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)},
        .usage = lvk::TextureUsageBits_Attachment,
        .debugName = "Depth buffer",
    });

    glfwSetWindowUserPointer(window_, this);

    glfwSetMouseButtonCallback(window_,
                               [](GLFWwindow *window, int button, int action, int mods)
                               {
                                   VulkanApp *app = (VulkanApp *)glfwGetWindowUserPointer(window);
                                   if (button == GLFW_MOUSE_BUTTON_LEFT)
                                   {
                                       app->mouseState_.pressedLeft = action == GLFW_PRESS;
                                   }
                                   for (auto &cb : app->callbacksMouseButton)
                                   {
                                       cb(window, button, action, mods);
                                   }
                               });
    glfwSetCursorPosCallback(window_,
                             [](GLFWwindow *window, double x, double y)
                             {
                                 VulkanApp *app = (VulkanApp *)glfwGetWindowUserPointer(window);
                                 int width, height;
                                 glfwGetFramebufferSize(window, &width, &height);
                                 app->mouseState_.pos.x = static_cast<float>(x / width);
                                 app->mouseState_.pos.y = 1.0f - static_cast<float>(y / height);
                             });
    glfwSetKeyCallback(window_,
                       [](GLFWwindow *window, int key, int scancode, int action, int mods)
                       {
                           VulkanApp *app = (VulkanApp *)glfwGetWindowUserPointer(window);
                           const bool pressed = action != GLFW_RELEASE;
                           if (key == GLFW_KEY_ESCAPE && pressed)
                           {
                               glfwSetWindowShouldClose(window, GLFW_TRUE);
                           }
                           if (key == GLFW_KEY_W)
                           {
                               app->positioner_.movement_.forward_ = pressed;
                           }
                           if (key == GLFW_KEY_S)
                           {
                               app->positioner_.movement_.backward_ = pressed;
                           }
                           if (key == GLFW_KEY_A)
                           {
                               app->positioner_.movement_.left_ = pressed;
                           }
                           if (key == GLFW_KEY_D)
                           {
                               app->positioner_.movement_.right_ = pressed;
                           }
                           if (key == GLFW_KEY_1)
                           {
                               app->positioner_.movement_.up_ = pressed;
                           }
                           if (key == GLFW_KEY_2)
                           {
                               app->positioner_.movement_.down_ = pressed;
                           }

                           app->positioner_.movement_.fastSpeed_ = (mods & GLFW_MOD_SHIFT) != 0;

                           if (key == GLFW_KEY_SPACE)
                           {
                               app->positioner_.lookAt(app->cfg_.initialCameraPos, app->cfg_.initialCameraTarget,
                                                       vec3(0.0f, 1.0f, 0.0f));
                               app->positioner_.setSpeed(vec3(0));
                           }
                           for (auto &cb : app->callbacksKey)
                           {
                               cb(window, key, scancode, action, mods);
                           }
                       });
    // initialize ImGUi after GLFW callbacks have been installed
    imgui_ = std::make_unique<lvk::ImGuiRenderer>(*ctx_, window_, "data/OpenSans-Light.ttf", 30.0f);
    implotCtx_ = ImPlot::CreateContext();
}
#endif

VulkanApp::~VulkanApp()
{
    ImPlot::DestroyContext(implotCtx_);

    gridPipeline = nullptr;
    gridVert = nullptr;
    gridFrag = nullptr;
    imgui_ = nullptr;
    depthTexture_ = nullptr;
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR)
    {
        destroyOpenXR();
        return;
    }
#endif
    ctx_ = nullptr;

#if !defined(ANDROID)
    if (window_)
    {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
#endif
}

lvk::Format VulkanApp::getDepthFormat() const
{
    return ctx_->getFormat(depthTexture_);
}

lvk::Format VulkanApp::getColorFormat() const
{
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR)
    {
        return lvk::vkFormatToFormat(xrColorSwapchain_.vkFormat);
    }
#endif
    return ctx_->getSwapchainFormat();
}

lvk::Dimensions VulkanApp::getOutputDimensions() const
{
    if (width_ > 0 && height_ > 0)
    {
        return {
            .width = static_cast<uint32_t>(width_),
            .height = static_cast<uint32_t>(height_),
        };
    }
    return {};
}

lvk::TextureHandle VulkanApp::getCurrentOutputTexture()
{
    if (currentOutputTexture_)
    {
        return currentOutputTexture_;
    }
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR)
    {
        currentOutputTexture_ = xrColorSwapchain_.currentTexture();
        return currentOutputTexture_;
    }
#endif
    currentOutputTexture_ = ctx_->getCurrentSwapchainTexture();
    return currentOutputTexture_;
}

lvk::SubmitHandle VulkanApp::submitFrame(lvk::ICommandBuffer &buf)
{
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR)
    {
        const lvk::SubmitHandle handle = ctx_->submit(buf);
        ctx_->wait(handle);
        return handle;
    }
#endif
    return ctx_->submit(buf, getCurrentOutputTexture());
}

bool VulkanApp::isOpenXR() const
{
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    return cfg_.enableOpenXR;
#else
    return false;
#endif
}

void VulkanApp::run(DrawFrameFunc drawFrame)
{
    LVK_PROFILER_FUNCTION();

    double timeStamp = glfwGetTime();
    float deltaSeconds = 0.0f;

#if defined(ANDROID)
    int events = 0;
    android_poll_source *source = nullptr;
    while (!androidApp_->destroyRequested)
    {
        while (ALooper_pollOnce(0, nullptr, &events, (void **)&source) >= 0)
        {
            if (source)
            {
                source->process(androidApp_, source);
            }
            if (androidApp_->destroyRequested)
            {
                return;
            }
        }

        if (!ctx_ || width_ <= 0 || height_ <= 0)
        {
            continue;
        }

        createAppDepthTexture(this);

        fpsCounter_.tick(deltaSeconds);
        const double newTimeStamp = glfwGetTime();
        deltaSeconds = static_cast<float>(newTimeStamp - timeStamp);
        timeStamp = newTimeStamp;

        const float ratio = width_ / (float)height_;
        positioner_.update(deltaSeconds, mouseState_.pos, mouseState_.pressedLeft);
        drawFrame((uint32_t)width_, (uint32_t)height_, ratio, deltaSeconds);
        if (ImGui::GetIO().MouseDown[0] && ImGui::GetIO().WantCaptureMouse)
        {
            mouseState_.pressedLeft = false;
            positioner_.resetMousePosition(mouseState_.pos);
        }
    }
#else
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR)
    {
        while (!xrShouldQuit_)
        {
            pollXrEvents();
            if (xrShouldQuit_)
            {
                break;
            }
            if (!renderXrFrame(drawFrame))
            {
                break;
            }
        }
        return;
    }
#endif
    while (!glfwWindowShouldClose(window_))
    {
        currentOutputTexture_ = {};
        fpsCounter_.tick(deltaSeconds);
        const double newTimeStamp = glfwGetTime();
        deltaSeconds = static_cast<float>(newTimeStamp - timeStamp);
        timeStamp = newTimeStamp;

        glfwPollEvents();
        int width, height;
#if defined(__APPLE__)
        // a hacky workaround for retina displays
        glfwGetWindowSize(window_, &width, &height);
#else
        glfwGetFramebufferSize(window_, &width, &height);
#endif
        if (!width || !height)
        {
            continue;
        }
        const float ratio = width / (float)height;

        positioner_.update(deltaSeconds, mouseState_.pos,
                           ImGui::GetIO().WantCaptureMouse ? false : mouseState_.pressedLeft);

        drawFrame((uint32_t)width, (uint32_t)height, ratio, deltaSeconds);

        if (maxFPS_ > 0)
        {
            const double frameEnd = glfwGetTime();
            const double elapsed = frameEnd - newTimeStamp;
            const double targetFrame = 1.0 / maxFPS_;
            if (elapsed < targetFrame)
            {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(static_cast<int64_t>((targetFrame - elapsed) * 1e6)));
            }
        }
    }
#endif
}

#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR

namespace
{
#if defined(_WIN32)
// Minimal ABI-compatible subset of renderdoc_app.h. Loading the API dynamically
// keeps RenderDoc an optional runtime dependency.
using RenderDocFunction = void(__cdecl *)();
using RenderDocSetCaptureKeys = void(__cdecl *)(int *keys, int numKeys);
using RenderDocStartFrameCapture = void(__cdecl *)(void *device, void *window);
using RenderDocIsFrameCapturing = uint32_t(__cdecl *)();
using RenderDocEndFrameCapture = uint32_t(__cdecl *)(void *device, void *window);
using RenderDocGetApi = int(__cdecl *)(int version, void **api);

struct RenderDocApi100
{
    RenderDocFunction functionsBeforeSetCaptureKeys[6];
    RenderDocSetCaptureKeys setCaptureKeys;
    RenderDocFunction functionsBeforeStartFrameCapture[12];
    RenderDocStartFrameCapture startFrameCapture;
    RenderDocIsFrameCapturing isFrameCapturing;
    RenderDocEndFrameCapture endFrameCapture;
};

RenderDocApi100 *getRenderDocApi()
{
    static RenderDocApi100 *api = []() -> RenderDocApi100 *
    {
        const HMODULE module = GetModuleHandleA("renderdoc.dll");
        if (!module)
        {
            return nullptr;
        }

        const auto getApi =
            reinterpret_cast<RenderDocGetApi>(GetProcAddress(module, "RENDERDOC_GetAPI"));
        RenderDocApi100 *result = nullptr;
        constexpr int renderDocApiVersion100 = 10000;
        if (!getApi ||
            getApi(renderDocApiVersion100, reinterpret_cast<void **>(&result)) != 1 ||
            !result)
        {
            return nullptr;
        }

        // The application owns the F12 edge and supplies the otherwise missing
        // headless/OpenXR frame boundary below.
        result->setCaptureKeys(nullptr, 0);
        LLOGL("RenderDoc detected: press F12 to capture one complete OpenXR render frame.\n");
        return result;
    }();
    return api;
}

void *getRenderDocVulkanDevicePointer(VkInstance instance)
{
    // This is RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE from renderdoc_app.h.
    return instance ? *reinterpret_cast<void **>(instance) : nullptr;
}

bool beginRenderDocXrCapture(VkInstance instance)
{
    RenderDocApi100 *api = getRenderDocApi();
    if (!api)
    {
        return false;
    }

    static bool wasF12Down = false;
    const bool isF12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    const bool captureRequested = isF12Down && !wasF12Down;
    wasF12Down = isF12Down;

    // Never overlap a capture which may have been requested externally.
    if (!captureRequested || api->isFrameCapturing())
    {
        return false;
    }

    api->startFrameCapture(getRenderDocVulkanDevicePointer(instance), nullptr);
    return api->isFrameCapturing() != 0;
}

void endRenderDocXrCapture(VkInstance instance)
{
    RenderDocApi100 *api = getRenderDocApi();
    if (api && api->isFrameCapturing())
    {
        if (!api->endFrameCapture(getRenderDocVulkanDevicePointer(instance), nullptr))
        {
            LLOGW("RenderDoc failed to save the OpenXR frame capture.\n");
        }
    }
}
#else
bool beginRenderDocXrCapture(VkInstance)
{
    return false;
}

void endRenderDocXrCapture(VkInstance)
{
}
#endif

glm::quat xrQuatToGlm(const XrQuaternionf &q)
{
    return glm::normalize(glm::quat(q.w, q.x, q.y, q.z));
}

glm::vec3 xrVecToGlm(const XrVector3f &v)
{
    return glm::vec3(v.x, v.y, v.z);
}

glm::mat4 xrPoseToMatrix(const XrPosef &pose)
{
    return glm::translate(glm::mat4(1.0f), xrVecToGlm(pose.position)) * glm::mat4_cast(xrQuatToGlm(pose.orientation));
}

glm::vec3 horizontalDirection(glm::vec3 v, const glm::vec3 &fallback)
{
    v.y = 0.0f;
    const float len = glm::length(v);
    return len > 0.0001f ? v / len : fallback;
}

float initialXrYaw(const glm::vec3 &pos, const glm::vec3 &target)
{
    glm::vec3 forward = horizontalDirection(target - pos, glm::vec3(0.0f, 0.0f, -1.0f));
    return std::atan2(-forward.x, -forward.z);
}

bool xrStringToPathChecked(XrInstance instance, const char *pathText, XrPath *path)
{
    const XrResult result = xrStringToPath(instance, pathText, path);
    if (XR_FAILED(result))
    {
        LLOGW("OpenXR path not available: %s (%s)\n", pathText, lvk::xrResultToString(result));
        *path = XR_NULL_PATH;
        return false;
    }
    return true;
}
} // namespace

void VulkanApp::initOpenXR()
{
    uint32_t availableExtensionCount = 0;
    XR_ASSERT(xrEnumerateInstanceExtensionProperties(nullptr, 0, &availableExtensionCount, nullptr));
    std::vector<XrExtensionProperties> availableExtensions(
        availableExtensionCount, XrExtensionProperties{.type = XR_TYPE_EXTENSION_PROPERTIES});
    XR_ASSERT(xrEnumerateInstanceExtensionProperties(nullptr, availableExtensionCount,
                                                     &availableExtensionCount, availableExtensions.data()));

    const auto hasExtension = [&](const char *name)
    {
        return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                           [&](const XrExtensionProperties &extension)
                           {
                               return std::strcmp(extension.extensionName, name) == 0;
                           });
    };

    std::vector<const char *> extensions = {XR_KHR_VULKAN_ENABLE_EXTENSION_NAME};
    const bool visibilityMaskAvailable = hasExtension(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
    if (visibilityMaskAvailable)
    {
        extensions.push_back(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
    }
    else
    {
        LLOGW("OpenXR runtime does not support XR_KHR_visibility_mask; hidden-area rendering is disabled.\n");
    }

    const XrInstanceCreateInfo instanceCI = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .applicationInfo =
            {
                .applicationName = "Renderer",
                .applicationVersion = 1,
                .engineName = "LightweightVK",
                .engineVersion = 1,
                .apiVersion = XR_API_VERSION_1_1,
            },
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .enabledExtensionNames = extensions.data(),
    };

    const XrResult result = xrCreateInstance(&instanceCI, &xrInstance_);
    if (XR_FAILED(result))
    {
        LLOGW("Failed to create OpenXR instance (%s). Is an OpenXR runtime available?\n",
              lvk::xrResultToString(result));
        LVK_ASSERT(false);
        return;
    }

    if (visibilityMaskAvailable)
    {
        const XrResult procResult =
            xrGetInstanceProcAddr(xrInstance_, "xrGetVisibilityMaskKHR",
                                  reinterpret_cast<PFN_xrVoidFunction *>(&xrGetVisibilityMaskKHR_));
        if (XR_FAILED(procResult) || !xrGetVisibilityMaskKHR_)
        {
            LLOGW("OpenXR runtime did not provide xrGetVisibilityMaskKHR (%s); hidden-area rendering is disabled.\n",
                  lvk::xrResultToString(procResult));
            xrGetVisibilityMaskKHR_ = nullptr;
        }
    }

    XrInstanceProperties instanceProps = {.type = XR_TYPE_INSTANCE_PROPERTIES};
    XR_ASSERT(xrGetInstanceProperties(xrInstance_, &instanceProps));
    LLOGL("OpenXR Runtime: %s v%u.%u.%u\n", instanceProps.runtimeName,
          XR_VERSION_MAJOR(instanceProps.runtimeVersion), XR_VERSION_MINOR(instanceProps.runtimeVersion),
          XR_VERSION_PATCH(instanceProps.runtimeVersion));

    const XrSystemGetInfo systemGI = {
        .type = XR_TYPE_SYSTEM_GET_INFO,
        .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
    };
    const XrResult sysResult = xrGetSystem(xrInstance_, &systemGI, &xrSystemId_);
    if (sysResult == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
    {
        LLOGW("No OpenXR head-mounted display is currently available. Make sure the headset is connected, awake, "
              "and accepted in the runtime before starting this app.\n");
        std::exit(EXIT_FAILURE);
    }
    if (XR_FAILED(sysResult))
    {
        LLOGW("OpenXR error: xrGetSystem() returned %s (%d)\n", lvk::xrResultToString(sysResult),
              static_cast<int>(sysResult));
        LVK_ASSERT_MSG(false, "xrGetSystem() failed");
        std::exit(EXIT_FAILURE);
    }

    XrSystemProperties systemProps = {.type = XR_TYPE_SYSTEM_PROPERTIES};
    XR_ASSERT(xrGetSystemProperties(xrInstance_, xrSystemId_, &systemProps));
    LLOGL("OpenXR System: %s (vendorId=%u)\n", systemProps.systemName, systemProps.vendorId);

    if (!lvk::appendOpenXRVulkanExtensions(xrInstance_, xrSystemId_, cfg_.contextConfig, xrVulkanExts_))
    {
        LLOGW("Failed to append OpenXR Vulkan extensions to LVK context config\n");
        std::exit(EXIT_FAILURE);
    }

    ctx_ = lvk::createVulkanContextXR(xrInstance_, xrSystemId_, xrGetInstanceProcAddr, cfg_.contextConfig);
}

void VulkanApp::initXrSession()
{
    const lvk::VulkanContext *vkCtx = static_cast<lvk::VulkanContext *>(ctx_.get());

    const XrGraphicsBindingVulkanKHR graphicsBinding = {
        .type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR,
        .instance = vkCtx->getVkInstance(),
        .physicalDevice = vkCtx->getVkPhysicalDevice(),
        .device = vkCtx->getVkDevice(),
        .queueFamilyIndex = vkCtx->deviceQueues_.graphicsQueueFamilyIndex,
        .queueIndex = 0,
    };

    const XrSessionCreateInfo sessionCI = {
        .type = XR_TYPE_SESSION_CREATE_INFO,
        .next = &graphicsBinding,
        .systemId = xrSystemId_,
    };
    XR_ASSERT(xrCreateSession(xrInstance_, &sessionCI, &xrSession_));

    const XrReferenceSpaceCreateInfo spaceCI = {
        .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
        .poseInReferenceSpace =
            {
                .orientation = {.x = 0, .y = 0, .z = 0, .w = 1},
                .position = {.x = 0, .y = 0, .z = 0},
            },
    };
    XR_ASSERT(xrCreateReferenceSpace(xrSession_, &spaceCI, &xrAppSpace_));

    xrPlayerPosition_ = cfg_.initialCameraPos;
    xrPlayerYaw_ = initialXrYaw(cfg_.initialCameraPos, cfg_.initialCameraTarget);
    initXrActions();
}

void VulkanApp::initXrActions()
{
    const XrActionSetCreateInfo actionSetCI = {
        .type = XR_TYPE_ACTION_SET_CREATE_INFO,
        .actionSetName = "vr_locomotion",
        .localizedActionSetName = "VR Locomotion",
        .priority = 0,
    };
    XR_ASSERT(xrCreateActionSet(xrInstance_, &actionSetCI, &xrActionSet_));

    xrStringToPathChecked(xrInstance_, "/user/hand/left", &xrLeftHandPath_);
    xrStringToPathChecked(xrInstance_, "/user/hand/right", &xrRightHandPath_);

    const XrPath leftHandPaths[] = {xrLeftHandPath_};
    const XrActionCreateInfo moveActionCI = {
        .type = XR_TYPE_ACTION_CREATE_INFO,
        .actionName = "move",
        .actionType = XR_ACTION_TYPE_VECTOR2F_INPUT,
        .countSubactionPaths = 1,
        .subactionPaths = leftHandPaths,
        .localizedActionName = "Move",
    };
    XR_ASSERT(xrCreateAction(xrActionSet_, &moveActionCI, &xrMoveAction_));

    const XrPath rightHandPaths[] = {xrRightHandPath_};
    const XrActionCreateInfo turnActionCI = {
        .type = XR_TYPE_ACTION_CREATE_INFO,
        .actionName = "turn",
        .actionType = XR_ACTION_TYPE_VECTOR2F_INPUT,
        .countSubactionPaths = 1,
        .subactionPaths = rightHandPaths,
        .localizedActionName = "Turn",
    };
    XR_ASSERT(xrCreateAction(xrActionSet_, &turnActionCI, &xrTurnAction_));

    const XrActionCreateInfo uiAimActionCI = {
        .type = XR_TYPE_ACTION_CREATE_INFO,
        .actionName = "ui_aim",
        .actionType = XR_ACTION_TYPE_POSE_INPUT,
        .countSubactionPaths = 1,
        .subactionPaths = rightHandPaths,
        .localizedActionName = "UI Aim",
    };
    XR_ASSERT(xrCreateAction(xrActionSet_, &uiAimActionCI, &xrUiAimAction_));

    const XrActionCreateInfo uiClickActionCI = {
        .type = XR_TYPE_ACTION_CREATE_INFO,
        .actionName = "ui_click",
        .actionType = XR_ACTION_TYPE_FLOAT_INPUT,
        .countSubactionPaths = 1,
        .subactionPaths = rightHandPaths,
        .localizedActionName = "UI Click",
    };
    XR_ASSERT(xrCreateAction(xrActionSet_, &uiClickActionCI, &xrUiClickAction_));

    const XrActionSpaceCreateInfo uiAimSpaceCI = {
        .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
        .action = xrUiAimAction_,
        .subactionPath = xrRightHandPath_,
        .poseInActionSpace =
            {
                .orientation = {.x = 0, .y = 0, .z = 0, .w = 1},
                .position = {.x = 0, .y = 0, .z = 0},
            },
    };
    XR_ASSERT(xrCreateActionSpace(xrSession_, &uiAimSpaceCI, &xrUiAimSpace_));

    const auto suggestControllerBindings = [&](const char *profilePath, const char *leftStickPath,
                                                const char *rightStickPath, const char *rightAimPath,
                                                const char *rightClickPath)
    {
        XrPath profile = XR_NULL_PATH;
        if (!xrStringToPathChecked(xrInstance_, profilePath, &profile))
        {
            return;
        }

        std::vector<XrActionSuggestedBinding> bindings;
        const auto addBinding = [&](XrAction action, const char *pathText)
        {
            if (!pathText)
            {
                return;
            }
            XrPath path = XR_NULL_PATH;
            if (xrStringToPathChecked(xrInstance_, pathText, &path))
            {
                bindings.push_back({.action = action, .binding = path});
            }
        };
        addBinding(xrMoveAction_, leftStickPath);
        addBinding(xrTurnAction_, rightStickPath);
        addBinding(xrUiAimAction_, rightAimPath);
        addBinding(xrUiClickAction_, rightClickPath);

        const XrInteractionProfileSuggestedBinding suggestedBindings = {
            .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
            .interactionProfile = profile,
            .countSuggestedBindings = static_cast<uint32_t>(bindings.size()),
            .suggestedBindings = bindings.data(),
        };
        const XrResult result = xrSuggestInteractionProfileBindings(xrInstance_, &suggestedBindings);
        if (XR_FAILED(result))
        {
            LLOGW("OpenXR could not suggest bindings for %s (%s)\n", profilePath, lvk::xrResultToString(result));
        }
    };

    suggestControllerBindings("/interaction_profiles/oculus/touch_controller",
                              "/user/hand/left/input/thumbstick", "/user/hand/right/input/thumbstick",
                              "/user/hand/right/input/aim/pose", "/user/hand/right/input/trigger/value");
    suggestControllerBindings("/interaction_profiles/valve/index_controller",
                              "/user/hand/left/input/thumbstick", "/user/hand/right/input/thumbstick",
                              "/user/hand/right/input/aim/pose", "/user/hand/right/input/trigger/value");
    suggestControllerBindings("/interaction_profiles/microsoft/motion_controller",
                              "/user/hand/left/input/thumbstick", "/user/hand/right/input/thumbstick",
                              "/user/hand/right/input/aim/pose", "/user/hand/right/input/trigger/value");
    suggestControllerBindings("/interaction_profiles/htc/vive_cosmos_controller",
                              "/user/hand/left/input/thumbstick", "/user/hand/right/input/thumbstick",
                              "/user/hand/right/input/aim/pose", "/user/hand/right/input/trigger/value");
    suggestControllerBindings("/interaction_profiles/htc/vive_controller",
                              "/user/hand/left/input/trackpad", "/user/hand/right/input/trackpad",
                              "/user/hand/right/input/aim/pose", "/user/hand/right/input/trigger/value");
    const XrActionSet actionSets[] = {xrActionSet_};
    const XrSessionActionSetsAttachInfo attachInfo = {
        .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .countActionSets = 1,
        .actionSets = actionSets,
    };
    XR_ASSERT(xrAttachSessionActionSets(xrSession_, &attachInfo));
}

void VulkanApp::initXrSwapchain()
{
    uint32_t numViews = 0;
    XR_ASSERT(
        xrEnumerateViewConfigurationViews(xrInstance_, xrSystemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                          &numViews, nullptr));
    xrConfigViews_.resize(numViews, {.type = XR_TYPE_VIEW_CONFIGURATION_VIEW});
    XR_ASSERT(xrEnumerateViewConfigurationViews(xrInstance_, xrSystemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                numViews, &numViews, xrConfigViews_.data()));
    LVK_ASSERT(numViews == 2);

    LLOGL("OpenXR view config: %ux%u (recommended), %ux%u (max)\n",
          xrConfigViews_[0].recommendedImageRectWidth, xrConfigViews_[0].recommendedImageRectHeight,
          xrConfigViews_[0].maxImageRectWidth, xrConfigViews_[0].maxImageRectHeight);

    LVK_VERIFY(xrColorSwapchain_.create(ctx_.get(), xrSession_, xrConfigViews_.data(), "XR color array swapchain"));
}

void VulkanApp::destroyOpenXR()
{
    if (ctx_)
    {
        ctx_->wait({});
        xrColorSwapchain_.destroy(ctx_.get());
    }
    if (xrUiAimSpace_)
    {
        xrDestroySpace(xrUiAimSpace_);
        xrUiAimSpace_ = XR_NULL_HANDLE;
    }
    if (xrAppSpace_)
    {
        xrDestroySpace(xrAppSpace_);
        xrAppSpace_ = XR_NULL_HANDLE;
    }
    if (xrMoveAction_)
    {
        xrDestroyAction(xrMoveAction_);
        xrMoveAction_ = XR_NULL_HANDLE;
    }
    if (xrTurnAction_)
    {
        xrDestroyAction(xrTurnAction_);
        xrTurnAction_ = XR_NULL_HANDLE;
    }
    if (xrUiAimAction_)
    {
        xrDestroyAction(xrUiAimAction_);
        xrUiAimAction_ = XR_NULL_HANDLE;
    }
    if (xrUiClickAction_)
    {
        xrDestroyAction(xrUiClickAction_);
        xrUiClickAction_ = XR_NULL_HANDLE;
    }
    if (xrActionSet_)
    {
        xrDestroyActionSet(xrActionSet_);
        xrActionSet_ = XR_NULL_HANDLE;
    }
    if (xrSession_)
    {
        xrDestroySession(xrSession_);
        xrSession_ = XR_NULL_HANDLE;
    }
    ctx_ = nullptr;
    if (xrInstance_)
    {
        xrDestroyInstance(xrInstance_);
        xrInstance_ = XR_NULL_HANDLE;
    }
#if !defined(ANDROID)
    glfwTerminate();
#endif
}

void VulkanApp::pollXrEvents()
{
    XrEventDataBuffer event = {.type = XR_TYPE_EVENT_DATA_BUFFER};
    XrResult pollResult = XR_SUCCESS;
    while ((pollResult = xrPollEvent(xrInstance_, &event)) == XR_SUCCESS)
    {
        switch (event.type)
        {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
        {
            const XrEventDataSessionStateChanged *stateEvent =
                reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
            xrSessionState_ = stateEvent->state;
            LLOGL("OpenXR session state: %s\n", lvk::xrSessionStateToString(xrSessionState_));
            switch (xrSessionState_)
            {
            case XR_SESSION_STATE_READY:
            {
                const XrSessionBeginInfo beginInfo = {
                    .type = XR_TYPE_SESSION_BEGIN_INFO,
                    .primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                };
                XR_ASSERT(xrBeginSession(xrSession_, &beginInfo));
                xrSessionRunning_ = true;
                break;
            }
            case XR_SESSION_STATE_STOPPING:
                XR_ASSERT(xrEndSession(xrSession_));
                xrSessionRunning_ = false;
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                xrShouldQuit_ = true;
                break;
            default:
                break;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            xrShouldQuit_ = true;
            break;
        default:
            break;
        }
        event = {.type = XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (pollResult != XR_EVENT_UNAVAILABLE)
    {
        LLOGW("OpenXR error: xrPollEvent() returned %s (%d)\n", lvk::xrResultToString(pollResult),
              static_cast<int>(pollResult));
    }
}

void VulkanApp::syncXrActions()
{
    xrMoveInput_ = vec2(0.0f);
    xrTurnInput_ = vec2(0.0f);
    xrUiClickInput_ = false;

    if (!xrActionSet_)
    {
        return;
    }

    const XrActiveActionSet activeActionSet = {
        .actionSet = xrActionSet_,
        .subactionPath = XR_NULL_PATH,
    };
    const XrActionsSyncInfo syncInfo = {
        .type = XR_TYPE_ACTIONS_SYNC_INFO,
        .countActiveActionSets = 1,
        .activeActionSets = &activeActionSet,
    };
    XR_ASSERT(xrSyncActions(xrSession_, &syncInfo));

    auto getVector2 = [&](XrAction action, XrPath subactionPath)
    {
        XrActionStateVector2f state = {.type = XR_TYPE_ACTION_STATE_VECTOR2F};
        const XrActionStateGetInfo getInfo = {
            .type = XR_TYPE_ACTION_STATE_GET_INFO,
            .action = action,
            .subactionPath = subactionPath,
        };
        XR_ASSERT(xrGetActionStateVector2f(xrSession_, &getInfo, &state));
        return state.isActive ? vec2(state.currentState.x, state.currentState.y) : vec2(0.0f);
    };

    xrMoveInput_ = getVector2(xrMoveAction_, xrLeftHandPath_);
    xrTurnInput_ = getVector2(xrTurnAction_, xrRightHandPath_);

    XrActionStateFloat clickState = {.type = XR_TYPE_ACTION_STATE_FLOAT};
    const XrActionStateGetInfo clickInfo = {
        .type = XR_TYPE_ACTION_STATE_GET_INFO,
        .action = xrUiClickAction_,
        .subactionPath = xrRightHandPath_,
    };
    XR_ASSERT(xrGetActionStateFloat(xrSession_, &clickInfo, &clickState));
    xrUiClickInput_ = clickState.isActive && clickState.currentState >= 0.5f;
}

void VulkanApp::updateXrUiInput(XrTime displayTime, float deltaSeconds)
{
    if (!ImGui::GetCurrentContext())
    {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    io.MouseDrawCursor = true;
    xrUiPointerValid_ = false;

    XrSpaceLocation location = {.type = XR_TYPE_SPACE_LOCATION};
    if (xrUiAimSpace_ && XR_SUCCEEDED(xrLocateSpace(xrUiAimSpace_, xrAppSpace_, displayTime, &location)) &&
        (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
        (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
    {
        const mat4 eyeFromLocal = glm::inverse(getXrLocalFromViewMatrix(0));
        const vec3 originEye = vec3(eyeFromLocal * vec4(xrVecToGlm(location.pose.position), 1.0f));
        const vec3 directionLocal = xrQuatToGlm(location.pose.orientation) * vec3(0.0f, 0.0f, -1.0f);
        const vec3 directionEye = glm::normalize(mat3(eyeFromLocal) * directionLocal);

        constexpr float kUiPlaneZ = -2.0f;
        if (directionEye.z < -0.001f)
        {
            const float distance = (kUiPlaneZ - originEye.z) / directionEye.z;
            if (distance > 0.0f)
            {
                const vec3 hitEye = originEye + directionEye * distance;
                const vec4 clip = getXrProjectionMatrix(0, 0.01f, 100.0f) * vec4(hitEye, 1.0f);
                if (clip.w > 0.0f)
                {
                    const vec2 ndc = vec2(clip) / clip.w;
                    if (ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f && ndc.y <= 1.0f)
                    {
                        const float mouseX = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width_);
                        const float mouseY = (0.5f - ndc.y * 0.5f) * static_cast<float>(height_);
                        io.AddMousePosEvent(mouseX, mouseY);
                        xrUiPointerValid_ = true;
                    }
                }
            }
        }
    }

    if (!xrUiPointerValid_)
    {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    io.AddMouseButtonEvent(0, xrUiPointerValid_ && xrUiClickInput_);

    constexpr float kScrollDeadZone = 0.25f;
    if (xrUiPointerValid_ && std::abs(xrTurnInput_.y) > kScrollDeadZone)
    {
        const float frameTime = std::min(deltaSeconds, 0.1f);
        io.AddMouseWheelEvent(0.0f, xrTurnInput_.y * 6.0f * frameTime);
    }
}

void VulkanApp::updateXrLocomotion(float deltaSeconds)
{
    constexpr float kDeadZone = 0.18f;
    constexpr float kMoveSpeed = 2.0f;
    constexpr float kTurnActivate = 0.75f;
    constexpr float kTurnRelease = 0.25f;
    constexpr float kSnapTurnRadians = glm::radians(30.0f);

    const glm::quat yawRotation = glm::angleAxis(xrPlayerYaw_, vec3(0.0f, 1.0f, 0.0f));
    const glm::quat headRotation = xrQuatToGlm(xrViews_[0].pose.orientation);
    const glm::quat worldHeadRotation = yawRotation * headRotation;

    const vec3 forward = horizontalDirection(worldHeadRotation * vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 0.0f, -1.0f));
    const vec3 right = horizontalDirection(worldHeadRotation * vec3(1.0f, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f));

    vec2 move = xrMoveInput_;
    if (glm::length(move) < kDeadZone)
    {
        move = vec2(0.0f);
    }
    else
    {
        const float len = std::min(glm::length(move), 1.0f);
        move = glm::normalize(move) * len;
    }
    xrPlayerPosition_ += (right * move.x + forward * move.y) * kMoveSpeed * deltaSeconds;

    const bool uiCapturesPointer =
        xrUiPointerValid_ && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
    const float turn = uiCapturesPointer ? 0.0f : xrTurnInput_.x;
    if (std::abs(turn) < kTurnRelease)
    {
        xrSnapTurnReady_ = true;
    }
    else if (xrSnapTurnReady_ && std::abs(turn) > kTurnActivate)
    {
        xrPlayerYaw_ += turn > 0.0f ? -kSnapTurnRadians : kSnapTurnRadians;
        xrSnapTurnReady_ = false;
    }
}

mat4 VulkanApp::getXrWorldFromLocalMatrix() const
{
    return glm::translate(mat4(1.0f), xrPlayerPosition_) *
           glm::rotate(mat4(1.0f), xrPlayerYaw_, vec3(0.0f, 1.0f, 0.0f));
}

mat4 VulkanApp::getXrLocalFromViewMatrix(uint32_t eye) const
{
    return xrPoseToMatrix(xrViews_[std::min<uint32_t>(eye, 1)].pose);
}

mat4 VulkanApp::getXrProjectionMatrix(uint32_t eye, float zNear, float zFar) const
{
    const XrFovf &fov = xrViews_[std::min<uint32_t>(eye, 1)].fov;
    const float tanLeft = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float tanDown = std::tan(fov.angleDown);
    const float tanUp = std::tan(fov.angleUp);
    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;

    mat4 result(0.0f);
    result[0][0] = 2.0f / tanWidth;
    result[1][1] = 2.0f / tanHeight;
    result[2][0] = (tanRight + tanLeft) / tanWidth;
    result[2][1] = (tanUp + tanDown) / tanHeight;
    result[2][2] = -zFar / (zFar - zNear);
    result[2][3] = -1.0f;
    result[3][2] = -(zFar * zNear) / (zFar - zNear);
    return result;
}

vec3 VulkanApp::getXrEyeWorldPosition(uint32_t eye) const
{
    const mat4 worldFromEye = getXrWorldFromLocalMatrix() * getXrLocalFromViewMatrix(eye);
    return vec3(worldFromEye[3]);
}

void VulkanApp::getXrVisibilityMask(uint32_t eye, std::vector<XrVector2f> &vertices,
                                    std::vector<uint32_t> &indices) const
{
    vertices.clear();
    indices.clear();
    if (!xrGetVisibilityMaskKHR_)
    {
        return;
    }

    XrVisibilityMaskKHR mask = {.type = XR_TYPE_VISIBILITY_MASK_KHR};

    // First call: query the vertex and index counts.
    XrResult result =
        xrGetVisibilityMaskKHR_(xrSession_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye,
                                XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &mask);
    if (XR_FAILED(result))
    {
        LLOGW("xrGetVisibilityMaskKHR(counts) failed for view %u: %s\n", eye,
              lvk::xrResultToString(result));
        return;
    }

    vertices.resize(mask.vertexCountOutput);
    indices.resize(mask.indexCountOutput);
    if (vertices.empty() || indices.empty())
    {
        vertices.clear();
        indices.clear();
        return;
    }

    // Second call: provide buffers with the capacities returned by the first call.
    // Retry if the runtime changes the mask between the two calls.
    for (uint32_t attempt = 0; attempt != 3; ++attempt)
    {
        mask.vertexCapacityInput = static_cast<uint32_t>(vertices.size());
        mask.vertices = vertices.data();
        mask.indexCapacityInput = static_cast<uint32_t>(indices.size());
        mask.indices = indices.data();
        result = xrGetVisibilityMaskKHR_(xrSession_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye,
                                         XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &mask);
        if (result == XR_ERROR_SIZE_INSUFFICIENT)
        {
            vertices.resize(mask.vertexCountOutput);
            indices.resize(mask.indexCountOutput);
            continue;
        }
        if (XR_FAILED(result))
        {
            LLOGW("xrGetVisibilityMaskKHR(data) failed for view %u: %s\n", eye,
                  lvk::xrResultToString(result));
            vertices.clear();
            indices.clear();
            return;
        }

        vertices.resize(mask.vertexCountOutput);
        indices.resize(mask.indexCountOutput);
        return;
    }

    LLOGW("OpenXR visibility mask kept changing while querying view %u.\n", eye);
    vertices.clear();
    indices.clear();
}

bool VulkanApp::renderXrFrame(DrawFrameFunc &drawFrame)
{
    if (!xrSessionRunning_)
    {
        static double lastWaitLogTime = 0.0;
        const double now = glfwGetTime();
        if (now - lastWaitLogTime > 1.0)
        {
            LLOGL("Waiting for OpenXR session to become READY; current state: %s\n",
                  lvk::xrSessionStateToString(xrSessionState_));
            lastWaitLogTime = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return true;
    }

    const XrFrameWaitInfo frameWaitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState = {.type = XR_TYPE_FRAME_STATE};
    XR_ASSERT(xrWaitFrame(xrSession_, &frameWaitInfo, &frameState));

    const double now = glfwGetTime();
    const float deltaSeconds = static_cast<float>(now - xrLastTimeStamp_);
    xrLastTimeStamp_ = now;
    fpsCounter_.tick(deltaSeconds);

    const XrFrameBeginInfo frameBeginInfo = {.type = XR_TYPE_FRAME_BEGIN_INFO};
    XR_ASSERT(xrBeginFrame(xrSession_, &frameBeginInfo));

    if (!frameState.shouldRender)
    {
        ctx_->wait({});
        const XrFrameEndInfo frameEndInfo = {
            .type = XR_TYPE_FRAME_END_INFO,
            .displayTime = frameState.predictedDisplayTime,
            .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        };
        XR_ASSERT(xrEndFrame(xrSession_, &frameEndInfo));
        return true;
    }

    const XrViewLocateInfo viewLocateInfo = {
        .type = XR_TYPE_VIEW_LOCATE_INFO,
        .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        .displayTime = frameState.predictedDisplayTime,
        .space = xrAppSpace_,
    };

    XrViewState viewState = {.type = XR_TYPE_VIEW_STATE};
    uint32_t numViews = 2;
    xrViews_[0] = {.type = XR_TYPE_VIEW};
    xrViews_[1] = {.type = XR_TYPE_VIEW};
    XR_ASSERT(xrLocateViews(xrSession_, &viewLocateInfo, &viewState, 2, &numViews, xrViews_));
    LVK_ASSERT(numViews == 2);
    xrViewsValid_ = true;
    syncXrActions();
    updateXrUiInput(frameState.predictedDisplayTime, deltaSeconds);
    updateXrLocomotion(deltaSeconds);

    if (!xrColorSwapchain_.acquire())
    {
        return false;
    }

    currentOutputTexture_ = xrColorSwapchain_.currentTexture();
    width_ = static_cast<int>(xrColorSwapchain_.width);
    height_ = static_cast<int>(xrColorSwapchain_.height);

    const VkInstance vkInstance =
        static_cast<lvk::VulkanContext *>(ctx_.get())->getVkInstance();
    const bool captureRenderDocFrame = beginRenderDocXrCapture(vkInstance);
    drawFrame(xrColorSwapchain_.width, xrColorSwapchain_.height,
              xrColorSwapchain_.width / static_cast<float>(xrColorSwapchain_.height), deltaSeconds);
    ctx_->wait({});
    if (captureRenderDocFrame)
    {
        endRenderDocXrCapture(vkInstance);
    }

    xrColorSwapchain_.release();
    currentOutputTexture_ = {};

    XrCompositionLayerProjectionView projectionViews[2] = {
        {.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
        {.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
    };
    xrColorSwapchain_.fillProjectionViews(xrViews_, projectionViews);

    const XrCompositionLayerProjection projectionLayer = {
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
        .space = xrAppSpace_,
        .viewCount = 2,
        .views = projectionViews,
    };
    const XrCompositionLayerBaseHeader *layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projectionLayer)};

    const XrFrameEndInfo frameEndInfo = {
        .type = XR_TYPE_FRAME_END_INFO,
        .displayTime = frameState.predictedDisplayTime,
        .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        .layerCount = 1,
        .layers = layers,
    };
    XR_ASSERT(xrEndFrame(xrSession_, &frameEndInfo));

    return true;
}

#endif

mat4 VulkanApp::getEyeViewMatrix(uint32_t eye) const
{
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR && xrViewsValid_)
    {
        const mat4 worldFromEye = getXrWorldFromLocalMatrix() * getXrLocalFromViewMatrix(eye);
        return glm::inverse(worldFromEye);
    }
#else
    (void)eye;
#endif
    assert(false);
    return {};
}

mat4 VulkanApp::getEyeProjectionMatrix(uint32_t eye, float zNear, float zFar) const
{
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR && xrViewsValid_)
    {
        return getXrProjectionMatrix(eye, zNear, zFar);
    }
#else
    (void)eye;
    (void)zNear;
    (void)zFar;
#endif
    assert(false);
    return {};
}

mat4 VulkanApp::getEyeViewProjectionMatrix(uint32_t eye,
                                           float zNear, float zFar) const
{
    return getEyeProjectionMatrix(eye, zNear, zFar) * getEyeViewMatrix(eye);
}

vec3 VulkanApp::getEyePosition(uint32_t eye) const
{
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    if (cfg_.enableOpenXR && xrViewsValid_)
    {
        return getXrEyeWorldPosition(eye);
    }
#else
    (void)eye;
#endif
    assert(false);
    return {};
}

void VulkanApp::drawMemo()
{
    // ImGui::SetNextWindowPos(ImVec2(10, 10));
    // ImGui::Begin(
    //     "Keyboard hints:", nullptr,
    //     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
    //     ImGuiWindowFlags_NoCollapse);
    // ImGui::Text("W/S/A/D - camera movement");
    // ImGui::Text("1/2 - camera up/down");
    // ImGui::Text("Shift - fast movement");
    // ImGui::Text("Space - reset view");
    // ImGui::End();
}

void VulkanApp::drawGTFInspector_Animations(GLTFIntrospective &intro)
{
    if (!intro.showAnimations)
    {
        return;
    }

    if (ImGui::Begin("Animations", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse))
    {
        for (uint32_t a = 0; a < intro.animations.size(); ++a)
        {
            auto it = std::find(intro.activeAnim.begin(), intro.activeAnim.end(), a);
            bool oState = it != intro.activeAnim.end(); // && selected < anim->size();
            bool state = oState;
            ImGui::Checkbox(intro.animations[a].c_str(), &state);

            if (state)
            {
                if (!oState)
                {
                    uint32_t freeSlot = intro.activeAnim.size() - 1;
                    if (auto nf = std::find(intro.activeAnim.begin(), intro.activeAnim.end(), ~0u);
                        nf != intro.activeAnim.end())
                    {
                        freeSlot = std::distance(intro.activeAnim.begin(), nf);
                    }
                    intro.activeAnim[freeSlot] = a;
                }
            }
            else
            {
                if (it != intro.activeAnim.end())
                {
                    *it = ~0;
                }
            }
        }
    }

    if (intro.showAnimationBlend)
    {
        ImGui::SliderFloat("Blend", &intro.blend, 0, 1.0f);
    }

    ImGui::End();
}

void VulkanApp::drawGTFInspector_Materials(GLTFIntrospective &intro)
{
    LVK_PROFILER_FUNCTION();

    if (!intro.showMaterials || intro.materials.empty())
    {
        return;
    }

    if (ImGui::Begin("Materials", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse))
    {
        for (uint32_t m = 0; m < intro.materials.size(); ++m)
        {
            GLTFMaterialIntro &mat = intro.materials[m];
            const uint32_t &currentMask = intro.materials[m].currentMaterialMask;

            auto setMaterialMask = [&m = intro.materials[m]](uint32_t flag, bool active)
            {
                m.modified = true;
                if (active)
                {
                    m.currentMaterialMask |= flag;
                }
                else
                {
                    m.currentMaterialMask &= ~flag;
                }
            };

            const bool isUnlit = (currentMask & MaterialType_Unlit) == MaterialType_Unlit;

            bool state = false;

            ImGui::Text("%s", mat.name.c_str());
            ImGui::PushID(m);
            state = isUnlit;

            if (ImGui::RadioButton("Unlit", state))
            {
                mat.currentMaterialMask = 0;
                setMaterialMask(MaterialType_Unlit, true);
            }

            state = (currentMask & MaterialType_MetallicRoughness) == MaterialType_MetallicRoughness;
            if ((mat.materialMask & MaterialType_MetallicRoughness) == MaterialType_MetallicRoughness)
            {
                if (ImGui::RadioButton("MetallicRoughness", state))
                {
                    setMaterialMask(MaterialType_Unlit, false);
                    setMaterialMask(MaterialType_SpecularGlossiness, false);
                    setMaterialMask(MaterialType_MetallicRoughness, true);
                }
            }

            state = (currentMask & MaterialType_SpecularGlossiness) == MaterialType_SpecularGlossiness;
            if ((mat.materialMask & MaterialType_SpecularGlossiness) == MaterialType_SpecularGlossiness)
            {
                if (ImGui::RadioButton("SpecularGlossiness", state))
                {
                    setMaterialMask(MaterialType_Unlit, false);
                    setMaterialMask(MaterialType_SpecularGlossiness, true);
                    setMaterialMask(MaterialType_MetallicRoughness, false);
                }
            }

            state = (currentMask & MaterialType_Sheen) == MaterialType_Sheen;
            if ((mat.materialMask & MaterialType_Sheen) == MaterialType_Sheen)
            {
                ImGui::BeginDisabled(isUnlit);
                if (ImGui::Checkbox("Sheen", &state))
                {
                    setMaterialMask(MaterialType_Sheen, state);
                }
                ImGui::EndDisabled();
            }

            state = (mat.currentMaterialMask & MaterialType_ClearCoat) == MaterialType_ClearCoat;
            if ((mat.materialMask & MaterialType_ClearCoat) == MaterialType_ClearCoat)
            {
                ImGui::BeginDisabled(isUnlit);
                if (ImGui::Checkbox("ClearCoat", &state))
                {
                    setMaterialMask(MaterialType_ClearCoat, state);
                }
                ImGui::EndDisabled();
            }

            state = (mat.currentMaterialMask & MaterialType_Specular) == MaterialType_Specular;
            if ((mat.materialMask & MaterialType_Specular) == MaterialType_Specular)
            {
                ImGui::BeginDisabled(isUnlit);
                if (ImGui::Checkbox("Specular", &state))
                {
                    setMaterialMask(MaterialType_Specular, state);
                }
                ImGui::EndDisabled();
            }

            state = (mat.currentMaterialMask & MaterialType_Transmission) == MaterialType_Transmission;
            if ((mat.materialMask & MaterialType_Transmission) == MaterialType_Transmission)
            {
                ImGui::BeginDisabled(isUnlit);
                if (ImGui::Checkbox("Transmission", &state))
                {
                    if (!state)
                    {
                        setMaterialMask(MaterialType_Volume, false);
                    }
                    setMaterialMask(MaterialType_Transmission, state);
                }
                ImGui::EndDisabled();
            }

            state = (mat.currentMaterialMask & MaterialType_Volume) == MaterialType_Volume;
            if ((mat.materialMask & MaterialType_Volume) == MaterialType_Volume)
            {
                ImGui::BeginDisabled(isUnlit);
                if (ImGui::Checkbox("Volume", &state))
                {
                    setMaterialMask(MaterialType_Volume, state);
                    if (state)
                    {
                        setMaterialMask(MaterialType_Transmission, true);
                    }
                }
                ImGui::EndDisabled();
            }

            ImGui::PopID();
        }
    }

    ImGui::End();
}

void VulkanApp::drawGTFInspector_Cameras(GLTFIntrospective &intro)
{
    if (!intro.showCameras)
    {
        return;
    }

    ImGui::Begin("Cameras:", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse);
    std::string current_item = intro.activeCamera != ~0u ? intro.cameras[intro.activeCamera] : "";
    if (ImGui::BeginCombo("##combo", current_item.c_str()))
    {
        for (uint32_t n = 0; n < intro.cameras.size(); n++)
        {
            bool is_selected = (current_item == intro.cameras[n]);
            if (ImGui::Selectable(intro.cameras[n].c_str(), is_selected))
            {
                intro.activeCamera = n;
                current_item = intro.cameras[n];
            }
            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::End();
}

void VulkanApp::drawGTFInspector(GLTFIntrospective &intro)
{
    if (!cfg_.showGLTFInspector)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(10, 300));

    drawGTFInspector_Animations(intro);
    drawGTFInspector_Materials(intro);
    drawGTFInspector_Cameras(intro);
}

void VulkanApp::drawFPS()
{
    if (const ImGuiViewport *v = ImGui::GetMainViewport())
    {
        ImGui::SetNextWindowPos({v->WorkPos.x + v->WorkSize.x - 15.0f, v->WorkPos.y + 15.0f}, ImGuiCond_Always,
                                {1.0f, 0.0f});
    }
    ImGui::SetNextWindowBgAlpha(0.30f);
    ImGui::SetNextWindowSize(ImVec2(ImGui::CalcTextSize("FPS : _______").x, 0));
    if (ImGui::Begin("##FPS", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove))
    {
        ImGui::Text("FPS : %i", (int)fpsCounter_.getFPS());
        ImGui::Text("Ms  : %.1f", fpsCounter_.getFPS() > 0 ? 1000.0 / fpsCounter_.getFPS() : 0);
    }
    ImGui::End();
}

void VulkanApp::drawGrid(lvk::ICommandBuffer &buf, const mat4 &proj, const vec3 &origin, uint32_t numSamples,
                         lvk::Format colorFormat)
{
    drawGrid(buf, proj * camera_.getViewMatrix(), origin, camera_.getPosition(), numSamples, colorFormat);
}

void VulkanApp::drawGrid(lvk::ICommandBuffer &buf, const mat4 &mvp, const vec3 &origin, const vec3 &camPos,
                         uint32_t numSamples, lvk::Format colorFormat)
{
    LVK_PROFILER_FUNCTION();

    if (gridPipeline.empty() || pipelineSamples != numSamples)
    {
        gridVert = loadShaderModule(ctx_, "data/shaders/Grid.vert");
        gridFrag = loadShaderModule(ctx_, "data/shaders/Grid.frag");

        pipelineSamples = numSamples;

        gridPipeline = ctx_->createRenderPipeline({
            .smVert = gridVert,
            .smFrag = gridFrag,
            .color = {{
                .format = colorFormat != lvk::Format_Invalid ? colorFormat : getColorFormat(),
                .blendEnabled = true,
                .srcRGBBlendFactor = lvk::BlendFactor_SrcAlpha,
                .dstRGBBlendFactor = lvk::BlendFactor_OneMinusSrcAlpha,
            }},
            .depthFormat = this->getDepthFormat(),
            .samplesCount = numSamples,
            .debugName = "Pipeline: drawGrid()",
        });
    }

    const struct
    {
        mat4 mvp;
        vec4 camPos;
        vec4 origin;
    } pc = {
        .mvp = mvp,
        .camPos = vec4(camPos, 1.0f),
        .origin = vec4(origin, 1.0f),
    };

    buf.cmdPushDebugGroupLabel("Grid", 0xff0000ff);
    buf.cmdBindRenderPipeline(gridPipeline);
    buf.cmdBindDepthState({.compareOp = lvk::CompareOp_Less, .isDepthWriteEnabled = false});
    buf.cmdPushConstants(pc);
    buf.cmdDraw(6);
    buf.cmdPopDebugGroupLabel();
}
