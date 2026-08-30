    vec2 selectionNoise = blueNoise(pc.frameIndex * 2u, pixel);
    vec2 directionNoise = blueNoise(pc.frameIndex * 2u + 1u, pixel);