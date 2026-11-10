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
        return false;
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
    return RenderingDriverType::OpenGL;
}

void RenderingDevice::create_singleton(RenderingDriverType driver)
{
    // 旧RenderingDevice暂时只支持OpenGL
    singleton = std::make_shared<GLRenderingDevice>();
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
