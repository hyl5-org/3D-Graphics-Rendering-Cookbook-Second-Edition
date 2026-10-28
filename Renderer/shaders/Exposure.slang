#pragma once

// Exposure compensation is a multiplier; metering is scene-referred and independent of it.
float exposureScale(float averageLuminance, float compensation)
{
    return max(compensation, 0.0) * 0.18 / max(averageLuminance, 0.0001);
}
