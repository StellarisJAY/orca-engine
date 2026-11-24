#pragma once

#include <vulkan/vulkan.hpp>

#include "rendering_device_refactor.h"

// =============================================================================
// Vulkan 图像布局（layout）与屏障（barrier）的集中映射。
//
// 背景：Vulkan 的 image layout 是显式状态机，每个阶段（上传、mipmap、渲染附件、
// 采样）都必须用 barrier 把布局切到对应状态，并给出正确的 stage/access mask。
// 这套映射如果散落在 vk_resource.cpp / vk_command.cpp 里，很容易出现"某个文件
// 以为图像是 shader read、另一个文件按 attachment 用"的分歧（审计清单 D-19/G-09）。
//
// 约定：
//   - 纹理对象在 VkTextureHandleData 里记录 current_layout（整张图统一状态）；
//   - 创建后 current_layout = eUndefined；
//   - queue_write_texture / generate_mip_map 结束后整张图回到 resting_layout，
//     并在 current_layout 里记录下来；
//   - 后续 render pass（V3）必须依据 current_layout 决定附件 barrier 的 oldLayout，
//     不能假设初始布局。
//
// 本文件只做纯映射，不读写资源对象状态。
// =============================================================================

/// @brief 图像布局切换所需的 src/dst 同步范围（stage + access）。
struct VkImageLayoutTransition
{
    vk::PipelineStageFlags src_stage;
    vk::AccessFlags src_access;
    vk::PipelineStageFlags dst_stage;
    vk::AccessFlags dst_access;
};

/// @brief 该格式在 image view / barrier 里使用的 aspect mask。
vk::ImageAspectFlags vk_image_aspect(Format format);

/// @brief 纹理"静止"（非传输、非渲染附件阶段）所处的布局。
///
/// 优先满足"能被采样"（TEXTURE_BINDING），其次 COPY_SRC、RENDER_ATTACHMENT；
/// 深度格式映射到深度只读 / 深度附件布局，而不是颜色布局。
vk::ImageLayout vk_image_resting_layout(Format format, uint8_t usage);

/// @brief old_layout → new_layout 的 stage/access mask。
VkImageLayoutTransition vk_image_layout_transition(vk::ImageLayout old_layout, vk::ImageLayout new_layout);

/// @brief 第 mip_level 层的尺寸（不小于 1）。
vk::Extent2D vk_mip_extent(uint32_t width, uint32_t height, uint32_t mip_level);

/// @brief 向 command buffer 记录一次图像布局切换 barrier。
///
/// old_layout 与 new_layout 相同时不做任何记录（no-op），便于调用方省略分支。
void vk_cmd_image_layout_barrier(vk::CommandBuffer command_buffer, vk::Image image,
                                 vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                 vk::ImageAspectFlags aspect, uint32_t base_mip, uint32_t mip_count,
                                 uint32_t base_layer, uint32_t layer_count);
