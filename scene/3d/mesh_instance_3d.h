#pragma once

#include "visual_instance_3d.h"
#include "object/ref_counted.h"
#include "mesh/mesh.h"
#include "material/material.h"
#include "rendering_server.h"

class MeshInstance3D : public VisualInstance3D
{
    Ref<Mesh> mesh;

    Ref<StandardMaterial> override_material;

private:
    void ensure_surfaces();
    void ensure_material(Ref<StandardMaterial> material);
    void ensure_texture(Ref<StandardMaterial> material);

public:
    MeshInstance3D(Ref<Mesh> mesh) : mesh(mesh) {}
    MeshInstance3D(Ref<Mesh> mesh, Ref<StandardMaterial> override_mat) : mesh(mesh), override_material(override_mat) {}

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
            if (mesh->get_rid() == 0)
            {
                mesh->set_rid(rendering_server->create_mesh());
            }
            // 如果MeshInstance有override材质，设置instance的材质，表面没有材质时回退到instance override 材质
            if(override_material.is_valid())
            {
                ensure_material(override_material);
                rendering_server->set_instance_override_material(instance, override_material->get_rid());
            }
            // 保证instance的所有表面都已传递到渲染端
            ensure_surfaces();
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