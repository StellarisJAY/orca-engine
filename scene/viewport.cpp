#include "viewport.h"
#include "rendering_server.h"

void Viewport::init()
{
}

World3D *Viewport::find_world_3d() const
{
    return nullptr;
}

void Viewport::render() const
{
    // 同步CPU侧资源和渲染侧资源
    RenderingServer::get_singleton()->sync();
    RenderingServer::get_singleton()->draw();
}
