#include "rendering_server.h"
#include "rendering_device.h"
#include "os/memory.h"

namespace
{
// set_mesh_vertices / set_mesh_indices 都到齐后，把 CPU 数据一次性提交给 device。
void commit_mesh_surface(MeshData &data)
{
    if (!data.has_vertices || !data.has_indices) return;

    auto device = RenderingDevice::get_singleton();
    if (device == nullptr) return;

    // 重新提交时先释放旧 surface。
    if (data.surface != 0)
    {
        device->destroy_mesh_surface(data.surface);
        data.surface = 0;
    }

    data.surface = device->create_mesh_surface(data.vertices, data.normals, data.uvs, data.indices);
    data.index_count = static_cast<uint32_t>(data.indices.size());
}
} // namespace

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
    // 只分配存储；真正的 GPU surface 在顶点 + 索引都到齐后创建。
    return storage.meshes.create(MeshData{});
}

void RenderingServer::set_mesh_vertices(
        RID mesh, 
        const std::vector<Vector3> &vertices, 
        const std::vector<Vector3> &normals, 
        const std::vector<Vector2> &uvs)
{
    MeshData *data = storage.meshes.get_data(mesh);
    if (data == nullptr) return;

    data->vertices = vertices;
    data->normals = normals;
    data->uvs = uvs;
    data->has_vertices = true;
    commit_mesh_surface(*data);
}

void RenderingServer::set_mesh_indices(RID mesh, const std::vector<uint32_t> &indices)
{
    MeshData *data = storage.meshes.get_data(mesh);
    if (data == nullptr) return;

    data->indices = indices;
    data->has_indices = true;
    commit_mesh_surface(*data);
}

RID RenderingServer::create_instance()
{
    InstanceData data;
    return storage.instances.create(data);
}

void RenderingServer::set_instance_mesh(RID instance, RID mesh)
{
    storage.instances.get_data(instance)->mesh = mesh;
}

void RenderingServer::set_instance_transform(RID instance, const Transform3D &transform)
{
    storage.instances.get_data(instance)->transfrom = transform;
}

void RenderingServer::set_instance_scene(RID instance, RID scene)
{
    storage.instances.get_data(instance)->scene = scene;
}

void RenderingServer::set_instance_visible(RID instance, bool visible)
{
    storage.instances.get_data(instance)->visible = visible;
}

RID RenderingServer::create_camera()
{
    CameraData data;
    return storage.cameras.create(data);
}

void RenderingServer::set_camera_transform(RID camera, const Transform3D &transform)
{
    storage.cameras.get_data(camera)->transform = transform;
}

void RenderingServer::set_camera_perspective(RID camera, real_t fovy, real_t aspect, real_t near, real_t far)
{
    storage.cameras.get_data(camera)->projection = Projection::create_perspective(fovy, aspect, near, far);
}

void RenderingServer::set_camera_orthogonal(RID camera, real_t left, real_t right, real_t bottom, real_t top, real_t near, real_t far)
{
    storage.cameras.get_data(camera)->projection = Projection::create_orthogonal(left, right, bottom, top, near, far);
}

void RenderingServer::free(RID rid)
{
    switch(RIDUtil::get_type(rid))
    {
    case RIDType::Camera: storage.cameras.free(rid); break;
    case RIDType::Mesh:
        if (MeshData *data = storage.meshes.get_data(rid))
        {
            if (data->surface != 0)
            {
                if (auto device = RenderingDevice::get_singleton())
                    device->destroy_mesh_surface(data->surface);
            }
        }
        storage.meshes.free(rid);
        break;
    case RIDType::Instance: storage.instances.free(rid); break;
    default: break;
    }
}

void RenderingServer::sync()
{
    
}

void RenderingServer::draw()
{
}
