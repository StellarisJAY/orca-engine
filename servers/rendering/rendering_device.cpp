#include "rendering_device.h"

#include <cstdio>

#if defined(ORCA_RENDERER_OPENGL)
#include "opengl/gl_rendering_device.h"
#endif

bool RenderingDevice::is_driver_supported(RenderingDriverType driver)
{
    switch (driver)
    {
    case RenderingDriverType::Vulkan:
        // 旧 RenderingDevice 接口没有 Vulkan 实现；Vulkan 走 RenderingDeviceRefactor。
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
    // 旧设备只支持 OpenGL。OpenGL 未编入本次构建时返回 Vulkan，让平台层按
    // Vulkan 创建窗口；真正创建设备会失败并提示改用 RenderingDeviceRefactor。
#if defined(ORCA_RENDERER_OPENGL)
    return RenderingDriverType::OpenGL;
#else
    return RenderingDriverType::Vulkan;
#endif
}

void RenderingDevice::create_singleton(RenderingDriverType driver)
{
    switch (driver)
    {
    case RenderingDriverType::OpenGL:
#if defined(ORCA_RENDERER_OPENGL)
        singleton = std::make_shared<GLRenderingDevice>();
        return;
#else
        std::fprintf(stderr,
                     "[ERROR] OpenGL backend is not available in this build; "
                     "use RenderingDeviceRefactor for Vulkan\n");
        return;
#endif
    case RenderingDriverType::Vulkan:
        // 旧接口没有 Vulkan 实现：不要静默创建 OpenGL 设备，否则窗口与设备会不匹配。
        std::fprintf(stderr,
                     "[ERROR] legacy RenderingDevice has no Vulkan backend; "
                     "use RenderingDeviceRefactor instead\n");
        return;
    }
}

void RenderingDevice::create_singleton()
{
    create_singleton(get_default_driver());
}

void RenderingDevice::destroy_singleton()
{
    singleton.reset();
}
