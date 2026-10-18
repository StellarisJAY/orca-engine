#pragma once

#include "rendering_storage.h"
#include "math/transform_3d.h"
#include "math/projection.h"
#include "math/vector2.h"
#include "math/vector3.h"

class RenderingServer
{
private:
    static RenderingServer *singleton;
    RenderingStorage storage;
public:
    static void create_singleton();
    static void destroy_singletone();
    static RenderingServer *get_singleton() { return singleton; }

    void init();

    // 场景资源

    RID create_scene();
    void set_scene_env(RID scene, RID env);

    // 网格体资源
    
    RID create_mesh();
    void set_mesh_vertices(
        RID mesh, 
        const std::vector<Vector3> &vertices, 
        const std::vector<Vector3> &normals, 
        const std::vector<Vector2> &uvs);
    void set_mesh_indices(RID mesh, const std::vector<uint32_t> &indices);

    // 渲染实例资源
    
    RID create_instance();
    void set_instance_mesh(RID instance, RID mesh);
    void set_instance_transform(RID instance, const Transform3D &transform);
    void set_instance_scene(RID instance, RID scene);
    void set_instance_visible(RID instance, bool visible);

    // 相机资源
    
    RID create_camera();
    void set_camera_transform(RID camera, const Transform3D &transform);
    void set_camera_perspective(RID camera, real_t fovy, real_t aspect, real_t near, real_t far);
    void set_camera_orthogonal(RID camera, real_t left, real_t right, real_t bottom, real_t top, real_t near, real_t far);

    void free(RID rid);
    void sync(); // 同步指令
    void draw(); // 渲染指令
};

inline RenderingServer *RenderingServer::singleton;