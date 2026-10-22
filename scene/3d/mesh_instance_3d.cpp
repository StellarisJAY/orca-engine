#include "mesh_instance_3d.h"

void MeshInstance3D::ensure_material()
{
    RenderingServer *rendering_server = RenderingServer::get_singleton();
    // 资源对应的material还没传递到渲染服务器
    if (material.is_valid() && !material->get_rid())
    {
        material->set_rid((rendering_server->create_material()));
        rendering_server->set_material_albedo(material->get_rid(), material->get_albedo());
        rendering_server->set_material_metallic(material->get_rid(), material->get_metallic());
        rendering_server->set_material_roughness(material->get_rid(), material->get_roughenss());
    }
    // 没有绑定材质，使用默认材质0
    if (material.is_null())
    {
        rendering_server->set_instance_material(get_instance(), 0);
    }
    else
    {
        ensure_texture();
        rendering_server->set_instance_material(get_instance(), material->get_rid());
    }
}

void MeshInstance3D::ensure_texture()
{
    RenderingServer *rendering_server = RenderingServer::get_singleton();
    uint8_t slot_idx = -1;
    for(const Ref<ImageTexture> &slot : material->texture_slots)
    {
        slot_idx++;
        if(slot.is_null()) continue;
        if(slot->get_rid() == 0)
        {
            slot->set_rid(rendering_server->create_texture());
            slot->mark_dirty(true);
            rendering_server->set_material_texture(material->get_rid(), slot_idx, slot->get_rid());
        }
        if(slot->is_rid_dirty())
        {
            rendering_server->set_texture_image(
                slot->get_rid(),
                slot->get_width(),
                slot->get_height(),
                slot->get_channels(),
                slot->get_pixels(),
                slot->is_srgb(),
                slot->is_repeat(),
                slot->is_linear_filter(),
                slot->is_gen_mipmap()
            );
            slot->mark_dirty(false);
        }
    }
}
