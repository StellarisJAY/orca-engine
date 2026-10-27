#include "mesh_instance_3d.h"

void MeshInstance3D::ensure_surfaces()
{
    RenderingServer *rendering_server = RenderingServer::get_singleton();
    for (const Mesh::Surface &surface : mesh->get_surfaces())
    {
        // 如果表面没有材质，那么设置为默认材质
        RID material_rid = 0;
        if (surface.material.is_valid())
        {
            // 保证表面的材质传递到Rendering侧
            ensure_material(surface.material);
            material_rid = surface.material->get_rid();
        }
        rendering_server->add_mesh_surface(
            mesh->get_rid(),
            material_rid,
            surface.vertices,
            surface.normals,
            surface.uvs,
            surface.tangents,
            surface.bitangents,
            surface.indices);
    }
}

void MeshInstance3D::ensure_material(Ref<StandardMaterial> material)
{
    RenderingServer *rendering_server = RenderingServer::get_singleton();
    // 资源对应的material还没传递到渲染服务器
    if (material->get_rid() == 0)
    {
        material->set_rid(rendering_server->create_material());
        material->mark_dirty(true);
    }
    if (material->is_dirty())
    {
        rendering_server->set_material_albedo(material->get_rid(), material->get_albedo());
        rendering_server->set_material_roughness(material->get_rid(), material->get_roughenss());
        rendering_server->set_material_metallic(material->get_rid(), material->get_metallic());
        rendering_server->set_material_scales(material->get_rid(), material->get_uv_scale(), material->get_parallax_scale(), material->get_parallax_flip());
    }
    // 保证材质所有贴图传递到GPU侧
    ensure_texture(material);
}

void MeshInstance3D::ensure_texture(Ref<StandardMaterial> material)
{
    RenderingServer *rendering_server = RenderingServer::get_singleton();
    uint8_t slot_idx = -1;
    for (const Ref<ImageTexture> texture : material->get_texture_slots())
    {
        slot_idx++;
        if (texture.is_null())
            continue;
        if (texture->get_rid() == 0)
        {
            texture->set_rid(rendering_server->create_texture());
            texture->mark_dirty(true);
        }
        if (texture->is_rid_dirty())
        {
            rendering_server->set_texture_image(
                texture->get_rid(),
                texture->get_width(),
                texture->get_height(),
                texture->get_channels(),
                texture->get_pixels(),
                texture->is_srgb(),
                texture->is_repeat(),
                texture->is_linear_filter(),
                texture->is_gen_mipmap());
        }
        rendering_server->set_material_texture(material->get_rid(), slot_idx, texture->get_rid());
    }
}
