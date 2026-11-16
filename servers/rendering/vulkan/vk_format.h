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
