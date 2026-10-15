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
    RenderingServer::get_singleton()->draw();
}
