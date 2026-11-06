#include "world_3d.h"
#include <cstdio>
#include <string>
#include <vector>

void World3D::ensure_skybox()
{
    auto skybox = environment->get_skybox();
    if (skybox.is_null())
        return;

    auto rendering_server = RenderingServer::get_singleton();

    // 先把六个面校验并拼成一整块立方体贴图数据；失败则不创建/挂接天空盒。
    std::vector<uint8_t> pixels;
    std::string error;
    if (!skybox->build_cubemap_pixels(pixels, &error))
    {
        std::printf("[ERROR] World3D: invalid skybox: %s\n", error.c_str());
        return;
    }

    if (skybox->get_rid() == 0)
        skybox->set_rid(rendering_server->create_skybox());

    rendering_server->set_skybox_cubemap(
        skybox->get_rid(),
        skybox->get_width(),
        skybox->get_height(),
        skybox->get_channels(),
        pixels,
        true,
        true
    );
    rendering_server->set_environment_skybox(environment->get_rid(), skybox->get_rid());
}
