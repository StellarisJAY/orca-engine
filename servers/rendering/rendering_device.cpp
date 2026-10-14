#include "rendering_device.h"

#include <cstdio>

#if defined(ORCA_RENDERER_VULKAN)
#include "vulkan/vk_rendering_device.h"
#endif
#if defined(ORCA_RENDERER_OPENGL)
#include "opengl/gl_rendering_device.h"
#endif

bool RenderingDevice::is_driver_supported(RenderingDriverType driver)
{
    switch (driver)
    {
    case RenderingDriverType::Vulkan:
#if defined(ORCA_RENDERER_VULKAN)
        return true;
#else
        return false;
#endif
    case RenderingDriverType::OpenGL:
#if defined(ORCA_RENDERER_OPENGL)
        return true;
#else
        return false;
#endif
    }
    return false;
}

RenderingDriverType RenderingDevice::get_default_driver()
{
#if defined(ORCA_RENDERER_DEFAULT_OPENGL)
    if (is_driver_supported(RenderingDriverType::OpenGL))
        return RenderingDriverType::OpenGL;
#endif
#if defined(ORCA_RENDERER_DEFAULT_VULKAN)
    if (is_driver_supported(RenderingDriverType::Vulkan))
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

void RenderingDevice::create_singleton(RenderingDriverType driver)
{
    switch (driver)
    {
    case RenderingDriverType::Vulkan:
#if defined(ORCA_RENDERER_VULKAN)
        singleton = std::make_shared<VkRenderingDevice>();
        return;
#else
        break;
#endif
    case RenderingDriverType::OpenGL:
#if defined(ORCA_RENDERER_OPENGL)
        singleton = std::make_shared<GLRenderingDevice>();
        return;
#else
        break;
#endif
    }
    std::fprintf(stderr, "[ERROR] rendering driver is not available in this build: %d\n", int(driver));
}

void RenderingDevice::create_singleton()
{
    create_singleton(get_default_driver());
}

void RenderingDevice::destroy_singleton()
{
    singleton.reset();
}
