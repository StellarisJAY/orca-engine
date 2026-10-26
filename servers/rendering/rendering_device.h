#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "math/vector3.h"
#include "math/vector2.h"
#include "math/transform_3d.h"
#include "rendering_storage.h"

// 图形后端类型。RenderingDevice 只依赖这个枚举，不依赖任何具体后端 API 的类型。
enum class RenderingDriverType
{
    Vulkan,
    OpenGL,
};

// 平台层向图形后端传递的原生窗口/上下文信息。
//
// 这里所有字段都是后端无关的（void* / uint32_t / 回调），具体含义由平台层与后端约定：
//   Win32: window = HWND, instance = HINSTANCE, context = HGLRC
//   X11:   window = xid,  display  = Display*
// 平台层因此不需要包含任何 Vulkan/OpenGL 头文件，也不需要认识 RenderingDevice 的具体实现；
// 反过来 RenderingDevice 也不需要认识 GLFW/SDL/Win32。
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


/// @brief 绘制的instance数据
struct DrawItem
{
    Transform3D transform;
    uint32_t surface = 0;
    uint32_t index_count = 0;

    MaterialData *material = 0;
};

/// @brief 2D纹理详情
struct TextureDesc
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t channels = 1;
    bool gen_mipmap = true;
    bool repeat = false;
    bool linear_filter = false;
    bool srgb = true;
};

// 渲染后端抽象基类。
//
// 设计原则：基类只描述“所有图形 API 都共有”的生命周期与原语；
// 任何后端专有概念（Vulkan 的 instance layer/extension、surface、swapchain 等）
// 都被封装在各自后端的实现里，通过 init() 收到的 RenderingWindowInfo 与私有成员传递，
// 而不是出现在虚函数签名中。
//
// 生命周期分为三个阶段，OpenGL 与 Vulkan 都能映射：
//   阶段一 init()          Vulkan: instance + 调试层 + surface；OpenGL: 加载入口点、校验上下文
//   阶段二 init_device()   Vulkan: 选择物理设备 + 逻辑设备；OpenGL: 默认资源（可空实现）
//   阶段三 init_swapchain()Vulkan: 交换链 + 图像视图；      OpenGL: 默认帧缓冲/视口
class RenderingDevice
{
    static std::shared_ptr<RenderingDevice> singleton;

public:
    static std::shared_ptr<RenderingDevice> get_singleton() { return singleton; }

    // 使用编译期默认后端创建单例。
    static void create_singleton();
    // 显式指定后端创建单例；若该后端未在本次构建中启用则创建失败。
    static void create_singleton(RenderingDriverType driver);
    static void destroy_singleton();

    // 查询某后端是否在本次构建中启用。
    static bool is_driver_supported(RenderingDriverType driver);
    static RenderingDriverType get_default_driver();

    RenderingDevice() = default;
    virtual ~RenderingDevice() = default;

    virtual RenderingDriverType get_driver_type() const = 0;

    // 阶段一：初始化后端全局上下文。
    virtual bool init(const RenderingWindowInfo &window) = 0;
    // 阶段二：选择/创建逻辑设备。
    virtual bool init_device() = 0;
    // 阶段三：创建呈现目标。
    virtual bool init_swapchain() = 0;
    // 逆序释放本后端持有的全部资源。
    virtual void shutdown() = 0;

    // 后端原生句柄，供上层模块桥接其它图形 API 时使用
    // （Vulkan: instance/device；OpenGL: context/DC）。
    virtual void *get_handle() const = 0;
    virtual void *get_device_handle() const = 0;

    // 网格几何资源
    // ------------------------------------------------------------------
    // 一次调用创建完整的 mesh surface。
    //
    // 之所以不拆成“建 VBO / 建 EBO / 设属性 / 上传”多个接口：OpenGL 的 VAO
    // 要求顶点属性、VBO、EBO 必须在同一时刻按固定顺序绑定设置——属性设置必须
    // 在 VBO 绑定之后，EBO 绑定又属于 VAO 状态。拆开调用无法保证顺序，极易出错。
    // 对 Vulkan 而言，顶点/索引缓冲 + 显存绑定同样应作为一次原子提交。
    //
    // vertices/normals/uvs 按 [p(3), n(3), uv(2)] 交错打包，分量类型为 real_t；
    // indices 为 32 位索引。返回后端无关的 surface 句柄（OpenGL: VAO）。
    virtual uint32_t create_mesh_surface(
        const std::vector<Vector3> &vertices,
        const std::vector<Vector3> &normals,
        const std::vector<Vector2> &uvs,
        const std::vector<Vector3> &tangents,
        const std::vector<Vector3> &bitangents,
        const std::vector<uint32_t> &indices) = 0;

    // 释放 create_mesh_surface 返回的句柄及其内部缓冲。
    virtual void destroy_mesh_surface(uint32_t surface) = 0;

    /// @brief 绘制instance
    /// @param item  instance数据
    virtual void draw_call(const DrawItem &item) = 0;
    
    virtual void clear() = 0;
    // shader program
    virtual uint64_t create_shader_program(const std::string &vertex_src, const std::string &frag_src) = 0;
    virtual void destroy_shader_program(uint64_t program) = 0;
    virtual void bind_shader_program(uint64_t program) = 0;
    
    virtual void set_uniform_mat4(uint64_t program, const char *uniform, const real_t *data) = 0;
    virtual void set_uniform_vec4(uint64_t program, const char *uniform, const real_t *data) = 0;
    virtual void set_uniform_float(uint64_t program, const char *uniform, float data) = 0;
    virtual void set_uniform_vec3(uint64_t program, const char *uniform, const real_t *data) = 0;
    virtual void set_uniform_int(uint64_t program, const char *uniform, int data) = 0;
    virtual void set_uniform_vec2(uint64_t program, const char *uniform, const real_t *data) = 0;
    virtual void set_uniform_bool(uint64_t program, const char *uniform, bool data) = 0;

    // 2D纹理

    virtual uint32_t create_texture_2d(const TextureDesc &desc, const void *pixels) = 0;
    virtual void bind_texture_2d(uint32_t unit, uint32_t texture) = 0;
    virtual void destroy_texture_2d(uint32_t texture) = 0;
};

inline std::shared_ptr<RenderingDevice> RenderingDevice::singleton;
