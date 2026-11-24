#pragma once
#include <cstdint>
#include "math/math_defs.h"

struct alignas(16) FrameUniforms
{
    real_t view[16];                   // offset 0,   size 64
    real_t projection[16];             // offset 64,  size 64
    real_t camera_position[4];         // offset 128, xyz + 0
    real_t ambient_color_strength[4];  // offset 144, linear rgb + ambient_strength
    real_t light_color_intensity[4];   // offset 160, linear rgb + intensity
    real_t light_direction_type[4];    // offset 176, direction xyz + (float)type
    real_t light_space_matrix[16];     // offset 192
    real_t light_space_projection[16]; // offset 256
    int32_t flags[4];                  // offset 320, flags[0]=cast_shadow，其余 0
}; // size 336
static_assert(sizeof(FrameUniforms) == 336);

struct alignas(16) ShadowUniforms
{
    real_t light_space_matrix[16]; // offset 0
    real_t projection[16];         // offset 64
}; // size 128
static_assert(sizeof(ShadowUniforms) == 128);

struct alignas(16) ObjectUniforms
{
    real_t model[16];        // offset 0
    real_t albedo[4];        // offset 64, linear rgba
    real_t params0[4];       // offset 80, metallic, roughness, parallax_scale, normal_scale
    real_t params1[4];       // offset 96, uv_scale.x, uv_scale.y, ao_strength, parallax_flip
    int32_t texture_mask[4]; // offset 112, texture_mask + 0/0/0
}; // size 128
static_assert(sizeof(ObjectUniforms) == 128);

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

struct alignas(16) SkyboxFrameUniforms
{
    real_t view[16];       // offset 0
    real_t projection[16]; // offset 64
}; // size 128