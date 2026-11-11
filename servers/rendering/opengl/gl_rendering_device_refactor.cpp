#include "gl_rendering_device_refactor.h"
#include "os/memory.h"
// glad 的实现（GLAD_GL_IMPLEMENTATION）只在 gl_loader.cpp 中定义一次，
// 这里只取函数声明与 GL 类型。
#include <glad/gl.h>
#include <cstdio>

// 后端资源对象（GLShaderModule / GLBuffer / GLTexture / ...）统一定义在 gl_internal.h，
// 这样回放器与单元测试都能看到它们的字段。

namespace
{
    const std::function<void *(const char *)> *g_get_proc_address = nullptr;

    GLADapiproc glad_load_proc(const char *name)
    {
        if (g_get_proc_address == nullptr || *g_get_proc_address == nullptr)
            return nullptr;
        return reinterpret_cast<GLADapiproc>((*g_get_proc_address)(name));
    }

    /// @brief 按用途选择 GL 缓冲目标。
    /// 注意：一个 GPUBuffer 只能有一个目标，用途位掩码必须只包含一种主用途。
    GLenum gl_buffer_target(uint8_t usage)
    {
        if (usage & GPUBufferUsage::VERTEX)
            return GL_ARRAY_BUFFER;
        if (usage & GPUBufferUsage::INDEX)
            return GL_ELEMENT_ARRAY_BUFFER;
        if (usage & GPUBufferUsage::UNIFORM)
            return GL_UNIFORM_BUFFER;
        return GL_ARRAY_BUFFER;
    }
} // namespace

bool GLRenderingDeviceRefactor::init(const RenderingWindowInfo &info)
{
    window_info = info;
    context = info.context;
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

bool GLRenderingDeviceRefactor::init_device()
{
    // OpenGL 没有"逻辑设备"概念，上下文即设备。
    return true;
}

bool GLRenderingDeviceRefactor::init_swapchain()
{
    // OpenGL 的"交换链"就是窗口的默认帧缓冲，由平台层负责呈现。
    // 这里只配置默认帧缓冲状态；真正的 pass 状态由 render pass 自己设置。
    const GLsizei width = static_cast<GLsizei>(window_info.width);
    const GLsizei height = static_cast<GLsizei>(window_info.height);
    if (width > 0 && height > 0)
        glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    return true;
}

void GLRenderingDeviceRefactor::resize_swapchain(uint32_t width, uint32_t height)
{
    window_info.width = width;
    window_info.height = height;
    // TODO(swapchain): 重建默认帧缓冲的深度缓冲（RHI 要求显式深度附件，
    // OpenGL 的默认帧缓冲自带深度，后续如需与 Vulkan 语义对齐再引入离屏目标）。
}

Format GLRenderingDeviceRefactor::get_swapchain_format() const
{
    // 默认帧缓冲不是 sRGB 格式：shader 末尾手工做 linear_to_srgb 编码，
    // 所以这里的格式必须是非 sRGB 的 RGBA8。
    return Format::RGBA8Unorm;
}

void GLRenderingDeviceRefactor::get_swapchain_size(uint32_t &width, uint32_t &height) const
{
    width = window_info.width;
    height = window_info.height;
}

void GLRenderingDeviceRefactor::present()
{
    // OpenGL 的呈现由平台层 swap_buffers 完成，这里只占位。
}

void GLRenderingDeviceRefactor::shutdown()
{
    // GL 上下文由平台层（窗口）持有并销毁，这里不释放。
}

// ---------------------------------------------------------------------------
// Shader
// ---------------------------------------------------------------------------

static bool get_shader_compile_status(const char *label, GLuint shader)
{
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::printf("[SHADER %s] %s\n", label, log);
        return false;
    }
    return true;
}

static bool get_program_link_status(GLuint program)
{
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::printf("[PROGRAM] %s\n", log);
        return false;
    }
    return true;
}

ShaderHandle GLRenderingDeviceRefactor::create_shader_module(const ShaderModuleDesc &desc)
{
    ShaderHandle shader;
    GLShaderModule *module = memnew(GLShaderModule);

    GLuint program = glCreateProgram();
    // 创建、编译顶点着色器
    if (!desc.source.empty())
    {
        GLenum shader_type;
        switch(desc.type)
        {
        case ShaderType::VERTEX:
            shader_type = GL_VERTEX_SHADER;
            break;
        case ShaderType::FRAGMENT:
            shader_type = GL_FRAGMENT_SHADER;
            break;
        }
        const GLuint gl_shader = glCreateShader(shader_type);
        const char *vert_shader_source = desc.source.c_str();
        glShaderSource(gl_shader, 1, &vert_shader_source, nullptr);
        glCompileShader(gl_shader);
        if (!get_shader_compile_status("vertex", gl_shader))
        {
            memdelete(module);
            return shader;
        }
        module->type = desc.type;
        module->shader = gl_shader;
    }
    shader.handle = program;
    shader.data = module;
    return shader;
}

void GLRenderingDeviceRefactor::destroy_shader_module(ShaderHandle handle)
{
    if (!handle.is_valid() || handle.data == nullptr)
        return;

    GLShaderModule *module = static_cast<GLShaderModule *>(handle.data);
    if (module->shader != 0)
        glDeleteShader(module->shader);
    memdelete(module);
}

// ---------------------------------------------------------------------------
// 缓冲区
// ---------------------------------------------------------------------------

GPUBufferHandle GLRenderingDeviceRefactor::create_buffer(const GPUBufferDesc &desc)
{
    GPUBufferHandle handle;
    if (desc.size == 0)
    {
        std::printf("[ERROR] create_buffer: buffer size is 0\n");
        return handle;
    }

    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    if (buffer == 0)
    {
        std::printf("[ERROR] create_buffer: failed to generate GL buffer\n");
        return handle;
    }

    const GLenum target = gl_buffer_target(desc.usage);
    glBindBuffer(target, buffer);
    glBufferData(target, static_cast<GLsizeiptr>(desc.size), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(target, 0);

    GLBuffer *buffer_data = memnew(GLBuffer);
    buffer_data->buffer = buffer;
    buffer_data->size = desc.size;
    buffer_data->usage = desc.usage;

    handle.handle = buffer;
    handle.data = buffer_data;
    return handle;
}

void GLRenderingDeviceRefactor::queue_write_buffer(GPUBufferHandle handle, uint64_t offset, const void *data, uint64_t size)
{
    if (!handle.is_valid() || handle.data == nullptr || data == nullptr || size == 0)
        return;

    GLBuffer *buffer_data = static_cast<GLBuffer *>(handle.data);
    if (offset + size > buffer_data->size)
    {
        std::printf("[ERROR] queue_write_buffer: write range [%llu, %llu) exceeds buffer size %llu\n",
                    static_cast<unsigned long long>(offset),
                    static_cast<unsigned long long>(offset + size),
                    static_cast<unsigned long long>(buffer_data->size));
        return;
    }

    // TODO(command): RHI 语义要求写入与 queue_submit 有序，等命令录制层落地后
    // 这里应改成"记录一条 WriteBuffer 命令"，由 queue_submit 回放时执行。
    const GLenum target = gl_buffer_target(buffer_data->usage);
    glBindBuffer(target, buffer_data->buffer);
    glBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
    glBindBuffer(target, 0);
}

void GLRenderingDeviceRefactor::destroy_buffer(GPUBufferHandle handle)
{
    if (!handle.is_valid())
        return;
    if (handle.data != nullptr)
    {
        GLBuffer *buffer_data = static_cast<GLBuffer *>(handle.data);
        if (buffer_data->buffer != 0)
            glDeleteBuffers(1, &buffer_data->buffer);
        memdelete(buffer_data);
    }
}

// ---------------------------------------------------------------------------
// 指令队列（录制相关实现在 gl_command.cpp，回放在 gl_executor.cpp）
// ---------------------------------------------------------------------------

void GLRenderingDeviceRefactor::queue_wait_idle()
{
    glFinish();
}

uint64_t GLRenderingDeviceRefactor::submit_frame()
{
    return ++frame_index;
}
