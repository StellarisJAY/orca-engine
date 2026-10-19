#pragma once

#include "object/rid.h"
#include "math/transform_3d.h"
#include "math/projection.h"
#include "math/vector2.h"
#include "math/vector3.h"
#include <cstdint>
#include <vector>

struct SceneData {
    RID main_camera = 0;
};

struct MeshData {
    // CPU 暂存：RenderingServer 的 set_mesh_vertices / set_mesh_indices 是分开调用的，
    // 两者都到齐后再一次性提交给 RenderingDevice 创建 surface（VAO 必须原子构建）。
    std::vector<Vector3> vertices;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<uint32_t> indices;
    bool has_vertices = false;
    bool has_indices = false;

    // GPU 侧资源（device 创建并持有）。
    uint32_t surface = 0;      // device surface 句柄（OpenGL: VAO）
    uint32_t index_count = 0;
};

struct CameraData {
    Transform3D transform;
    Projection projection;
};

struct InstanceData {
    Transform3D transfrom;
    RID mesh;
    RID scene;
    bool visible;
};

struct ShaderData {
    uint64_t shader_program; // OpenGL create program
};

class RenderingStorage
{
public:
    RIDStorage<CameraData> cameras{RIDType::Camera};
    RIDStorage<MeshData> meshes{RIDType::Mesh};
    RIDStorage<InstanceData> instances{RIDType::Instance};
    RIDStorage<ShaderData> shaders{RIDType::Shader};
    RIDStorage<SceneData> scenes{RIDType::Scene};
};
