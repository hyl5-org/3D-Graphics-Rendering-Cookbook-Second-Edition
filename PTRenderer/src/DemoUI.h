#pragma once

#include "RenderPasses.h"

namespace FinalDemo
{

void drawControls(uint32_t width, uint32_t height, float aspectRatio, SceneCulling &culling, 
                  HDRPass &hdr, VulkanApp &app);

} // namespace FinalDemo
