#pragma once

#include <vulkan/vulkan.hpp>

#include "rendering_device_refactor.h"

// =============================================================================
// Vulkan 格式 / 用途映射（纯逻辑，不产生 Vulkan 调用）。
//
// 拆成独立文件的目的：
//   1. 格式映射与描述校验一样可以脱离 device 做单元测试；
//   2. create_texture / create_texture_view / 交换链初始化共用同一份映射，
//      避免"交换链声明一种格式、管线又用另一种格式"的分叉（审计清单 D-17/D-18）。
// =============================================================================

/// @brief Format → vk::Format。Undefined / 未知格式返回 false。
bool vk_format_info(Format format, vk::Format &out);

/// @brief vk::Format → Format（主要用于把实际交换链格式翻译成接口格式）。
/// 不支持的格式返回 false。
bool vk_format_from_vulkan(vk::Format format, Format &out);

/// @brief TextureUsage 位掩码 → VkImageUsageFlags。
///
/// 深度格式的 RENDER_ATTACHMENT 必须映射成 eDepthStencilAttachment 而不是
/// eColorAttachment；未知用途位会被忽略，由 rhi_validate_texture_desc 提前拒绝。
vk::ImageUsageFlags vk_image_usage_from_flags(uint8_t usage, Format format);

/// @brief GPUBufferUsage 位掩码 → VkBufferUsageFlags。
///
/// 与 image usage 一样是纯映射：未知用途位会被忽略，由共享层
/// rhi_validate_buffer_desc 在 create_buffer 入口提前拒绝。
vk::BufferUsageFlags vk_buffer_usage_from_flags(uint8_t usage);

/// @brief CompareFunction → vk::CompareOp（采样器的深度比较函数，阴影 PCF 用）。
///
/// 默认返回 `eLessOrEqual`：即使 `compare_enable` 为 false，Vulkan 也要求
/// `compareOp` 是一个合法枚举值，用 WebGPU 的默认值最不容易引入行为差异。
vk::CompareOp vk_compare_op_from_function(CompareFunction function);

/// @brief 顶点属性格式 → vk::Format（管线 vertex input attribute 用）。
///
/// 未知格式返回 false；共享校验 `rhi_validate_render_pipeline_desc` 已在入口
/// 拒绝未知格式，这里的失败分支只是 Defensive 处理。
bool vk_vertex_format_info(VertexFormat format, vk::Format &out);

/// @brief BlendFactor → vk::BlendFactor（管线颜色混合状态用）。
vk::BlendFactor vk_blend_factor_from(BlendFactor factor);

/// @brief BlendOperation → vk::BlendOp（管线颜色混合状态用）。
vk::BlendOp vk_blend_op_from(BlendOperation operation);

/// @brief ColorWriteMask 位掩码 → vk::ColorComponentFlags。
///
/// 未知位会被忽略；已关闭的颜色通道不会被写入。
vk::ColorComponentFlags vk_color_write_mask_from(uint8_t mask);

/// @brief 采样数 → vk::SampleCountFlagBits（管线 multisample 与 render pass 附件用）。
///
/// 接口当前只支持 1（共享校验拒绝其它值）；保留完整映射，后续开 MSAA 时不必改表。
vk::SampleCountFlagBits vk_sample_count_from_bits(uint32_t samples);
