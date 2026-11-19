#pragma once

#include <cstdint>
#include <string>

#include "rendering_device_refactor.h"

// =============================================================================
// 后端无关的 RHI 描述校验。
//
// 这里只校验接口层可判断的约束（格式是否已定义、尺寸/mip/usage 是否合法、
// 视图范围是否越界、管线各阶段/顶点布局是否自洽），不涉及任何具体图形 API 能力。
// 后端特有的能力检查（Vulkan 的 vk::Format 支持）由 Vulkan 后端在
// 自己的 create_* 入口里追加。
//
// Vulkan 后端的 create_* 统一调用这一层，避免校验逻辑分散在各个创建函数
// （审计清单 D-21）。
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
/// Vulkan 后端的 queue_write_texture 使用这份换算。
uint32_t rhi_format_texel_size(Format format);

/// @brief 校验缓冲区描述。
///
/// 当前约束：
///   - size 必须大于 0；
///   - usage 不能为空，且只能包含 GPUBufferUsage 已定义的位（STORAGE 视为未知用途，
///     与纹理的 TEXTURE_USAGE 规则一致，本期直接拒绝）；
///   - VERTEX / INDEX / UNIFORM / STORAGE 中最多只能有一个（该规则最初来自 GL 的
///     单目标绑定约束，现作为接口层的保守约束保留，保证描述在其他后端也可用）。
bool rhi_validate_buffer_desc(const GPUBufferDesc &desc, std::string &error);

/// @brief 校验一次缓冲区写入的区间（不校验指针与 usage）。
bool rhi_validate_buffer_write(uint64_t buffer_size, uint64_t offset, uint64_t size, std::string &error);

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

/// @brief 已解析的纹理视图范围（把 desc 里的 0 解析成"到最后一层/面"）。
///
/// Vulkan 后端直接拿这份结果去填 subresourceRange / 逻辑视图范围，避免在
/// 创建视图时再算一遍 `mip_count == 0 ? mip_levels - base_mip : mip_count`。
struct RhiTextureViewRange
{
    TextureViewDimension dim = TextureViewDimension::Texture2D;
    TextureAspect aspect = TextureAspect::Color;
    uint32_t base_mip = 0;
    uint32_t mip_count = 1;
    uint32_t base_layer = 0;
    uint32_t layer_count = 1;
};

/// @brief 校验纹理视图描述并解析出最终范围（校验规则同 `rhi_validate_texture_view_desc`）。
///
/// Vulkan 的 cubemap 视图要求 `baseArrayLayer % 6 == 0` 且 `layerCount` 是 6 的倍数，
/// 这条约束在这里统一拒绝，避免把非法范围送进 `vkCreateImageView`。
bool rhi_resolve_texture_view_range(const RhiTextureInfo &texture, const TextureViewDesc &desc,
                                    RhiTextureViewRange &range, std::string &error);

/// @brief 校验采样器描述。
bool rhi_validate_sampler_desc(const SamplerDesc &desc, std::string &error);

/// @brief 校验 shader 模块描述（后端无关部分）。
///
/// 规则：
///   - `source` 不能为空，`entry_point` 不能为空；
///   - `ShaderSourceKind::SPIRV`：字节数必须是 4 的倍数、至少 20 字节头部、
///     magic / 版本合法、指令流不能越界，并且 `OpEntryPoint` 中必须存在
///     `entry_point`，其 ExecutionModel 与 `desc.type` 一致；
///   - `ShaderSourceKind::GLSL_TEXT`：只检查非空，能否编译由后端决定
///     （Vulkan 会拒绝并提示先编译成 SPIR-V）。
bool rhi_validate_shader_module_desc(const ShaderModuleDesc &desc, std::string &error);

/// @brief 校验渲染管线描述（只校验描述层面，不检查 shader 里声明的名字/位置）。
bool rhi_validate_render_pipeline_desc(const RenderPipelineDesc &desc, std::string &error);
