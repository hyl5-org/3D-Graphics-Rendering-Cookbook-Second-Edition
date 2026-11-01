#pragma once

#ifdef __cplusplus
#include <cstdint>
#endif

static const uint32_t RECEIVER_SIZE_LOG2 = 6;
static const uint32_t RECEIVER_TILE_SIZE = (1u << RECEIVER_SIZE_LOG2);
static const uint32_t RECEIVER_MIP_LEVELS = (RECEIVER_SIZE_LOG2 + 1);
static const uint32_t RECEIVER_EMPTY_CELL = (RECEIVER_TILE_SIZE * RECEIVER_TILE_SIZE);
static const uint32_t RECEIVER_GROUP_SIZE = 8;
static const uint32_t RECEIVER_REDUCE_SIZE = (RECEIVER_TILE_SIZE / 4);
static const uint32_t RECEIVER_REDUCE_THREADS = (RECEIVER_REDUCE_SIZE * RECEIVER_REDUCE_SIZE);
static const float CASTER_BOUNDS_EPSILON = 0.00001f;
static const float RECEIVER_DEPTH_EPSILON = 0.0001f;

static const uint32_t OCCLUSION_GROUP_SIZE = 64;
