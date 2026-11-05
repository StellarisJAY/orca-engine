#include "mesh_instance_3d.h"

void MeshInstance3D::upload_if_needed()
{
    if (mesh.is_null())
    {
        return;
    }

    RenderingServer *rendering_server = RenderingServer::get_singleton();

    // RID 只在第一次上传时创建；set_rid 不清除 dirty，surface 是否已上传由 dirty 决定。
    if (mesh->get_rid() == 0)
    {
        mesh->set_rid(rendering_server->create_mesh());
    }

    // 材质/贴图上传是幂等的，每次进树都可以确保一次（共享 Mesh 被多个实例引用时，
    // 不同实例可能注入不同材质）。
    for (const Mesh::Surface &surface : mesh->get_surfaces())
    {
        if (surface.material.is_valid())
        {
            ensure_material(surface.material);
        }
    }

    // surface 只能追加一次：同一个 Mesh 被多个 MeshInstance3D 引用时，
    // 第二个实例不能再次 add_mesh_surface，否则会出现重复面/过绘。
    if (mesh->is_rid_dirty())
    {
        ensure_surfaces();
        mesh->mark_rid_clean();
    }
}

void MeshInstance3D::ensure_surfaces()
{
    RenderingServer *rendering_server = RenderingServer::get_singleton();
    for (const Mesh::Surface &surface : mesh->get_surfaces())
    {
        // 材质已经在 upload_if_needed() 里上传过，这里只取 RID。
        // 如果表面没有材质，则留 0，渲染端回退到 instance override / 默认材质。
        RID material_rid = 0;
        if (surface.material.is_valid())
        {
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
        rendering_server->set_material_normal_scale(material->get_rid(), material->get_normal_scale());
        rendering_server->set_material_ao_strength(material->get_rid(), material->get_ao_strength());
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
        {
            // 贴图缺失或加载失败时清空对应槽位，shader 会回退到标量参数。
            rendering_server->set_material_texture(material->get_rid(), slot_idx, 0);
            continue;
        }
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
