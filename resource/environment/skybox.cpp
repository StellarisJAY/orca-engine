#include "skybox.h"

Ref<Resource> CubeMapSkybox::duplicate() const
{
    auto copied = memnew(CubeMapSkybox);
    for(int i = 0; i < 6; i++)
    {
        copied->cubemap_slots[i] = cubemap_slots[i];
    }
    copied->mark_dirty(true);
    return Ref<Resource>(copied);
}

void CubeMapSkybox::set_image_texture(Slot slot, const Ref<ImageTexture> &texture)
{
    if(texture.is_valid())
    {
        cubemap_slots[static_cast<uint8_t>(slot)] = texture;
        if(width == 0 && height == 0)
        {
            width = texture->get_width();
            height = texture->get_height();
        }
        if(channels == 0)
        {
            channels = texture->get_channels();
        }
        mark_dirty(true);
    }
}

const Ref<ImageTexture> &CubeMapSkybox::get_slot(Slot slot) const
{
    return cubemap_slots[static_cast<uint8_t>(slot)];
}
