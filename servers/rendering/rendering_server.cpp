#include "rendering_server.h"
#include "os/memory.h"

void RenderingServer::create_singleton()
{
    singleton = memnew(RenderingServer);
}

void RenderingServer::destroy_singletone()
{
    memdelete(singleton);
}

void RenderingServer::init()
{
    
}

RID RenderingServer::create_scene()
{
    return RID();
}

void RenderingServer::set_scene_env(RID scene, RID env)
{
}

RID RenderingServer::create_mesh()
{
    return RID();
}

void RenderingServer::set_mesh_vertices(RID mesh)
{
}

void RenderingServer::set_mesh_indices(RID mesh, const Transform3D &transform)
{
}

RID RenderingServer::create_instance()
{
    return RID();
}

void RenderingServer::set_instance_mesh(RID instance, RID mesh)
{
}

void RenderingServer::set_instance_transform(RID instance, const Transform3D &transform)
{
}

void RenderingServer::set_instance_scene(RID instance, RID scene)
{
}

void RenderingServer::set_instance_visible(RID instance, bool visible)
{
}

RID RenderingServer::create_camera()
{
    return RID();
}

void RenderingServer::set_camera_transform(RID camera, const Transform3D &transform)
{
}

void RenderingServer::set_camera_perspective(RID camera, real_t fovy, real_t, real_t)
{
}

void RenderingServer::set_camera_orthogonal(RID camera, real_t left, real_t right, real_t bottom, real_t top, real_t, real_t)
{
}

void RenderingServer::free(RID rid)
{
}

void RenderingServer::sync()
{
}

void RenderingServer::draw()
{
}
