#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <memory>

// ---------------------- Handles ----------------------------------

struct ShaderHandle
{
};

struct RenderPipelineHandle
{
};

struct BindGroupLayoutHandle
{
};

struct CommandEncoderHandle
{
};

struct RenderPassHandle
{
};

struct CommandBufferHandle
{
};

struct GPUBufferHandle
{
};

struct TextureHandle
{
};

struct TextureViewHandle
{
};

struct SamplerHandle
{
};

// ---------------------- End Handles ----------------------------------

// 平台层向图形后端传递的原生窗口/上下文信息
struct RenderingWindowInfo
{
    void *window = nullptr;   // 原生窗口句柄
    void *display = nullptr;  // 原生显示/连接
    void *instance = nullptr; // 原生实例/模块句柄
    void *context = nullptr;  // 已创建的图形上下文（OpenGL: HGLRC/GLXContext；Vulkan 为空）
    uint32_t width = 0;
    uint32_t height = 0;

    // 按名称加载后端入口点。OpenGL 后端依赖它动态加载 GL 函数；Vulkan 后端可忽略。
    std::function<void *(const char *)> get_proc_address;
    // 为 Vulkan 创建呈现表面。参数为后端原生 instance（VkInstance），
    // 输出为 VkSurfaceKHR（以 uint64_t 传输，避免在公共头里暴露 Vulkan 类型）。
    std::function<bool(void *native_instance, uint64_t &out_surface)> create_vulkan_surface;
    // 平台声明的 Vulkan 实例扩展名（例如 GLFW 的 glfwGetRequiredInstanceExtensions）。
    std::vector<const char *> vulkan_instance_extensions;
};

struct ShaderModuleDesc
{
    std::string name;
    std::string vertex_source;
    std::string fragment_source;
};

/// @brief 渲染目标画面
struct ColorAttachmentDesc
{
};

/// @brief 渲染目标深度
struct DepthAttachmentDesc
{
};

/// @brief 图元模式
struct Primitive
{
    /// @brief 图元拓扑方式
    enum class Topology
    {
        // 三个点组成三角形
        TriangleList,
    };
    /// @brief 面剔除模式
    enum class CullMode
    {
        // 不剔除
        None,
        // 剔除背面
        Back,
        // 剔除正面
        Front,
    };
    /// @brief 正面定义
    enum class FrontFace
    {
        // 逆时针
        CCW,
    };

    Topology topology = Topology::TriangleList; // 默认三点为一个独立三角形
    CullMode cull_mode = CullMode::None;        // 默认不剔除面
    FrontFace front_face = FrontFace::CCW;      // 默认逆时针为正面
};

struct BufferLayout
{
    struct Attribute
    {
        uint32_t shader_location = 0;
        uint32_t offset = 0;
        // TODO format enum
    };

    uint32_t array_stride = 0;
    std::vector<Attribute> attributes;
};

/// @brief 顶点着色阶段
struct VertexStage
{
    /// @brief shader程序
    ShaderHandle module;
    /// @brief 顶点着色器入口函数名
    std::string entry_point;
    /// @brief 缓冲区layout,渲染通道传递的buffer用该layout解析
    std::vector<BufferLayout> buffers;
};

/// @brief 片段着色阶段
struct FragmentStage
{
    /// @brief shader程序
    ShaderHandle module;
    /// @brief 片段着色器入口函数名
    std::string entry_point;
};

struct RenderPipelineDesc
{
    /// @brief 渲染管线标签名
    std::string label;
    /// @brief 渲染管线的顶点着色阶段
    VertexStage vertex;
    /// @brief 渲染管线的片段着色阶段
    FragmentStage fragment;
    /// @brief 渲染管线的几何属性，包含拓扑方式、面剔除、正面定义
    Primitive primitive;
};

/// @brief GPU缓冲区用途，暂时只做顶点缓冲、索引缓冲和uniform缓冲
class GPUBufferUsage
{
public:
    constexpr static uint8_t VERTEX = 1 << 0;
    constexpr static uint8_t INDEX = 1 << 1;
    constexpr static uint8_t UNIFORM = 1 << 2;
};

/// @brief 创建GPU缓冲区信息
struct GPUBufferDesc
{
    /// @brief 缓冲区标签名
    std::string label;
    /// @brief 缓冲区大小
    uint64_t size = 0;
    /// @brief 缓冲区用途
    uint8_t usage;
};

struct RenderPassDesc
{
    std::string label;
    std::vector<ColorAttachmentDesc> color_attachments;
    std::vector<DepthAttachmentDesc> depth_attachments;
};

enum class TextureDimension
{
    Texture2D,
    Cube,
};

enum class TextureViewDimension
{
    Texture2D,
    Cube,
};

class TextureUsage
{
public:
    constexpr static uint8_t TEXTURE_BINDING = 1 << 0;
    constexpr static uint8_t RENDER_ATTACHMENT = 1 << 1;
};

struct TextureDesc
{
    TextureDimension dim;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    uint32_t samples_count = 1;
    uint8_t texture_usage = TextureUsage::TEXTURE_BINDING;
};

enum class TextureAspect
{
    Color,
    DepthOnly,
};

struct TextureViewDesc
{
    TextureViewDimension dim;
    uint32_t base_mip, mip_count, base_layer, layer_count;
    TextureAspect aspect;
};

struct SamplerDesc
{
};

struct TextureWriteDesc
{
};

enum class RenderingDriverType
{
    Vulkan,
    OpenGL,
};

/// @brief 现代图形API风格渲染后端接口
/// @brief 模仿WebGPU的API风格，屏蔽底层API(Vulkan,DirectX,Metal,OpenGL)的差异
/// @brief 由后端适配器自行适配接口差异
class RenderingDeviceRefactor
{
public:
    // ------------------------ 初始化流程 -------------------------------------

    /// @brief 初始化渲染后端
    /// @param info 窗口信息
    /// @return 成功?
    virtual bool init(const RenderingWindowInfo &info) = 0;

    /// @brief 初始化逻辑设备(物理设备集合->唯一逻辑设备)
    /// @return 成功?
    virtual bool init_device() = 0;

    /// @brief 初始化交换链
    /// @return 成功?
    virtual bool init_swapchain() = 0;

    virtual void shutdown() = 0;

    // ------------------------- Shader ---------------------------------------

    /// @brief 创建shader
    /// @param desc shader描述+代码
    /// @return shader句柄
    virtual ShaderHandle create_shader_module(const ShaderModuleDesc &desc) = 0;
    /// @brief 销毁shader
    /// @param handle
    virtual void destroy_shader_module(ShaderHandle handle) = 0;

    // ------------------------- 缓冲 -----------------------------------------

    /// @brief 创建GPU缓冲区
    /// @param desc 缓冲区描述
    /// @return handle
    virtual GPUBufferHandle create_buffer(const GPUBufferDesc &desc) = 0;

    /// @brief 写入GPU缓冲区
    /// @param handle 缓冲区handle
    /// @param offset 偏移
    /// @param data 数据
    virtual void queue_write_buffer(GPUBufferHandle handle, uint64_t offset, float *data) = 0;

    // ------------------------- 绑定组 ------------------------------------

    /// @brief 创建绑定组
    /// @return
    virtual BindGroupLayoutHandle create_bind_group_layout() = 0;

    // ------------------------- 纹理、sampler ----------------------------------------

    /// @brief 创建纹理
    /// @param desc 纹理描述
    /// @return 纹理hanle
    virtual TextureHandle create_texture(const TextureDesc &desc) = 0;
    /// @brief 创建纹理view
    /// @param texture 纹理handle
    /// @param desc 视图描述
    /// @return 视图handle
    virtual TextureViewHandle create_texture_view(TextureHandle texture, const TextureViewDesc &desc) = 0;
    virtual SamplerHandle create_sampler(const SamplerDesc &desc) = 0;
    virtual void queue_write_texture(TextureHandle texture, const TextureWriteDesc &desc) = 0;
    virtual void generate_mip_map(TextureHandle texture) = 0;

    // ------------------------- 渲染管线 ----------------------------------------

    /// @brief 创建渲染管线
    /// @brief 渲染管线负责管理buffer的layout，使用什么shader程序，输出到什么格式
    /// @brief 渲染管现在绘制前创建，绘制时绑定管线
    /// @return 句柄
    virtual RenderPipelineHandle create_render_pipeline(const RenderPipelineDesc &desc) = 0;

    // ------------------------- command & render pass --------------------------

    /// @brief 创建指令编码器
    /// @return 句柄
    virtual CommandEncoderHandle create_command_encoder() = 0;

    /// @brief 开启渲染通道指令录制
    /// @brief 一个渲染流程 = 多个渲染通道的指令录制
    /// @brief eg: 深度贴图pass->PBR pass->后处理pass
    /// @param encoder 编码器handle
    /// @param desc 渲染通道描述
    /// @return render pass句柄
    virtual RenderPassHandle encoder_begin_render_pass(CommandEncoderHandle encoder, const RenderPassDesc &desc) = 0;

    /// @brief 设置渲染通道的渲染管线
    /// @param pass 通道handle
    /// @param pipeline 管线handle
    virtual void pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline) = 0;

    virtual void pass_set_bind_group(RenderPassHandle pass) = 0;

    /// @brief 设置槽位的缓冲区
    /// @param pass 渲染通道handle
    /// @param slot layout槽位
    /// @param buffer 缓冲区handle
    virtual void pass_set_vertex_buffer(RenderPassHandle pass, uint8_t slot, GPUBufferHandle buffer) = 0;

    /// @brief 设置索引缓冲区
    /// @param pass 渲染通道handle
    /// @param buffer 缓冲区handle
    virtual void pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer) = 0;

    /// @brief 设置viewport
    /// @param pass 渲染通道handle
    virtual void pass_set_viewport(RenderPassHandle pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t min_depth, uint32_t max_depth) = 0;

    /// @brief 渲染通道绘制 draw call
    /// @param pass 渲染通道handle
    /// @param indices_length 索引数量
    virtual void pass_draw_indexed(RenderPassHandle pass, uint64_t indices_length) = 0;

    /// @brief 一个渲染通道结束，之后可能结束指令录制也可能开始另一个渲染通道
    /// @param pass 渲染通道handle
    virtual void pass_end(RenderPassHandle pass) = 0;

    /// @brief 结束指令录制拿到编码后的command buffer
    /// @param encoder 指令编码器
    /// @return CommandBuffer句柄
    virtual CommandBufferHandle encoder_finish(CommandEncoderHandle encoder) = 0;

    // ------------------------- 设备指令队列 --------------------------------------------

    /// @brief 提交录制的指令buffer
    /// @param cmd CommandBuffer handle
    virtual void queue_submit(CommandBufferHandle cmd) = 0;
    virtual void queue_wait_idle() = 0;
    virtual uint64_t submit_frame() = 0;
    virtual void defer_destroy() = 0;

private:
    static std::shared_ptr<RenderingDeviceRefactor> singleton;

public:
    static std::shared_ptr<RenderingDeviceRefactor> get_singleton() { return singleton; }
    static void create_singleton();
    static void create_singleton(RenderingDriverType driver);
    static void destroy_singleton();
    static RenderingDriverType get_default_driver();
};

inline std::shared_ptr<RenderingDeviceRefactor> RenderingDeviceRefactor::singleton;