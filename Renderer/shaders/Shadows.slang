#pragma once

float PCF(float3 uvw, uint textureid, uint samplerid)
{
    return textureBindless2DShadow(textureid, samplerid, uvw);
}

float PCF3x3(float3 uvw, uint textureid, uint samplerid)
{
    float size = 1.0 / textureBindlessSize2D(textureid).x;
    float shadow = 0.0;
    for (int v = -1; v <= +1; v++)
        for (int u = -1; u <= +1; u++)
            shadow += textureBindless2DShadow(textureid, samplerid, uvw + size * float3(u, v, 0));
    return shadow / 9;
}

float shadow(float4 s, uint textureid, uint samplerid)
{
    s = s / s.w;
    if (all(s.xyz >= float3(0.0)) && all(s.xyz <= float3(1.0)))
    {
        // PCF estimates light visibility: zero means fully occluded, one means unoccluded.
        return PCF(float3(s.x, 1.0 - s.y, s.z), textureid, samplerid);
    }
    return 1.0;
}
