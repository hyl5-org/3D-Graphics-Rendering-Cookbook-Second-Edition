#include "DemoConfig.h"

namespace FinalDemo
{

DemoSettings gSettings;

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
            if (key == GLFW_KEY_G)
            {
                gSettings.culling.mode = CullingMode_GPU;
            }
        });
#endif
}

} // namespace FinalDemo
