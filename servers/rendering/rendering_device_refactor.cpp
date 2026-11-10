#include "rendering_device_refactor.h"
#if defined(ORCA_RENDERER_VULKAN)
#include "vulkan/vk_rendering_device_refactor.h"
#endif
#if defined(ORCA_RENDERER_OPENGL)
#include "opengl/gl_rendering_device_refactor.h"
#endif

void RenderingDeviceRefactor::create_singleton()
{
    create_singleton(get_default_driver());
}

void RenderingDeviceRefactor::create_singleton(RenderingDriverType driver)
{
    switch (driver)
    {
    case RenderingDriverType::Vulkan:
#if defined(ORCA_RENDERER_VULKAN)
        singleton = std::make_shared<VkRenderingDeviceRefactor>();
        return;
#else
        break;
#endif
    case RenderingDriverType::OpenGL:
#if defined(ORCA_RENDERER_OPENGL)
        singleton = std::make_shared<GLRenderingDeviceRefactor>();
        return;
#else
        break;
#endif
    }
    std::fprintf(stderr, "[ERROR] rendering driver is not available in this build: %d\n", int(driver));
}

void RenderingDeviceRefactor::destroy_singleton()
{
    singleton.reset();
}

RenderingDriverType RenderingDeviceRefactor::get_default_driver()
{
#if defined(ORCA_RENDERER_DEFAULT_OPENGL)
    return RenderingDriverType::OpenGL;
#endif
#if defined(ORCA_RENDERER_DEFAULT_VULKAN)
    return RenderingDriverType::Vulkan;
#endif
    // 请求的默认后端在本次构建中不可用时，回退到任意可用后端。
#if defined(ORCA_RENDERER_VULKAN)
    return RenderingDriverType::Vulkan;
#elif defined(ORCA_RENDERER_OPENGL)
    return RenderingDriverType::OpenGL;
#else
    return RenderingDriverType::Vulkan; // 不会到达：构建要求至少启用一个后端
#endif
}