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
//    destroy_* 可以在任意时刻调用（包括提交后、帧中间）。后端内部自行处理延迟销毁：
//    OpenGL 立即删除（驱动保证安全），Vulkan 延迟到对应 fence 信号之后。
//
// 3. 命令录制
//    一帧的流程是：create_command_encoder -> encoder_begin_render_pass ...
//    -> pass_end -> encoder_finish -> queue_submit。
//    queue_write_* 与 queue_submit 的先后顺序决定了数据可见性：
//    在同一帧内先写入、后提交的数据对本次提交的绘制可见。
//
// 4. 默认帧缓冲
//    ColorAttachmentDesc.view 为无效句柄时表示"默认帧缓冲"（交换链）。
//    此时 viewport 由交换链尺寸决定，深度附件由后端提供的交换链深度缓冲承担。
//
// 5. 绑定组的名字字段
//    GLSL 3.30 不支持 layout(binding = N)，OpenGL 后端必须通过名字定位
//    uniform block / sampler uniform。因此 BindGroupLayoutEntry.name 必填：
//      - UniformBuffer 项填 uniform block 名（如 "ViewBlock"）；
//      - 纹理 / 采样器项填 shader 里的 sampler uniform 名（如 "u_albedo_texture"）。
//    Vulkan 后端可以忽略该字段。
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
};

struct BindGroupHandle : public BaseHandle
{
};

struct PipelineLayoutHandle : public BaseHandle
{
};

struct RenderPipelineHandle : public BaseHandle
{
};

struct CommandEncoderHandle : public BaseHandle
{
};

struct RenderPassHandle : public BaseHandle
{
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

/// @brief 采样器参数。OpenGL 3.3 原生支持独立 sampler 对象，可直接映射。
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
    /// @brief 深度比较采样（阴影 PCF 预留）；GL 对应 GL_TEXTURE_COMPARE_MODE
    bool compare_enable = false;
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
    /// @brief GL 后端用它定位 uniform block / sampler uniform；Vulkan 可忽略
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
struct FragmentStage
{
    ShaderHandle module;
    std::string entry_point = "main";
    std::vector<ColorTargetState> targets;
};

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

struct ShaderModuleDesc
{
    std::string name;
    std::string source;
    ShaderType type = ShaderType::VERTEX;
};

// =============================================================================
// 设备接口
// =============================================================================

/// @brief 现代图形 API 风格渲染后端接口（仿 WebGPU）。
///
/// 分层约定：
///   - 本接口只描述"所有现代后端共有"的显式概念（资源对象、绑定组、管线、命令录制）；
///   - 后端专有细节（GL 的 FBO/VAO/状态机、Vulkan 的 layout/barrier/descriptor pool）
///     必须封装在各自后端内部，不得出现在本头文件里；
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

    /// @brief 初始化交换链（OpenGL: 配置默认帧缓冲）
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

    /// @brief 呈现一帧。OpenGL 后端为 no-op（交换仍由平台层完成）；
    ///        Vulkan 后端在这里做 vkQueuePresentKHR。
    virtual void present() = 0;

    virtual void shutdown() = 0;

    // ------------------------- Shader ---------------------------------------

    /// @brief 创建 shader 模块（顶点 + 片段源码）
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
    virtual void pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline) = 0;

    /// @brief 绑定绑定组
    /// @param group_index 组下标，对应 PipelineLayoutDesc::bind_group_layouts 的下标
    /// @param dynamic_offsets 动态偏移数组，与布局中 has_dynamic_offset 的项按顺序对应
    virtual void pass_set_bind_group(RenderPassHandle pass, uint32_t group_index, BindGroupHandle bind_group,
                                     const uint32_t *dynamic_offsets = nullptr, uint32_t dynamic_offset_count = 0) = 0;

    /// @brief 设置某个 slot 的顶点缓冲区
    virtual void pass_set_vertex_buffer(RenderPassHandle pass, uint32_t slot, GPUBufferHandle buffer, uint64_t offset = 0) = 0;

    /// @brief 设置索引缓冲区
    virtual void pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer, uint64_t offset = 0,
                                       IndexFormat format = IndexFormat::Uint32) = 0;

    /// @brief 设置 viewport（x/y/width/height 为像素；min_depth/max_depth 为 0..1 的深度范围）
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
    static void create_singleton(RenderingDriverType driver);
    static void destroy_singleton();
    static RenderingDriverType get_default_driver();
    static std::shared_ptr<RenderingDeviceRefactor> get_singleton() { return singleton; }

private:
    static std::shared_ptr<RenderingDeviceRefactor> singleton;
};

inline std::shared_ptr<RenderingDeviceRefactor> RenderingDeviceRefactor::singleton;
