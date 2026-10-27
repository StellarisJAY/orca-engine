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
    TextureSlotHeight,
};

class RenderingServer
{
private:
    static RenderingServer *singleton;
    RenderingStorage storage;
    std::vector<DrawItem> draw_items;

    RID default_shader_program = 0;

    RID default_texture_albedo = 0;
    RID default_texture_normal = 0;
    RID default_texture_roughness = 0;
    RID default_texture_metallic = 0;
    RID default_texture_ao = 0;
    RID default_texture_height = 0;
    
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
    
    /// @brief 创建渲染端网格体资源
    /// @return RID
    RID create_mesh();
    /// @brief 向网格体增加表面，表面用来区分同一网格体的不同材质的部分
    /// @param mesh RID
    /// @param material RID 
    /// @param vertices 顶点集合
    /// @param normals 顶点法线集合
    /// @param uvs 顶点uv集合
    /// @param tangents 顶点切线集合
    /// @param bitangents 顶点副切线集合
    /// @param indices 面顶点索引
    void add_mesh_surface(
        RID mesh, 
        RID material,
        const std::vector<Vector3> &vertices, 
        const std::vector<Vector3> &normals, 
        const std::vector<Vector2> &uvs, 
        const std::vector<Vector3> &tangents, 
        const std::vector<Vector3> &bitangents, 
        const std::vector<uint32_t> &indices);

    // 渲染实例资源
    
    /// @brief 创建渲染实例，一个实例下一个网格体，实例用来维护物体的transform、可见性和所属场景
    /// @return RID
    RID create_instance();
    /// @brief 绑定网格体与实例
    /// @param instance 实例RID
    /// @param mesh 网格体资源RID
    void set_instance_mesh(RID instance, RID mesh);
    /// @brief 设置实例的transform
    /// @param instance RID
    /// @param transform global transform
    void set_instance_transform(RID instance, const Transform3D &transform);
    /// @brief 设置实例所属场景
    /// @param instance RID
    /// @param scene 场景RID
    void set_instance_scene(RID instance, RID scene);
    /// @brief 设置实例可见性
    /// @param instance RID
    /// @param visible 是否可见
    void set_instance_visible(RID instance, bool visible);
    /// @brief 设置实例的override材质，在mesh没有材质时回退到override材质，如果两者都没有，回退到默认材质
    /// @param instance RID
    /// @param material 材质RID
    void set_instance_override_material(RID instance, RID material);

    // 材质资源
    
    /// @brief 创建材质资源
    /// @return RID
    RID create_material();
    /// @brief 设置基础色
    /// @param mat RID
    /// @param color 颜色
    void set_material_albedo(RID mat, const Color &color);
    /// @brief 设置粗糙度
    /// @param mat RID
    /// @param roughness 粗糙度 0.01~1.0 
    void set_material_roughness(RID mat, float roughness);
    /// @brief 设置金属度
    /// @param mat RID
    /// @param metallic 金属度 0~1 
    void set_material_metallic(RID mat, float metallic);
    /// @brief 设置材质纹理
    /// @param mat RID
    /// @param slot 纹理槽
    /// @param texture 图片纹理资源 RID
    void set_material_texture(RID mat, uint8_t slot, RID texture);
    /// @brief 设置材质的缩放
    /// @param mat RID
    /// @param uv_scale uv缩放 
    /// @param parallax_scale 视差缩放
    /// @param parallax_flip 视差深度/高度翻转
    void set_material_scales(RID mat, const Vector2 &uv_scale, float parallax_scale, bool parallax_flip);

    // 纹理资源
    
    /// @brief 创建图像纹理资源
    /// @return RID
    RID create_texture();
    /// @brief 上传图像数据到图像纹理
    /// @param texture RID
    /// @param width 宽
    /// @param height 高
    /// @param channels 颜色通道数
    /// @param pixels 像素点集合
    /// @param srgb 是否是sRGB
    /// @param repeat 是否重复
    /// @param linear_filter 线性过滤
    /// @param gen_mipmap 生成MIPMAP
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

    /// @brief 创建相机资源
    /// @return RID
    RID create_camera();
    /// @brief 设置相机transform，仅旋转和位移有效
    /// @param camera RID
    /// @param transform global transform
    void set_camera_transform(RID camera, const Transform3D &transform);
    /// @brief 设置相机为视角投影
    /// @param camera RID
    /// @param fovy y轴fov
    /// @param aspect 宽高比
    /// @param  近平面
    /// @param  远平面
    void set_camera_perspective(RID camera, real_t fovy, real_t aspect, real_t near, real_t far);
    /// @brief 设置相机为正交投影
    /// @param camera RID
    /// @param left 左边界
    /// @param right 右边界
    /// @param bottom 下边界
    /// @param top 上边界
    /// @param  近平面
    /// @param  远平面
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

private:
    void bind_textures(MaterialData *material, uint64_t program);
    void create_default_textures();
};

inline RenderingServer *RenderingServer::singleton;