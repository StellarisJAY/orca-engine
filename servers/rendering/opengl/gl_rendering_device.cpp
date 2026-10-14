#include "gl_rendering_device.h"

#include <cstdio>

// 系统/编译器提供的 OpenGL 1.1 头。现代 GL 入口点（shader/VAO 等）以后
// 可以在这里通过 window_info.get_proc_address 动态加载，无需引入 GLAD/GLEW。
#include <GL/gl.h>

bool GLRenderingDevice::init(const RenderingWindowInfo &window)
{
    window_info = window;
    context = window.context;
    if (context == nullptr)
    {
        // 上下文由平台层在创建窗口时一并创建（GLFW: glfwMakeContextCurrent）。
        // 没有上下文说明窗口不是以 OpenGL 后端创建的。
        std::printf("[ERROR] OpenGL context is missing: create the window with RenderingDriverType::OpenGL\n");
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
