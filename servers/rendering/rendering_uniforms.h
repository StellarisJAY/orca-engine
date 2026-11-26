#pragma once
#include <cstdint>
#include "math/math_defs.h"

/// @brief 帧全局uniforms
/// @brief group=0, binding=0
struct alignas(16) FrameUniforms
{
    real_t view[16];                   // offset 0,   size 64
    real_t projection[16];             // offset 64,  size 64
    real_t camera_position[4];         // offset 128, xyz + 0
    real_t ambient_color_strength[4];  // offset 144, linear rgb + ambient_strength
}; // size 160
static_assert(sizeof(FrameUniforms) == 160);

/// @brief 物体uniforms
/// @brief group=0, binding=1
struct alignas(16) ObjectUniforms
{
    real_t model[16];        // offset 0, size 64
}; // size 64
static_assert(sizeof(ObjectUniforms) == 64);

/// @brief 物体材质uniforms
/// @brief group=1, binding=0
struct alignas(16) MaterialUniforms
{
    real_t albedo[4] = {0}; // offset 0
    real_t metalic = 0; // offset 16
    real_t roughness = 0; // offset 32
    real_t parallax_scale = 1.0; // offset 48
    real_t normal_scale = 1.0; // offset 64
    real_t uv_scale[2] = { 1.0 }; // offset 80
    real_t ao_strength = 1.0; // offset 112
    uint32_t parallax_flip = 1; // offset 128
    int32_t texture_mask[4] = {0}; // offset 144
};
static_assert(sizeof(MaterialUniforms) == 64);

/// @brief 灯光uniforms
struct alignas(16) LightUniforms
{
    real_t light_position[4];        // offset 0, size 16
    real_t light_color_intensity[4]; // offset 64, size 16, rgb+intensity
    uint8_t flags[4];                // offset 128, size 4, type+cast_shadow
};
static_assert(sizeof(LightUniforms) == 48);

struct alignas(16) ShadowUniforms
{
    real_t light_space_matrix[16]; // offset 0
    real_t projection[16];         // offset 64
}; // size 128
static_assert(sizeof(ShadowUniforms) == 128);


struct alignas(16) SkyboxFrameUniforms
{
    real_t view[16];       // offset 0
    real_t projection[16]; // offset 64
}; // size 128