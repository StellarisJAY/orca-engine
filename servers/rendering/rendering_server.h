#pragma once

#include "rendering_storage.h"
#include "math/transform_3d.h"
#include "math/projection.h"
#include "math/vector2.h"
#include "math/vector3.h"

struct DrawItem;

enum
{
    TextureSlotAlbedo = 0,
    TextureSlotNormal,
    TextureSlotRoughness,
    TextureSlotMetallic,
    TextureSlotAO,
};

class RenderingServer
{
private:
    static RenderingServer *singleton;
    RenderingStorage storage;
    std::vector<DrawItem> draw_items;

    RID default_shader_program = 0;
    RID default_material = 0;
    RID current_camera = 0;
    RID current_scene = 0;

public:
    static void create_singleton();
    static void destroy_singletone();
    static RenderingServer *get_singleton() { return singleton; }

    bool init();

    // 场景资源

    RID create_scene();
    void set_scene_env(RID scene, RID env);
    void set_scene_main_camera(RID scene, RID camera);

    RID create_environment();
    void set_environment_ambient_light(RID env, const Color &color, float strength);

    // 网格体资源

    RID create_mesh();
    void set_mesh_vertices(
        RID mesh,
        const std::vector<Vector3> &vertices,
        const std::vector<Vector3> &normals,
        const std::vector<Vector2> &uvs,
        const std::vector<Vector3> &tangents,
        const std::vector<Vector3> &bitangents);
    void set_mesh_indices(RID mesh, const std::vector<uint32_t> &indices);

    // 渲染实例资源

    RID create_instance();
    void set_instance_mesh(RID instance, RID mesh);
    void set_instance_transform(RID instance, const Transform3D &transform);
    void set_instance_scene(RID instance, RID scene);
    void set_instance_visible(RID instance, bool visible);
    void set_instance_material(RID instance, RID material);

    // 材质资源

    RID create_material();
    void set_material_albedo(RID mat, const Color &color);
    void set_material_roughness(RID mat, float roughness);
    void set_material_metallic(RID mat, float metallic);
    void set_material_texture(RID mat, uint8_t slot, RID texture);

    // 纹理资源

    RID create_texture();
    void set_texture_image(
        RID texture,
        uint32_t width, uint32_t height,
        uint8_t channels,
        const std::vector<uint8_t> &pixels,
        bool srgb,
        bool repeat,
        bool linear_filter,
        bool gen_mipmap);

    // 相机资源

    RID create_camera();
    void set_camera_transform(RID camera, const Transform3D &transform);
    void set_camera_perspective(RID camera, real_t fovy, real_t aspect, real_t near, real_t far);
    void set_camera_orthogonal(RID camera, real_t left, real_t right, real_t bottom, real_t top, real_t near, real_t far);

    // 灯光资源

    RID create_light();
    void set_light_transform(RID light, const Transform3D &transform);
    void set_light_color(RID light, const Color &color);
    void set_light_type(RID light, int type);
    void set_light_scene(RID light, RID scene);
    void set_light_cast_shadow(RID light, bool shadow);
    void set_light_temperature(RID light, float temperature);
    void set_light_direction(RID light, const Vector3 &direction);

    void free(RID rid);
    void sync(); // 同步指令
    void draw(); // 渲染指令
};

inline RenderingServer *RenderingServer::singleton;