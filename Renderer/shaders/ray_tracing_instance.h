#pragma once

#ifdef __cplusplus
#include <cstdint>
using RayTracingInstanceIndex = uint32_t;
#else
typealias RayTracingInstanceIndex = uint;
#endif

// Vulkan custom index is 24 bits: bit 23 = masked, bits 0..22 = mesh index.
static const RayTracingInstanceIndex kRayTracingMaskedBit = 1u << 23;
static const RayTracingInstanceIndex kRayTracingMeshIndexMask = kRayTracingMaskedBit - 1u;
