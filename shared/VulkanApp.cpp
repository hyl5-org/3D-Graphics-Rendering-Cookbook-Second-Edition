#include "VulkanApp.h"

#include "UtilsGLTF.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <thread>
#include <unordered_map>

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

    window_ = lvk::initWindow("Simple example", width, height);
    ctx_ = lvk::createVulkanContextWithSwapchain(window_, width, height,
                                                 {
                                                     .enableValidation = false,
                                                     .enableValidationBestPractices = false,
                                                 });
    // recommend to use vulkan configurator on windows
    depthTexture_ = ctx_->createTexture({
        .type = lvk::TextureType_2D,
        .format = lvk::Format_Z_F32,
        .dimensions = {(uint32_t)width, (uint32_t)height},
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
    ctx_ = nullptr;

#if !defined(ANDROID)
    glfwDestroyWindow(window_);
    glfwTerminate();
#endif
}

lvk::Format VulkanApp::getDepthFormat() const
{
    return ctx_->getFormat(depthTexture_);
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
    while (!glfwWindowShouldClose(window_))
    {
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
                .format = colorFormat != lvk::Format_Invalid ? colorFormat : ctx_->getSwapchainFormat(),
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
