#include "DemoConfig.h"

namespace FinalDemo
{

DemoSettings gSettings;

void ViewContext::update(const VulkanApp &app, float zNear, float zFar)
{
    for (uint32_t eye = 0; eye != kMultiViewLayerCount; eye++)
    {
        view[eye] = app.getEyeViewMatrix(eye);
        projection[eye] = app.getEyeProjectionMatrix(eye, zNear, zFar);
        viewProjection[eye] = projection[eye] * view[eye];
        inverseViewProjection[eye] = glm::inverse(viewProjection[eye]);
        cameraPosition[eye] = vec4(app.getEyePosition(eye), 1.0f);
    }
}

void installKeyboardShortcuts(VulkanApp &app)
{
#if !defined(ANDROID)
    app.addKeyCallback(
        [](GLFWwindow *window, int key, int scancode, int action, int mods)
        {
            const bool pressed = action != GLFW_RELEASE;
            if (!pressed || ImGui::GetIO().WantCaptureKeyboard)
            {
                return;
            }
            if (key == GLFW_KEY_P)
            {
                gSettings.culling.freezeView = !gSettings.culling.freezeView;
            }
            if (key == GLFW_KEY_N)
            {
                gSettings.culling.mode = CullingMode_None;
            }
            if (key == GLFW_KEY_C)
            {
                gSettings.culling.mode = CullingMode_CPU;
            }
            // if (key == GLFW_KEY_G)
            // {
            //     gSettings.culling.mode = CullingMode_GPU;
            // }
        });
#endif
}

} // namespace FinalDemo
