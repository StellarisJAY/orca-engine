#include "math/math_defs.h"
#include "gl_rendering_device.h"
#include <cstdio>

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

namespace
{
    const std::function<void *(const char *)> *g_get_proc_address = nullptr;

    GLADapiproc glad_load_proc(const char *name)
    {
        if (g_get_proc_address == nullptr || *g_get_proc_address == nullptr)
            return nullptr;
        return reinterpret_cast<GLADapiproc>((*g_get_proc_address)(name));
    }
} // namespace

bool GLRenderingDevice::init(const RenderingWindowInfo &window)
{
    window_info = window;
    context = window.context;
    if (context == nullptr)
    {
        std::printf("[ERROR] OpenGL context is missing: create the window with RenderingDriverType::OpenGL\n");
        return false;
    }

    // 上下文此时已 current（平台层 makeContextCurrent 过），GLAD 会在这里一次性加载全部入口点
    g_get_proc_address = &window_info.get_proc_address;
    const int gl_version = gladLoadGL(glad_load_proc);
    g_get_proc_address = nullptr;

    if (gl_version == 0)
    {
        std::printf("[ERROR] failed to load OpenGL functions via GLAD\n");
        return false;
    }

    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    std::printf("OpenGL context ready: %s (%s)\n",
                version ? reinterpret_cast<const char *>(version) : "<unknown>",
                renderer ? reinterpret_cast<const char *>(renderer) : "<unknown>");
    return true;
}

bool GLRenderingDevice::init_device()
{
    // OpenGL 没有单独的“逻辑设备”对象，上下文即设备。这里留给后续创建默认资源。
    return true;
}

bool GLRenderingDevice::init_swapchain()
{
    // OpenGL 的“交换链”就是窗口的默认帧缓冲，由平台层负责呈现。
    // 这里只配置默认帧缓冲状态。
    const GLsizei width = static_cast<GLsizei>(window_info.width);
    const GLsizei height = static_cast<GLsizei>(window_info.height);
    if (width > 0 && height > 0)
        glViewport(0, 0, width, height);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    return true;
}

void GLRenderingDevice::shutdown()
{
    // GL 上下文由平台层（窗口）持有并销毁，这里不释放。
    context = nullptr;
}

uint32_t GLRenderingDevice::create_mesh_surface(
    const std::vector<Vector3> &vertices,
    const std::vector<Vector3> &normals,
    const std::vector<Vector2> &uvs,
    const std::vector<uint32_t> &indices)
{
    const size_t vertex_count = vertices.size();
    if (vertex_count == 0 || normals.size() != vertex_count || uvs.size() != vertex_count)
    {
        std::printf("[ERROR] create_mesh_surface: vertex/normal/uv count mismatch\n");
        return 0;
    }

    // 交错打包 [px,py,pz, nx,ny,nz, u,v]。
    std::vector<real_t> packed;
    packed.reserve(vertex_count * 8);
    for (size_t i = 0; i < vertex_count; i++)
    {
        const Vector3 &p = vertices[i];
        const Vector3 &n = normals[i];
        const Vector2 &uv = uvs[i];
        packed.push_back(p.x); packed.push_back(p.y); packed.push_back(p.z);
        packed.push_back(n.x); packed.push_back(n.y); packed.push_back(n.z);
        packed.push_back(uv.x); packed.push_back(uv.y);
    }

    GLuint vbo = 0;
    GLuint ebo = 0;
    GLuint vao = 0;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glGenVertexArrays(1, &vao);

    glBindVertexArray(vao);

    // 顶点缓冲：属性指针会记录当前绑定的 GL_ARRAY_BUFFER，必须先绑定再设属性。
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(packed.size() * sizeof(real_t)),
                 packed.data(), GL_STATIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(8 * sizeof(real_t));
    const auto offset = [](size_t element) {
        return reinterpret_cast<void *>(element * sizeof(real_t));
    };

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_DOUBLE, GL_FALSE, stride, offset(0)); // position
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_DOUBLE, GL_FALSE, stride, offset(3)); // normal
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_DOUBLE, GL_FALSE, stride, offset(6)); // uv

    // 索引缓冲：EBO 绑定属于 VAO 状态，必须在 VAO 绑定期间设置。
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    mesh_surfaces.emplace(vao, GLMeshSurface{vbo, ebo});
    return vao;
}

void GLRenderingDevice::destroy_mesh_surface(uint32_t surface)
{
    auto it = mesh_surfaces.find(surface);
    if (it == mesh_surfaces.end()) return;

    GLuint vbo = it->second.vertex_buffer;
    GLuint ebo = it->second.index_buffer;
    GLuint vao = surface;
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteVertexArrays(1, &vao);

    mesh_surfaces.erase(it);
}
