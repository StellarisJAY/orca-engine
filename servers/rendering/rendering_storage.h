#pragma once

#include "object/rid.h"
#include "math/transform_3d.h"
#include "math/projection.h"
#include "math/vector2.h"
#include "math/vector3.h"
#include "math/color.h"
#include <cstdint>
#include <vector>

struct SceneData
{
    RID main_camera = 0;
    RID env = 0;
    std::vector<RID> lights;
};

struct EnvironmentData
{
    Color ambient_color = {255, 255, 255, 255};
    float ambient_strength = 1.0;
};

struct MeshData
{
    // CPU 暂存：RenderingServer 的 set_mesh_vertices / set_mesh_indices 是分开调用的，
    // 两者都到齐后再一次性提交给 RenderingDevice 创建 surface（VAO 必须原子构建）。
    std::vector<Vector3> vertices;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<Vector3> tangents;
    std::vector<Vector3> bitangents;
    
    std::vector<uint32_t> indices;
    bool has_vertices = false;
    bool has_indices = false;

    // GPU 侧资源（device 创建并持有）。
    uint32_t surface = 0; // device surface 句柄（OpenGL: VAO）
    uint32_t index_count = 0;
};

struct MaterialData
{
    Color albedo;
    float metallic = 0;
    float roughness = 0.5;
    float specular = 1.0;
    Vector2 uv_scale = {1.0, 1.0};
    float height_scale = 1.0;
    float normal_scale = 1.0;
    float emission = 0.0;

    RID texture_albedo = 0;
    RID texture_normal = 0;
    RID texture_roughness = 0;
    RID texture_metallic = 0;
    RID texture_ao = 0;
    RID texture_height = 0;
};

struct TextureData
{
    uint32_t texture_2d = 0; // device层句柄
    uint32_t width = 0;
    uint32_t height = 0;
    bool srgb = true;
    bool repeat = true;
    bool linear_filter = true;
    bool mipmap = true;
};

struct CameraData
{
    Transform3D transform;
    Projection projection;
};

struct InstanceData
{
    Transform3D transfrom;
    RID mesh = 0;
    RID material = 0;
    RID scene = 0;
    bool visible = false;
};

struct LightData
{
    Transform3D transform;
    Vector3 direction = Vector3::ZERO;
    Color final_color;
    int type;
    Color base_color;
    float temperature;
    bool cast_shadow = false;
    bool visible = true;
};


struct ShaderData
{
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
    RIDStorage<EnvironmentData> environments{RIDType::Environment};
    RIDStorage<MaterialData> materials{RIDType::Material};
    RIDStorage<LightData> lights{RIDType::Light};
    RIDStorage<TextureData> textures{RIDType::Texture};
};
