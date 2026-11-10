#include "gl_rendering_device_refactor.h"
#include "os/memory.h"
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

struct GLShaderModule
{
    GLuint program = 0;
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
};

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
    // OpenGL没有设备概念
    return true;
}

bool GLRenderingDeviceRefactor::init_swapchain()
{
    // OpenGL 的“交换链”就是窗口的默认帧缓冲，由平台层负责呈现。
    // 这里只配置默认帧缓冲状态。
    const GLsizei width = static_cast<GLsizei>(window_info.width);
    const GLsizei height = static_cast<GLsizei>(window_info.height);
    if (width > 0 && height > 0)
        glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    return true;
}

void GLRenderingDeviceRefactor::shutdown()
{
}

bool get_shader_compile_status(const char *label, GLuint shader)
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

bool get_program_link_status(GLuint program)
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
    if (!desc.vertex_source.empty())
    {
        const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        const char *vert_shader_source = desc.vertex_source.c_str();
        glShaderSource(vertex_shader, 1, &vert_shader_source, nullptr);
        glCompileShader(vertex_shader);
        bool compile_ok = get_shader_compile_status("vertex", vertex_shader);
        if(!compile_ok)
            return shader;
        module->vertex_shader = vertex_shader;
        glAttachShader(program, vertex_shader);
    }
    // 创建、编译片段着色器
    if(!desc.fragment_source.empty())
    {
        const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        const char *frag_shader_source = desc.fragment_source.c_str();
        glShaderSource(fragment_shader, 1, &frag_shader_source, nullptr);
        glCompileShader(fragment_shader);
        bool compile_ok = get_shader_compile_status("fragment", fragment_shader);
        if(!compile_ok)
            return shader;
        module->fragment_shader = fragment_shader;
        glAttachShader(program, fragment_shader);
    }

    // 链接着色器程序
    glLinkProgram(program);
    bool link_ok = get_program_link_status(program);
    if(!link_ok)
        return shader;

    module->program = program;
    shader.handle = program;
    shader.data = module;
    return shader;
}

void GLRenderingDeviceRefactor::destroy_shader_module(ShaderHandle handle)
{
    if(handle.is_valid() && handle.data != nullptr)
    {
        GLShaderModule *module = reinterpret_cast<GLShaderModule*>(handle.data);
        if(module->program != 0)
        {
            glDeleteProgram(module->program);
            module->program = 0;
        }
        if(module->vertex_shader != 0)
        {
            glDeleteShader(module->vertex_shader);
            module->vertex_shader = 0;
        }
        if(module->fragment_shader != 0)
        {
            glDeleteShader(module->fragment_shader);
            module->fragment_shader = 0;
        }
        memdelete(module);
        handle.data = nullptr;
        handle.handle = 0;
    }
}

GPUBufferHandle GLRenderingDeviceRefactor::create_buffer(const GPUBufferDesc &desc)
{
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    return GPUBufferHandle();
}
