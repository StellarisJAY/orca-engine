#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// ------------------------- 绑定组 / 管线布局 ----------------------------------

/// @brief 绑定组布局项的后端无关视图（Vulkan 创建 descriptor set layout 时缓存的字段）。
struct RhiBindGroupLayoutEntry
{
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    /// @brief 可见阶段位掩码，见 ShaderStage
    uint8_t visibility = ShaderStage::FRAGMENT;
    /// @brief UniformBuffer：单次绑定的最小字节数；0 表示不校验
    uint64_t min_binding_size = 0;
    /// @brief UniformBuffer：是否允许动态偏移
    bool has_dynamic_offset = false;
};

/// @brief 已创建绑定组布局的条目集合（按创建顺序）。
struct RhiBindGroupLayoutInfo
{
    std::vector<RhiBindGroupLayoutEntry> entries;
};

/// @brief 创建绑定组时一个 entry 的资源信息（后端从自己的资源对象填充，供共享校验）。
///
/// 共享层只判断"资源与布局项是否匹配"，不检查对齐、数量上限等后端能力；
/// 这些由各后端在调用本校验之前/之后追加。
struct RhiBindingResourceInfo
{
    BindingType type = BindingType::UniformBuffer;
    /// @brief UniformBuffer：是否提供了有效的缓冲区
    bool has_buffer = false;
    /// @brief UniformBuffer：创建缓冲区时的用途位掩码（必须包含 UNIFORM）
    uint8_t buffer_usage = 0;
    uint64_t buffer_size = 0;
    uint64_t offset = 0;
    /// @brief 绑定字节数；0 表示从 offset 到缓冲区末尾
    uint64_t size = 0;
    /// @brief 纹理绑定：是否提供了有效的纹理视图
    bool has_view = false;
    TextureViewDimension view_dim = TextureViewDimension::Texture2D;
    TextureAspect view_aspect = TextureAspect::Color;
    Format view_format = Format::Undefined;
    /// @brief 纹理绑定 / Sampler 绑定：是否提供了有效的采样器
    bool has_sampler = false;
    /// @brief 纹理绑定：采样器是否启用了深度比较（compare_enable）
    bool sampler_compare_enable = false;
};

struct RhiBindGroupEntryInfo
{
    uint32_t binding = 0;
    RhiBindingResourceInfo resource;
};

/// @brief 解析后的绑定组项；buffer 的 size == 0 已展开成实际字节数。
struct RhiResolvedBinding
{
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    uint64_t offset = 0;
    uint64_t size = 0;
    bool has_dynamic_offset = false;
};

/// @brief 校验绑定组布局描述。
///
/// 约束：
///   - binding 编号不能重复；
///   - visibility 不能为空，且只能包含 ShaderStage 已定义的位；
///   - has_dynamic_offset / min_binding_size 只对 UniformBuffer 有意义，其它类型必须为默认值。
///
/// 空条目集合合法（空布局可以对应空绑定组）。
bool rhi_validate_bind_group_layout_desc(const BindGroupLayoutDesc &desc, std::string &error);

/// @brief 校验 bind group 的 entry 与布局是否匹配，并解析 buffer 区间。
///
/// 规则：
///   - 布局里的每个 binding 必须恰好出现一次，不能缺、不能多、不能重复；
///   - entry 的资源类型必须与布局项一致；
///   - UniformBuffer：必须提供带 UNIFORM 用途的缓冲区，offset/size 不越界，
///     `size == 0` 解析为"到缓冲区末尾"，解析后不能为 0，且不小于 `min_binding_size`；
///   - Texture2D / TextureCube：必须是颜色 aspect、非深度格式、维度匹配，且带采样器
///     （组合图像采样器）；
///   - DepthTexture：必须是 DepthOnly aspect 的深度格式 2D 视图，且带采样器；
///   - Sampler：只提供采样器。
bool rhi_resolve_bind_group_desc(const RhiBindGroupLayoutInfo &layout,
                                 const std::vector<RhiBindGroupEntryInfo> &entries,
                                 std::vector<RhiResolvedBinding> &bindings,
                                 std::string &error);

/// @brief 校验管线布局描述（只校验组布局句柄是否有效；空布局合法）。
bool rhi_validate_pipeline_layout_desc(const PipelineLayoutDesc &desc, std::string &error);

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
