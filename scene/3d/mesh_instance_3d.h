#pragma once

#include "visual_instance_3d.h"
#include "object/ref_counted.h"
#include "mesh/mesh.h"
#include "material/material.h"
#include "rendering_server.h"

class MeshInstance3D: public VisualInstance3D
{
    Ref<Mesh> mesh;
    Ref<Material> material;
public:
    MeshInstance3D(Ref<Mesh> mesh) : mesh(mesh), material(Ref<Material>(nullptr)) {}
    MeshInstance3D(Ref<Mesh> mesh, Ref<Material> mat) : mesh(mesh), material(mat) {}
    
    void _notification(int what)
    {
        // 创建渲染instance
        VisualInstance3D::_notification(what);
        switch(what)
        {
        case NOTIFICATION_ENTER_TREE:
            // 资源对应的mesh还没有传递到渲染服务器
            if(!mesh->get_rid()) 
            {
                mesh->set_rid(RenderingServer::get_singleton()->create_mesh());
                // TODO 多个surface的mesh
                const Mesh::Surface surface = mesh->get_surface(0);
                RenderingServer::get_singleton()->set_mesh_vertices(mesh->get_rid(), surface.vertices, surface.normals, surface.uvs);
                RenderingServer::get_singleton()->set_mesh_indices(mesh->get_rid(), surface.indices);
            }
            RenderingServer::get_singleton()->set_instance_mesh(get_instance(), mesh->get_rid());
            break;
        case NOTIFICATION_EXIT_TREE:
            break;    
        }
    }
};