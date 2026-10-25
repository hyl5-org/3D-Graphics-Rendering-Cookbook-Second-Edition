#pragma once

float3 linearToSRGB(float3 value)
{
    float3 c = max(value, float3(0.0));
    float3 low = c * 12.92;
    float3 high = 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055;
    return float3(c.x <= 0.0031308 ? low.x : high.x,
                  c.y <= 0.0031308 ? low.y : high.y,
                  c.z <= 0.0031308 ? low.z : high.z);
}
