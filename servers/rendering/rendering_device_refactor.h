#pragma once

#include <cstdint>
#include <string>
#include <functional>

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

struct ShaderHandle
{
};

struct RenderPipelineHandle
{
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

/// @brief 顶点着色阶段
struct VertexStage
{
    /// @brief shader程序
    ShaderHandle module;
    /// @brief 顶点着色器入口函数名
    std::string entry_point;
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
    bool init(const RenderingWindowInfo &info);

    /// @brief 初始化逻辑设备(物理设备集合->唯一逻辑设备)
    /// @return 成功?
    bool init_device();

    /// @brief 初始化交换链
    /// @return 成功?
    bool init_swapchain();

    void shutdown();

    // ------------------------- Shader ---------------------------------------

    /// @brief 创建shader
    /// @param desc shader描述+代码
    /// @return shader句柄
    virtual ShaderHandle create_shader_module(const ShaderModuleDesc &desc) = 0;
    /// @brief 销毁shader
    /// @param handle
    virtual void destroy_shader_module(ShaderHandle handle) = 0;

    // ------------------------- 绑定组 ------------------------------------

    /// @brief 创建绑定组
    /// @return
    virtual BindGroupLayoutHandle create_bind_group_layout() = 0;

    // ------------------------- 渲染目标 ----------------------------------------

    // ------------------------- 渲染管线 ----------------------------------------

    /// @brief 创建渲染管线
    /// @brief 渲染管线负责管理buffer的layout，使用什么shader程序，输出到什么格式
    /// @brief 渲染管现在绘制前创建，绘制时绑定管线
    /// @return 句柄
    virtual RenderPipelineHandle create_render_pipeline(const RenderPipelineDesc &desc) = 0;

    // ------------------------- command & render pass --------------------------

    /// @brief 创建指令编码器
    /// @return 句柄
    virtual CommandEncoderHandle create_command_encoder();

    /// @brief 开启渲染通道指令录制
    /// @brief 一个渲染流程 = 多个渲染通道的指令录制
    /// @brief eg: 深度贴图pass->PBR pass->后处理pass
    /// @param encoder 编码器handle
    /// @return render pass句柄
    virtual RenderPassHandle encoder_begin_render_pass(CommandEncoderHandle encoder);

    /// @brief 设置渲染通道的渲染管线
    /// @param pass 通道handle
    /// @param pipeline 管线handle
    virtual void pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline);

    virtual void pass_set_bind_group(RenderPassHandle pass);

    virtual void pass_set_vertex_buffer(RenderPassHandle pass);

    virtual void pass_set_index_buffer(RenderPassHandle pass);

    virtual void pass_set_viewport(RenderPassHandle pass);

    virtual void pass_draw_indexed(RenderPassHandle pass);

    /// @brief 渲染通道结束
    /// @param pass 渲染通道handle
    virtual void pass_end(RenderPassHandle pass);

    /// @brief 结束指令录制拿到编码后的command buffer
    /// @param encoder 指令编码器
    /// @return CommandBuffer句柄
    virtual CommandBufferHandle encoder_finish(CommandEncoderHandle encoder);

    // ------------------------- 设备指令队列 --------------------------------------------

    /// @brief 提交录制的指令buffer
    /// @param cmd CommandBuffer handle
    virtual void queue_submit(CommandBufferHandle cmd);
    virtual void queue_wait_idle();
    virtual uint64_t submit_frame();
    virtual void defer_destroy();
};