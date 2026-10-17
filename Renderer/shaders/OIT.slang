#pragma once

struct TransparentFragment
{
    half4 color;
    float depth;
    uint next;
};

typealias TransparencyListsBuffer = TransparentFragment *;
