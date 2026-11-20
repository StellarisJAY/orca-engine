#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "rendering_device.h"

// =============================================================================
// 约定（所有后端必须遵守）
//
// 1. 句柄
//    所有句柄都是"不透明、非拥有"的轻量值类型：
//      - handle 是后端分配的唯一标识，0 表示无效；
//      - data 是后端私有对象的裸指针，只由对应后端解释，调用方不得解引用或释放；
//      - 句柄按值传递，销毁后调用方手里的副本即失效，必须重新从 create_* 获取。
//
// 2. 资源销毁
//    destroy_* 可以在任意时刻调用（包括提交后、帧中间）。Vulkan 后端内部自行处理
//    延迟销毁：在帧 fence 信号之后回收，避免删除仍被在途命令引用的资源。

// 3. 命令录制
//    一帧的流程是：create_command_encoder -> encoder_begin_render_pass ...
//    -> pass_end -> encoder_finish -> queue_submit。
//    queue_write_* 与 queue_submit 的先后顺序决定了数据可见性：
//    在同一帧内先写入、后提交的数据对本次提交的绘制可见。

// 4. 默认帧缓冲
//    ColorAttachmentDesc.view 为无效句柄时表示"默认帧缓冲"（交换链）。
//    此时 viewport 由交换链尺寸决定，深度附件由交换链的深度缓冲承担。

// 5. 绑定组的名字字段
//    BindGroupLayoutEntry.name 是调试/未来后端的保留字段（按 uniform 名绑定）。
//    Vulkan 按 binding 编号创建 descriptor set，忽略该字段；填写它便于把描述
//    与 shader 里的 uniform 名对照排查。
// =============================================================================

// =============================================================================
// 句柄
// =============================================================================

struct BaseHandle
{
    uint64_t handle = 0;
    void *data = nullptr;

    bool is_valid() const { return handle != 0; }
    explicit operator bool() const { return is_valid(); }
    bool operator==(const BaseHandle &other) const { return handle == other.handle; }
    bool operator!=(const BaseHandle &other) const { return !(*this == other); }
};

struct ShaderHandle : public BaseHandle
{
};

struct GPUBufferHandle : public BaseHandle
{
};

struct TextureHandle : public BaseHandle
{
};

struct TextureViewHandle : public BaseHandle
{
};

struct SamplerHandle : public BaseHandle
{
};

struct BindGroupLayoutHandle : public BaseHandle
{
    // 后端会缓存布局项供 create_bind_group / create_pipeline_layout 使用。
    // 销毁前必须先销毁引用它的 bind group 与 pipeline layout。
};

struct BindGroupHandle : public BaseHandle
{
    // Vulkan 后端持有布局与资源的非拥有指针；销毁 bind group 前不要销毁它们对应的
    // 布局、buffer、view、sampler。
};

struct PipelineLayoutHandle : public BaseHandle
{
    // 后端持有 bind group layout 的非拥有指针；销毁 pipeline layout 前必须先销毁
    // 引用这些布局的 bind group，并在销毁布局前销毁本对象。
};

struct RenderPipelineHandle : public BaseHandle
{
};

struct CommandEncoderHandle : public BaseHandle
{
};

struct RenderPassHandle : public BaseHandle
{
    // 句柄在所属 command buffer 被 destroy_command_buffer 释放前有效。
};

struct CommandBufferHandle : public BaseHandle
{
};

// =============================================================================
// 格式
// =============================================================================

/// @brief 纹理 / 渲染目标格式。
/// 新增格式时必须同时补三处：后端映射表、管线创建时的附件校验、shader 侧约定。
enum class Format
{
    Undefined = 0,
    R8Unorm,
    RG8Unorm,
    RGBA8Unorm,
    RGBA8UnormSrgb,
    R16Float,
    RGBA16Float,
    Depth32Float,
    /// @brief Vulkan 交换链在部分平台只提供 BGRA 通道顺序。
    BGRA8Unorm,
    BGRA8UnormSrgb,
};

/// @brief 顶点属性格式（与纹理格式分开，避免语义混淆）。
enum class VertexFormat
{
    Float32,
    Float32x2,
    Float32x3,
    Float32x4,
    Uint32,
    Uint8x4Normalized,
};

/// @brief 索引缓冲的元素类型。
enum class IndexFormat
{
    Uint16,
    Uint32,
};

// =============================================================================
// 缓冲区
// =============================================================================

/// @brief GPU 缓冲区用途（位掩码）。
class GPUBufferUsage
{
public:
    constexpr static uint8_t VERTEX = 1 << 0;
    constexpr static uint8_t INDEX = 1 << 1;
    constexpr static uint8_t UNIFORM = 1 << 2;
    constexpr static uint8_t COPY_DST = 1 << 3;
    constexpr static uint8_t COPY_SRC = 1 << 4;
    /// @brief 预留给 future compute / 存储缓冲，暂不要求后端实现。
    constexpr static uint8_t STORAGE = 1 << 5;
};

struct GPUBufferDesc
{
    /// @brief 缓冲区标签名，仅用于调试
    std::string label;
    /// @brief 缓冲区大小（字节）
    uint64_t size = 0;
    /// @brief 用途位掩码，见 GPUBufferUsage
    uint8_t usage = 0;
};

// =============================================================================
// 纹理 / 视图 / 采样器
// =============================================================================

enum class TextureDimension
{
    Texture1D,
    Texture2D,
    Cube,
};

enum class TextureViewDimension
{
    Texture2D,
    Cube,
};

enum class TextureAspect
{
    Color,
    DepthOnly,
};

/// @brief 纹理用途（位掩码）。
class TextureUsage
{
public:
    constexpr static uint8_t TEXTURE_BINDING = 1 << 0; // 纹理绑定
    constexpr static uint8_t RENDER_ATTACHMENT = 1 << 1; // 渲染framebuffer
    constexpr static uint8_t COPY_DST = 1 << 2;
    constexpr static uint8_t COPY_SRC = 1 << 3;
    constexpr static uint8_t STORAGE = 1 << 4; // shader写入
};

struct TextureDescRefactor
{
    std::string label;
    TextureDimension dim = TextureDimension::Texture2D;
    /// @brief 像素格式；深度图使用 Depth32Float
    Format format = Format::Undefined;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    /// @brief 采样数；本期只支持 1（无 MSAA）
    uint32_t samples_count = 1;
    /// @brief 用途位掩码，见 TextureUsage
    uint8_t usage = TextureUsage::TEXTURE_BINDING;
};

struct TextureViewDesc
{
    TextureViewDimension dim = TextureViewDimension::Texture2D;
    uint32_t base_mip = 0;
    /// @brief mip 层数；0 表示"从 base_mip 到最后一层"（等价于 WebGPU 的默认行为）
    uint32_t mip_count = 0;
    uint32_t base_layer = 0;
    /// @brief 层数；0 表示"从 base_layer 到最后一层"，cubemap 为 6
    uint32_t layer_count = 0;
    TextureAspect aspect = TextureAspect::Color;
};

/// @brief 采样器参数。对应 Vulkan 的独立 sampler 对象。
enum class AddressMode
{
    ClampToEdge,
    Repeat,
    MirrorRepeat,
};

enum class FilterMode
{
    Nearest,
    Linear,
};

/// @brief 深度比较函数：采样器 compare_op（阴影 PCF）与管线 depth_compare 共用同一套取值。
///
/// 命名沿用 WebGPU 的 CompareFunction：`LessEqual` 表示"新值 <= 旧值"通过比较
/// （Vulkan 的 `VK_COMPARE_OP_LESS_OR_EQUAL`）。
enum class CompareFunction
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

struct SamplerDesc
{
    AddressMode address_u = AddressMode::Repeat;
    AddressMode address_v = AddressMode::Repeat;
    AddressMode address_w = AddressMode::Repeat;
    FilterMode mag_filter = FilterMode::Linear;
    FilterMode min_filter = FilterMode::Linear;
    FilterMode mipmap_filter = FilterMode::Linear;
    float lod_min_clamp = 0.0f;
    float lod_max_clamp = 1000.0f;
    /// @brief 深度比较采样（阴影 PCF 预留）；对应 Vulkan 的 compareEnable
    bool compare_enable = false;
    /// @brief 比较函数；仅 compare_enable 为 true 时生效。默认 LessEqual 与 WebGPU 一致。
    ///        Vulkan 映射为 VkSamplerCreateInfo::compareOp。
    CompareFunction compare_op = CompareFunction::LessEqual;
};

/// @brief 纹理上传描述。
struct TextureWriteDesc
{
    uint32_t mip_level = 0;
    /// @brief 数组层 / cubemap 面索引（0..5）
    uint32_t array_layer = 0;
    uint32_t origin_x = 0;
    uint32_t origin_y = 0;
    /// @brief 上传区域尺寸；为 0 时取纹理该 mip 的完整尺寸
    uint32_t width = 0;
    uint32_t height = 0;
    /// @brief 每行字节数；0 表示按 width*格式字节数紧密打包
    uint32_t bytes_per_row = 0;
    const void *data = nullptr;
    uint64_t data_size = 0;
};

// =============================================================================
// 绑定组 / 管线布局
// =============================================================================

/// @brief 着色阶段可见性（位掩码）。
class ShaderStage
{
public:
    constexpr static uint8_t VERTEX = 1 << 0;
    constexpr static uint8_t FRAGMENT = 1 << 1;
};

enum class BindingType
{
    UniformBuffer,
    Texture2D,
    TextureCube,
    /// @brief 深度纹理（阴影贴图），在 GLSL 中声明为 sampler2D
    DepthTexture,
    Sampler,
};

struct BindGroupLayoutEntry
{
    /// @brief 组内绑定编号
    uint32_t binding = 0;
    /// @brief 可见阶段，见 ShaderStage
    uint8_t visibility = ShaderStage::FRAGMENT;
    BindingType type = BindingType::UniformBuffer;
    /// @brief 调试/未来后端的保留字段（按 uniform 名绑定）；Vulkan 忽略
    std::string name;
    /// @brief UniformBuffer：单次绑定的最小字节数；0 表示不校验
    uint64_t min_binding_size = 0;
    /// @brief UniformBuffer：是否允许动态偏移（per-draw 数据推荐开启）
    bool has_dynamic_offset = false;
};

struct BindGroupLayoutDesc
{
    std::string label;
    std::vector<BindGroupLayoutEntry> entries;
};

struct BindingResource
{
    BindingType type = BindingType::UniformBuffer;
    GPUBufferHandle buffer;
    TextureViewHandle texture_view;
    SamplerHandle sampler;
    /// @brief UniformBuffer 的起始偏移
    uint64_t offset = 0;
    /// @brief UniformBuffer 的绑定字节数；0 表示从 offset 到缓冲区末尾
    uint64_t size = 0;
};

struct BindGroupEntry
{
    uint32_t binding = 0;
    BindingResource resource;
};

struct BindGroupDesc
{
    std::string label;
    BindGroupLayoutHandle layout;
    std::vector<BindGroupEntry> entries;
};

struct PipelineLayoutDesc
{
    std::string label;
    /// @brief 按数组下标对应 group index：bind_group_layouts[i] 即 group i
    std::vector<BindGroupLayoutHandle> bind_group_layouts;
};

// =============================================================================
// 渲染管线
// =============================================================================

/// @brief 图元模式。
struct Primitive
{
    enum class Topology
    {
        TriangleList,
        TriangleStrip,
    };
    enum class CullMode
    {
        None,
        Back,
        Front,
    };
    enum class FrontFace
    {
        CCW,
        CW,
    };

    Topology topology = Topology::TriangleList;
    CullMode cull_mode = CullMode::None;
    FrontFace front_face = FrontFace::CCW;
};

struct BufferLayout
{
    struct Attribute
    {
        uint32_t shader_location = 0;
        /// @brief 相对该顶点缓冲起始位置的字节偏移
        uint32_t offset = 0;
        VertexFormat format = VertexFormat::Float32;
    };

    /// @brief 单个顶点的字节跨度
    uint32_t array_stride = 0;
    std::vector<Attribute> attributes;
};

/// @brief 顶点着色阶段。
struct VertexStage
{
    ShaderHandle module;
    /// @brief 顶点着色器入口函数名（GLSL 忽略，Vulkan/SPIR-V 必填）
    std::string entry_point = "main";
    /// @brief 顶点缓冲区布局，下标对应 pass_set_vertex_buffer 的 slot
    std::vector<BufferLayout> buffers;
};

/// @brief 颜色写掩码（位掩码）。
class ColorWriteMask
{
public:
    constexpr static uint8_t R = 1 << 0;
    constexpr static uint8_t G = 1 << 1;
    constexpr static uint8_t B = 1 << 2;
    constexpr static uint8_t A = 1 << 3;
    constexpr static uint8_t ALL = R | G | B | A;
};

enum class BlendFactor
{
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha,
};

enum class BlendOperation
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

struct BlendComponent
{
    BlendOperation operation = BlendOperation::Add;
    BlendFactor src_factor = BlendFactor::One;
    BlendFactor dst_factor = BlendFactor::Zero;
};

struct BlendState
{
    bool enabled = false;
    BlendComponent color;
    BlendComponent alpha;
};

struct ColorTargetState
{
    /// @brief 必须与对应颜色附件的格式一致
    Format format = Format::RGBA8Unorm;
    BlendState blend;
    uint8_t write_mask = ColorWriteMask::ALL;
};

/// @brief 片段着色阶段。targets 为空表示纯深度 pass（不输出颜色）。
///
/// 即使 targets 为空也必须提供 fragment 模块：只要管线没有启用 rasterizer discard，
/// Vulkan 就要求完整的 fragment shader 状态（深度/模板测试属于片段操作）。
struct FragmentStage
{
    ShaderHandle module;
    std::string entry_point = "main";
    std::vector<ColorTargetState> targets;
};

struct DepthStencilState
{
    /// @brief 必须与深度附件格式一致；Undefined 表示关闭深度读写
    Format format = Format::Undefined;
    bool depth_write_enabled = false;
    CompareFunction depth_compare = CompareFunction::Always;
};

struct RenderPipelineDesc
{
    std::string label;
    /// @brief 绑定组布局；必须通过 create_pipeline_layout 显式声明
    PipelineLayoutHandle pipeline_layout;
    VertexStage vertex;
    FragmentStage fragment;
    Primitive primitive;
    DepthStencilState depth_stencil;
    /// @brief 多重采样数；本期必须为 1
    uint32_t samples_count = 1;
};

// =============================================================================
// 渲染通道
// =============================================================================

enum class LoadOp
{
    Load,
    Clear,
    DontCare,
};

enum class StoreOp
{
    Store,
    Discard,
};

struct ColorAttachmentDesc
{
    /// @brief 无效句柄表示默认帧缓冲（交换链）
    TextureViewHandle view;
    LoadOp load_op = LoadOp::Clear;
    StoreOp store_op = StoreOp::Store;
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthAttachmentDesc
{
    /// @brief 无效句柄表示默认帧缓冲的内部深度缓冲（仅当 color 走默认帧缓冲时可用）
    TextureViewHandle view;
    LoadOp load_op = LoadOp::Clear;
    StoreOp store_op = StoreOp::Store;
    float clear_depth = 1.0f;
};

struct RenderPassDesc
{
    std::string label;
    std::vector<ColorAttachmentDesc> color_attachments;
    /// @brief 目前只支持 1 个深度附件；为空表示无深度
    std::vector<DepthAttachmentDesc> depth_attachments;
    /// @brief 渲染区域尺寸；为 0 时由附件尺寸推导
    uint32_t width = 0;
    uint32_t height = 0;
};

// =============================================================================
// 窗口 / 驱动 / Shader
// =============================================================================

enum class ShaderType
{
    VERTEX,
    FRAGMENT,
};

/// @brief ShaderModuleDesc::source 的解释方式。
///
/// 源类型必须由调用方显式给出，后端不允许猜测：Vulkan 只接受预编译的 SPIR-V，
/// 现存的 `#version 330 core` 文本必须先经工具链（`shaders-vulkan/` + glslc）
/// 编译成 `.spv`（见 docs/vulkan_rhi_checklist_v2.md 的 V4）。
enum class ShaderSourceKind
{
    /// @brief UTF-8 GLSL 文本（不能直接交给 Vulkan）
    GLSL_TEXT,
    /// @brief SPIR-V 二进制指令流（小端、按 4 字节对齐的 word 序列）
    SPIRV,
};

struct ShaderModuleDesc
{
    std::string name;
    /// @brief 源码内容；按 source_kind 解释为文本或二进制。
    ///
    /// 二进制用 std::string 承载是为了兼容既有接口；其中的 `\0` 不会被截断，
    /// 但交给 Vulkan 前后端会复制到对齐的 `uint32_t` 缓冲（pCode 要求 4 字节对齐）。
    std::string source;
    ShaderSourceKind source_kind = ShaderSourceKind::SPIRV;
    /// @brief 入口函数名；SPIR-V 里必须真实存在（默认 "main"）。
    ///
    /// 该字段只用于校验模块本身；真正的管线入口名以
    /// VertexStage::entry_point / FragmentStage::entry_point 为准。
    std::string entry_point = "main";
    ShaderType type = ShaderType::VERTEX;
};

// =============================================================================
// 设备接口
// =============================================================================

/// @brief 现代图形 API 风格渲染后端接口（仿 WebGPU）。
///
/// 当前唯一实现是 Vulkan（原 Refactor 版 OpenGL 适配层已移除）。
/// 分层约定：
///   - 本接口只描述"所有现代后端共有"的显式概念（资源对象、绑定组、管线、命令录制）；
///   - 后端专有细节（Vulkan 的 layout/barrier/descriptor pool）
///     必须封装在 Vulkan 后端内部，不得出现在本头文件里；
///   - 头文件不依赖 math / core 模块，保持渲染层内部自洽。
class RenderingDeviceRefactor
{
public:
    virtual ~RenderingDeviceRefactor() = default;

    // ------------------------ 初始化流程 -------------------------------------

    /// @brief 初始化渲染后端（加载入口点 / 创建 instance）
    /// @param info 窗口信息
    /// @return 成功?
    virtual bool init(const RenderingWindowInfo &info) = 0;

    /// @brief 初始化逻辑设备(物理设备集合->唯一逻辑设备)
    /// @return 成功?
    virtual bool init_device() = 0;

    /// @brief 初始化交换链（Vulkan: 创建 swapchain 及配套资源）
    /// @return 成功?
    virtual bool init_swapchain() = 0;

    /// @brief 交换链尺寸变化后重建交换链相关资源（默认帧缓冲深度缓冲等）
    virtual void resize_swapchain(uint32_t width, uint32_t height) = 0;

    /// @brief 默认帧缓冲（交换链）的像素格式。
    /// 创建渲染管线时，ColorTargetState::format 必须使用该格式。
    virtual Format get_swapchain_format() const = 0;

    /// @brief 默认帧缓冲（交换链）的当前尺寸。
    /// 不传附件 view 的 render pass 用它确定 viewport。
    virtual void get_swapchain_size(uint32_t &width, uint32_t &height) const = 0;

    /// @brief 呈现一帧。Vulkan 后端在这里做 vkQueuePresentKHR。
    virtual void present() = 0;

    virtual void shutdown() = 0;

    // ------------------------- Shader ---------------------------------------

    /// @brief 创建 shader 模块（当前 Vulkan 只接受 ShaderSourceKind::SPIRV）
    virtual ShaderHandle create_shader_module(const ShaderModuleDesc &desc) = 0;
    virtual void destroy_shader_module(ShaderHandle handle) = 0;

    // ------------------------- 缓冲区 ---------------------------------------

    /// @brief 创建 GPU 缓冲区
    virtual GPUBufferHandle create_buffer(const GPUBufferDesc &desc) = 0;
    /// @brief 写入 GPU 缓冲区（size 为字节数）
    virtual void queue_write_buffer(GPUBufferHandle handle, uint64_t offset, const void *data, uint64_t size) = 0;
    virtual void destroy_buffer(GPUBufferHandle handle) = 0;

    // ------------------------- 纹理 / sampler --------------------------------

    virtual TextureHandle create_texture(const TextureDescRefactor &desc) = 0;
    virtual TextureViewHandle create_texture_view(TextureHandle texture, const TextureViewDesc &desc) = 0;
    virtual SamplerHandle create_sampler(const SamplerDesc &desc) = 0;
    virtual void queue_write_texture(TextureHandle texture, const TextureWriteDesc &desc) = 0;
    /// @brief 生成完整 mipmap 链
    virtual void generate_mip_map(TextureHandle texture) = 0;
    virtual void destroy_texture(TextureHandle texture) = 0;
    virtual void destroy_texture_view(TextureViewHandle view) = 0;
    virtual void destroy_sampler(SamplerHandle sampler) = 0;

    // ------------------------- 绑定组 / 管线布局 ------------------------------

    virtual BindGroupLayoutHandle create_bind_group_layout(const BindGroupLayoutDesc &desc) = 0;
    virtual BindGroupHandle create_bind_group(const BindGroupDesc &desc) = 0;
    virtual PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc &desc) = 0;
    virtual void destroy_bind_group(BindGroupHandle bind_group) = 0;
    virtual void destroy_bind_group_layout(BindGroupLayoutHandle layout) = 0;
    virtual void destroy_pipeline_layout(PipelineLayoutHandle layout) = 0;

    // ------------------------- 渲染管线 -------------------------------------

    /// @brief 创建渲染管线（绑定 shader、顶点布局、深度/混合/图元状态、目标格式）
    virtual RenderPipelineHandle create_render_pipeline(const RenderPipelineDesc &desc) = 0;
    virtual void destroy_render_pipeline(RenderPipelineHandle pipeline) = 0;

    // ------------------------- command & render pass -------------------------

    /// @brief 创建指令编码器。一个 encoder 可以录制多个 render pass，
    ///        finish 之后 encoder 失效。
    virtual CommandEncoderHandle create_command_encoder() = 0;

    /// @brief 开始一个渲染通道的指令录制
    virtual RenderPassHandle encoder_begin_render_pass(CommandEncoderHandle encoder, const RenderPassDesc &desc) = 0;

    /// @brief 绑定渲染管线
    ///
    /// 调用顺序约定：本命令必须先于 `pass_set_bind_group`、`pass_set_vertex_buffer`
    /// 与 `pass_set_index_buffer`，后三者需要从当前管线取 pipeline layout / 顶点布局做校验。
    /// 管线与 pass 的附件格式/采样数必须兼容（Vulkan 的 render pass 兼容性）。
    virtual void pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline) = 0;

    /// @brief 绑定绑定组
    /// @param group_index 组下标，对应 PipelineLayoutDesc::bind_group_layouts 的下标
    /// @param dynamic_offsets 动态偏移数组，与布局中 has_dynamic_offset 的项按顺序对应
    ///
    /// 绑定组的布局必须与当前管线的 pipeline layout 在 `group_index` 处的布局一致；
    /// 动态偏移的数量必须等于该布局中 `has_dynamic_offset` 的项数，每个偏移按
    /// uniform buffer 对齐要求校验，并且 offset + 动态偏移 + 区间不得超过缓冲区大小。
    virtual void pass_set_bind_group(RenderPassHandle pass, uint32_t group_index, BindGroupHandle bind_group,
                                     const uint32_t *dynamic_offsets = nullptr, uint32_t dynamic_offset_count = 0) = 0;

    /// @brief 设置某个 slot 的顶点缓冲区
    /// @param slot 顶点缓冲区槽位，对应 VertexStage::buffers 的下标
    /// @param offset 缓冲区内的起始字节偏移
    ///
    /// 缓冲区必须带 `GPUBufferUsage::VERTEX`；offset 不能越过缓冲区末尾。
    /// 已绑定管线时 slot 必须小于管线声明的顶点缓冲区数量。
    virtual void pass_set_vertex_buffer(RenderPassHandle pass, uint32_t slot, GPUBufferHandle buffer, uint64_t offset = 0) = 0;

    /// @brief 设置索引缓冲区
    /// @param offset 缓冲区内的起始字节偏移，必须是索引元素大小的整数倍
    ///
    /// 缓冲区必须带 `GPUBufferUsage::INDEX`，offset 不能越过缓冲区末尾。
    virtual void pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer, uint64_t offset = 0,
                                       IndexFormat format = IndexFormat::Uint32) = 0;

    /// @brief 设置 viewport（x/y/width/height 为像素；min_depth/max_depth 为 0..1 的深度范围）
    ///
    /// 本接口没有独立的 scissor 命令（G-04）：后端按 viewport 派生 scissor 并裁剪到
    /// render area。render pass 开始时后端会先设置一个覆盖整个 render area 的默认
    /// viewport/scissor，因此不调用本命令也能绘制；需要局部渲染时再覆盖它。
    virtual void pass_set_viewport(RenderPassHandle pass, float x, float y, float width, float height,
                                   float min_depth = 0.0f, float max_depth = 1.0f) = 0;

    /// @brief 索引绘制
    virtual void pass_draw_indexed(RenderPassHandle pass, uint32_t index_count, uint32_t instance_count = 1,
                                   uint32_t first_index = 0, int32_t base_vertex = 0) = 0;

    /// @brief 非索引绘制
    virtual void pass_draw(RenderPassHandle pass, uint32_t vertex_count, uint32_t instance_count = 1,
                           uint32_t first_vertex = 0) = 0;

    /// @brief 结束渲染通道
    virtual void pass_end(RenderPassHandle pass) = 0;

    /// @brief 结束指令录制，返回可提交的 command buffer
    virtual CommandBufferHandle encoder_finish(CommandEncoderHandle encoder) = 0;

    /// @brief 销毁 command buffer（提交完成后由调用方释放）
    ///        该录制创建的 render pass/framebuffer 等内部对象随之释放，
    ///        之后对应的 RenderPassHandle 不再有效。
    virtual void destroy_command_buffer(CommandBufferHandle cmd) = 0;

    // ------------------------- 设备指令队列 ----------------------------------

    /// @brief 提交录制的指令 buffer
    virtual void queue_submit(CommandBufferHandle cmd) = 0;
    /// @brief 阻塞等待队列空闲
    virtual void queue_wait_idle() = 0;
    /// @brief 提交当前帧，返回单调递增的帧序号（供延迟销毁 / 多帧在飞使用）
    virtual uint64_t submit_frame() = 0;

    // ------------------------- 单例 -----------------------------------------

    static void create_singleton();
    /// @brief 显式指定驱动；当前只支持 Vulkan，传入其他驱动会打印错误并保持空单例。
    static void create_singleton(RenderingDriverType driver);
    static void destroy_singleton();
    static RenderingDriverType get_default_driver();
    static std::shared_ptr<RenderingDeviceRefactor> get_singleton() { return singleton; }

private:
    static std::shared_ptr<RenderingDeviceRefactor> singleton;
};

inline std::shared_ptr<RenderingDeviceRefactor> RenderingDeviceRefactor::singleton;
