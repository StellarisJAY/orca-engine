#include "rendering_server.h"
#include "rendering_device_refactor.h"

RID RenderingServer::create_mesh()
{
    // 只分配存储；真正的 GPU surface 在顶点 + 索引都到齐后创建。
    return storage.meshes.create(MeshData{});
}

void RenderingServer::add_mesh_surface(
    RID mesh,
    RID material,
    const std::vector<Vector3> &vertices,
    const std::vector<Vector3> &normals,
    const std::vector<Vector2> &uvs,
    const std::vector<Vector3> &tangents,
    const std::vector<Vector3> &bitangents,
    const std::vector<uint32_t> &indices)
{
    const auto rendering_device = RenderingDeviceRefactor::get_singleton();
    MeshData *data = storage.meshes.get_data(mesh);
    if (data == nullptr)
        return;
    // 输入数据校验
    const auto count_vert = vertices.size();
    if(count_vert == 0)
    {
        throw std::exception("invalid mesh surface: empty vertices");
    }
    if(normals.size() != count_vert || uvs.size() != count_vert || tangents.size() != count_vert || bitangents.size() != count_vert)
    {
        throw std::exception("invalid mesh surface: vertices count mismatch");
    }
    if(indices.empty())
    {
        throw std::exception("invalid mesh surface: empty indices");
    }

    const size_t element_per_vertex = 14;
    const size_t bytes_per_vertex = element_per_vertex * sizeof(real_t); 

    // 创建vertex buffer
    GPUBufferDesc buffer_desc;
    buffer_desc.usage = GPUBufferUsage::VERTEX | GPUBufferUsage::COPY_DST;
    buffer_desc.size = bytes_per_vertex * count_vert;
    const GPUBufferHandle vertex_buffer = rendering_device->create_buffer(buffer_desc);
    
    // 展平vertex buffer
    std::vector<real_t> buffer_data;
    buffer_data.resize(bytes_per_vertex * count_vert);

    for(auto i = 0; i < count_vert; i++)
    {
        const size_t off = i * element_per_vertex;
        // position
        buffer_data[off] = vertices[i].x;
        buffer_data[off+1] = vertices[i].y;
        buffer_data[off+2] = vertices[i].z;
        // normal
        buffer_data[off+3] = normals[i].x;
        buffer_data[off+4] = normals[i].y;
        buffer_data[off+5] = normals[i].z;
        // uv
        buffer_data[off+6]= uvs[i].x;
        buffer_data[off+7]= uvs[i].y;
        // tangent
        buffer_data[off+8]= tangents[i].x;
        buffer_data[off+9]= tangents[i].y;
        buffer_data[off+10]= tangents[i].z;
        // bitangent
        buffer_data[off+11]= bitangents[i].x;
        buffer_data[off+12]= bitangents[i].y;
        buffer_data[off+13]= bitangents[i].z;
    }

    rendering_device->queue_write_buffer(vertex_buffer, 0, buffer_data.data(), bytes_per_vertex * count_vert);
    
    Surface surface;
    surface.material = material;
    surface.index_count = indices.size();
    surface.handle = vertex_buffer;
    data->surfaces.emplace_back(surface);
}