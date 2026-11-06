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

struct SkyboxData
{
    /// @brief GPU侧立方体贴图handle
    uint32_t cube_map_texture = 0;
    /// @brief 默认天空盒立方体的 device surface 句柄（所有天空盒共享）
    uint32_t cube_surface = 0;
    /// @brief 默认天空盒立方体的索引数量
    uint32_t cube_index_count = 0;
};

struct EnvironmentData
{
    Color ambient_color = {255, 255, 255, 255};
    float ambient_strength = 1.0;

    RID skybox = 0;
};

struct Surface
{
    // GPU 侧资源（device 创建并持有）。
    uint32_t surface = 0; // device surface 句柄（OpenGL: VAO）
    uint32_t index_count = 0;
    RID material = 0;
};

struct MeshData
{
    std::vector<Surface> surfaces;
};

struct MaterialData
{
    Color albedo;
    float metallic = 0;
    float roughness = 0.5;
    float specular = 1.0;
    Vector2 uv_scale = {1.0, 1.0};
    float normal_scale = 1.0;
    float ao_strength = 1.0;
    float emission = 0.0;
    float parallax_scale = 0.02;
    bool parallax_flip = false;

    // 位掩码记录哪些槽位真正有可用贴图，位序与 TextureSlot 一致。
    uint32_t texture_mask = 0;

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
    RID scene = 0;
    bool visible = false;
    RID override_material = 0;
};

struct LightData
{
    Transform3D transform;
    Vector3 direction = Vector3::ZERO;
    Transform3D light_space_transform;
    Color final_color;
    int type;
    Color base_color;
    float temperature = 6500.0f;
    float intensity = 1.0f;
    bool cast_shadow = false;
    bool visible = true;

    uint32_t depth_map = 0;
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
    RIDStorage<SkyboxData> skyboxes{RIDType::Skybox};
};
