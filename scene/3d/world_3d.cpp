#include "world_3d.h"

void World3D::ensure_skybox()
{
    auto skybox = environment->get_skybox();
    auto rendering_server = RenderingServer::get_singleton();
    if (skybox.is_valid())
    {
        if (skybox->get_rid() == 0)
            skybox->set_rid(rendering_server->create_skybox());
        // 合并skybox的多个贴图槽位

        std::vector<uint8_t> pixels;
        for(uint8_t i = 0; i < 6; i++)
        {
            CubeMapSkybox::Slot slot = static_cast<CubeMapSkybox::Slot>(i);
            auto image_texture = skybox->get_slot(slot);
            pixels.insert(pixels.end(), image_texture->get_pixels().begin(), image_texture->get_pixels().end());
        }

        rendering_server->set_skybox_cubemap(
            skybox->get_rid(),
            skybox->get_width(),
            skybox->get_height(),
            skybox->get_channels(),
            pixels,
            true,
            true
        );
    }
}
