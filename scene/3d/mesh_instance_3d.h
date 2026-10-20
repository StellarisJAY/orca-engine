#pragma once

#include "visual_instance_3d.h"
#include "object/ref_counted.h"
#include "mesh/mesh.h"
#include "material/material.h"
#include "rendering_server.h"

class MeshInstance3D : public VisualInstance3D
{
    Ref<Mesh> mesh;
    Ref<StandardMaterial> material;

public:
    MeshInstance3D(Ref<Mesh> mesh) : mesh(mesh), material(Ref<StandardMaterial>(nullptr)) {}
    MeshInstance3D(Ref<Mesh> mesh, Ref<StandardMaterial> mat) : mesh(mesh), material(mat) {}

    void _notification(int what)
    {
        // 创建渲染instance
        VisualInstance3D::_notification(what);
        RenderingServer *rendering_server = RenderingServer::get_singleton();
        switch (what)
        {
        case NOTIFICATION_ENTER_TREE:
            instance = RenderingServer::get_singleton()->create_instance();
            RenderingServer::get_singleton()->set_instance_visible(instance, true);
            RenderingServer::get_singleton()->set_instance_transform(instance, get_global_transform());
            // 资源对应的mesh还没有传递到渲染服务器
            if (!mesh->get_rid())
            {
                mesh->set_rid(rendering_server->create_mesh());
                // TODO 多个surface的mesh
                const Mesh::Surface surface = mesh->get_surface(0);
                rendering_server->set_mesh_vertices(mesh->get_rid(), surface.vertices, surface.normals, surface.uvs);
                rendering_server->set_mesh_indices(mesh->get_rid(), surface.indices);
            }

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
                rendering_server->set_instance_material(get_instance(), 0);
            else
                rendering_server->set_instance_material(get_instance(), material->get_rid());

            rendering_server->set_instance_mesh(get_instance(), mesh->get_rid());
            break;
        case NOTIFICATION_EXIT_TREE:
            break;
        case NOTIFICATION_MATERIAL_CHANGE:
            // TODO
            break;
        }
    }
};