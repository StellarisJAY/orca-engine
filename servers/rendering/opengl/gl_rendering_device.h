#pragma once

#include "rendering_device.h"
#include <unordered_map>

struct GLProgram
{
    uint64_t program;
    uint64_t vert_shader;
    uint64_t frag_shader;
    std::unordered_map<std::string, uint64_t> uniform_locations;

    int64_t get_uniform_location(const std::string &name);
};

// OpenGL 图形后端。
//
// OpenGL 与 Vulkan 的差异很大（没有 instance/逻辑设备/交换链等概念，上下文随窗口创建、
// 呈现由平台层 swap_buffers 完成），但三阶段生命周期仍然可以一一映射：
//   init()          加载入口点并校验平台已经创建好的 GL 上下文
//   init_device()   创建默认资源（当前可空实现）
//   init_swapchain()配置默认帧缓冲/视口
class GLRenderingDevice : public RenderingDevice
{
    RenderingWindowInfo window_info;
    void *context = nullptr;

    // GL 下 surface 句柄就是 VAO；VBO/EBO 需要另外记录才能释放。
    struct GLMeshSurface
    {
        uint32_t vertex_buffer = 0;
        uint32_t index_buffer = 0;
    };
    std::unordered_map<uint32_t, GLMeshSurface> mesh_surfaces;
    std::unordered_map<uint64_t, GLProgram*> shader_programs;
    uint32_t last_bound_vao = 0;
public:
    RenderingDriverType get_driver_type() const override { return RenderingDriverType::OpenGL; }

    bool init(const RenderingWindowInfo &window) override;
    bool init_device() override;
    bool init_swapchain() override;
    void shutdown() override;

    void *get_handle() const override { return context; }
    void *get_device_handle() const override { return nullptr; }

    uint32_t create_mesh_surface(
        const std::vector<Vector3> &vertices,
        const std::vector<Vector3> &normals,
        const std::vector<Vector2> &uvs,
        const std::vector<Vector3> &tangents,
        const std::vector<Vector3> &bitangents,
        const std::vector<uint32_t> &indices) override;

    void destroy_mesh_surface(uint32_t surface) override;

    void draw_call(const DrawItem &item) override;
    void clear() override;

    uint64_t create_shader_program(const std::string &vertex_src, const std::string &frag_src) override;
    void destroy_shader_program(uint64_t program) override;
    void bind_shader_program(uint64_t program) override;
    void set_uniform_mat4(uint64_t program, const char *uniform, const real_t *data) override;
    void set_uniform_vec4(uint64_t program, const char *uniform, const real_t *data) override;
    void set_uniform_float(uint64_t program, const char *uniform, float data) override;
    void set_uniform_vec3(uint64_t program, const char *uniform, const real_t *data) override;
    void set_uniform_int(uint64_t program, const char *uniform, int data) override;
    void set_uniform_vec2(uint64_t program, const char *uniform, const real_t *data) override;
    void set_uniform_bool(uint64_t program, const char *uniform, bool data) override;

    uint32_t create_texture_2d(const TextureDesc &desc, const void *pixels) override;
    void bind_texture_2d(uint32_t unit, uint32_t texture) override;
    void destroy_texture_2d(uint32_t texture) override;
};

