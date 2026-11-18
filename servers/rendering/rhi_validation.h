#pragma once

#include <cstdint>
#include <string>

#include "rendering_device_refactor.h"

// =============================================================================
// 后端无关的 RHI 描述校验。
//
// 这里只校验接口层可判断的约束（格式是否已定义、尺寸/mip/usage 是否合法、
// 视图范围是否越界、管线各阶段/顶点布局是否自洽），不涉及任何具体图形 API 能力。
// 后端特有的能力检查（GL 的格式映射、Vulkan 的 vk::Format 支持）由各后端在
// 自己的 create_* 入口里追加。
//
// OpenGL 后端的 gl_validate_* 与 Vulkan 后端的 create_* 共用这一层，避免两套
// 校验逻辑逐渐分叉（审计清单 D-21）。
// =============================================================================

/// @brief 该格式是否为深度格式（当前只有 Depth32Float）。
bool rhi_format_is_depth(Format format);

/// @brief 该格式是否带 sRGB 编码（用于颜色管线/交换链约定判断）。
bool rhi_format_is_srgb(Format format);

/// @brief 顶点属性格式的字节数；未知格式返回 0。
uint32_t rhi_vertex_format_size(VertexFormat format);

/// @brief 纹理格式每像素（texel）的字节数；Undefined/未知格式返回 0。
///
/// 用于上传时计算紧密打包的一行字节数（bytes_per_row == 0 的默认值），
/// GL/Vulkan 两个后端的 queue_write_texture 共用同一份换算。
uint32_t rhi_format_texel_size(Format format);

/// @brief 校验纹理描述。
bool rhi_validate_texture_desc(const TextureDescRefactor &desc, std::string &error);

/// @brief 纹理视图校验所需的父纹理信息（后端从自己的纹理对象填充）。
struct RhiTextureInfo
{
    Format format = Format::Undefined;
    TextureDimension dim = TextureDimension::Texture2D;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    /// @brief 数组层数：2D 为 1，Cube 为 6
    uint32_t layers = 1;
};

/// @brief 校验纹理视图描述与父纹理是否匹配（含 0 值到默认范围的解析）。
bool rhi_validate_texture_view_desc(const RhiTextureInfo &texture, const TextureViewDesc &desc,
                                    std::string &error);

/// @brief 校验采样器描述。
bool rhi_validate_sampler_desc(const SamplerDesc &desc, std::string &error);

/// @brief 校验渲染管线描述（只校验描述层面，不检查 shader 里声明的名字/位置）。
bool rhi_validate_render_pipeline_desc(const RenderPipelineDesc &desc, std::string &error);
