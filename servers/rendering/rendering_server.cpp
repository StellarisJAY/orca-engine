#include "rendering_server.h"
#include "rendering_device.h"
#include "os/memory.h"
#include "shaders/builtin/builtin_shaders.h"

namespace
{
    // set_mesh_vertices / set_mesh_indices 都到齐后，把 CPU 数据一次性提交给 device。
    void commit_mesh_surface(MeshData &data)
    {
        if (!data.has_vertices || !data.has_indices)
            return;

        auto device = RenderingDevice::get_singleton();
        if (device == nullptr)
            return;

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

bool RenderingServer::init()
{
    // 创建builtin shader程序
    const uint64_t shader_program = RenderingDevice::get_singleton()->create_shader_program(builtin_vertex_shader, builtin_fragment_shader);
    if (shader_program == 0)
    {
        std::printf("[ERROR] failed to create default shader program");
        return false;
    }
    default_shader_program = storage.shaders.create(ShaderData{.shader_program = shader_program});

    // 创建默认材质，如果物体没有材质则使用该材质
    default_material = storage.materials.create(MaterialData{
        .albedo = Color(255, 255, 255),
        .metallic = 1.0,
        .roughness = 1.0,
        .specular = 1.0,
        .emission = 0,
    });
    return true;
}

RID RenderingServer::create_scene()
{
    RID rid = storage.scenes.create(SceneData{.main_camera = 0});
    // TODO 暂时规定scene进入scene_tree后就成为current_scene
    current_scene = rid;
    return rid;
}

void RenderingServer::set_scene_env(RID scene, RID env)
{
    storage.scenes.get_data(scene)->env = env;
}

void RenderingServer::set_scene_main_camera(RID scene, RID camera)
{
    storage.scenes.get_data(scene)->main_camera = camera;
}

RID RenderingServer::create_environment()
{
    EnvironmentData data{
        .ambient_color = Color(128, 128, 128, 128),
        .ambient_strength = 1.0f,
    };
    return storage.environments.create(data);
}

void RenderingServer::set_environment_ambient_light(RID env, const Color &color, float strength)
{
    EnvironmentData *data = storage.environments.get_data(env);
    if (data != nullptr)
    {
        data->ambient_color = color;
        data->ambient_strength = strength;
    }
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
    if (data == nullptr)
        return;

    data->vertices = vertices;
    data->normals = normals;
    data->uvs = uvs;
    data->has_vertices = true;
    commit_mesh_surface(*data);
}

void RenderingServer::set_mesh_indices(RID mesh, const std::vector<uint32_t> &indices)
{
    MeshData *data = storage.meshes.get_data(mesh);
    if (data == nullptr)
        return;

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

void RenderingServer::set_instance_material(RID instance, RID material)
{
    storage.instances.get_data(instance)->material = material;
}

RID RenderingServer::create_material()
{
    return storage.materials.create(MaterialData());
}

void RenderingServer::set_material_albedo(RID mat, const Color &color)
{
    storage.materials.get_data(mat)->albedo = color;
}

void RenderingServer::set_material_roughness(RID mat, float roughness)
{
    storage.materials.get_data(mat)->roughness = roughness;
}

void RenderingServer::set_material_metallic(RID mat, float metallic)
{
    storage.materials.get_data(mat)->metallic = metallic;
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

RID RenderingServer::create_light()
{
    return storage.lights.create(LightData{
        .transform = Transform3D(),
        .final_color = Color(1, 1, 1),
        .type = 0,
    });
}

void RenderingServer::set_light_transform(RID light, const Transform3D &transform)
{
    storage.lights.get_data(light)->transform = transform;
}

void RenderingServer::set_light_color(RID light, const Color &color)
{
    LightData *light_data = storage.lights.get_data(light);
    if (light_data)
    {
        // TODO 温度和颜色计算最终颜色
        light_data->final_color = color;
        light_data->base_color = color;
    }
}

void RenderingServer::set_light_type(RID light, int type)
{
    storage.lights.get_data(light)->type = type;
}

void RenderingServer::set_light_scene(RID light, RID scene)
{
    SceneData *scene_data = storage.scenes.get_data(scene);
    if (scene_data)
    {
        for (auto l : scene_data->lights)
        {
            if (l == light)
                return;
        }
        scene_data->lights.push_back(light);
    }
}

void RenderingServer::set_light_cast_shadow(RID light, bool shadow)
{
    storage.lights.get_data(light)->cast_shadow = shadow;
}

void RenderingServer::set_light_temperature(RID light, float temperature)
{
    LightData *light_data = storage.lights.get_data(light);
    if (light_data)
    {
        // TODO 温度和颜色计算最终颜色
        light_data->temperature = temperature;
    }
}

void RenderingServer::set_light_direction(RID light, const Vector3 &direction)
{
    storage.lights.get_data(light)->direction = direction;
}

void RenderingServer::free(RID rid)
{
    switch (RIDUtil::get_type(rid))
    {
    case RIDType::Camera:
        storage.cameras.free(rid);
        break;
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
    case RIDType::Instance:
        storage.instances.free(rid);
        break;
    default:
        break;
    }
}

void RenderingServer::sync()
{
}

void RenderingServer::draw()
{
    RenderingDevice::get_singleton()->clear();
    draw_items.clear();

    // 过滤出可渲染的实例
    for (InstanceData *instance : storage.instances)
    {
        if (!instance->visible)
            continue;
        // TODO 还要过滤scene是否是当前相机的scene
        MeshData *mesh_data = storage.meshes.get_data(instance->mesh);
        if (mesh_data == nullptr)
        {
            std::printf("[WARN] drawing instance with empty mesh data\n");
            continue;
        }
        if (!mesh_data->has_indices || !mesh_data->has_vertices)
            continue;

        // 实例的材质数据，没有材质则用默认材质
        RID material = instance->material == 0 ? default_material : instance->material;
        MaterialData *materail_data = storage.materials.get_data(material);
        draw_items.push_back(DrawItem{
            .transform = instance->transfrom,
            .surface = mesh_data->surface,
            .index_count = mesh_data->index_count,
            .material = materail_data,
        });
    }

    SceneData *scene_data = storage.scenes.get_data(current_scene);
    EnvironmentData *env_data = storage.environments.get_data(scene_data->env);
    CameraData *camera_data = storage.cameras.get_data(scene_data->main_camera);
    ShaderData *shader_data = storage.shaders.get_data(default_shader_program);
    if (shader_data == nullptr)
        return;
    // shader程序切换，现在只有builtin shader
    const uint64_t program = shader_data->shader_program;
    std::shared_ptr<RenderingDevice> rendering_device = RenderingDevice::get_singleton();
    rendering_device->bind_shader_program(program);

    // 相机变换
    real_t camera_transform[16];
    if (camera_data != nullptr)
    {
        camera_data->transform.affine_inverse().get_mat4(camera_transform);
        const real_t *projection = camera_data->projection.data;
        rendering_device->set_uniform_vec3(program, "u_camera_position", camera_data->transform.origin.coords);
        rendering_device->set_uniform_mat4(program, "u_view", camera_transform);
        rendering_device->set_uniform_mat4(program, "u_projection", projection);
    }

    // 环境
    if (env_data != nullptr)
    {
        // 环境光
        real_t ambient_color[4];
        env_data->ambient_color.linear(ambient_color);
        rendering_device->set_uniform_vec4(program, "u_ambient_color", ambient_color);
        rendering_device->set_uniform_float(program, "u_ambient_strength", env_data->ambient_strength);
    }

    // 灯光
    if (scene_data->lights.size() > 0)
    {
        // 暂时只做一个灯光
        // TODO 多光源
        LightData *light_data = storage.lights.get_data(scene_data->lights[0]);
        if (light_data != nullptr && light_data->visible)
        {
            real_t light_color[3];
            light_data->final_color.linear(light_color);
            rendering_device->set_uniform_vec3(program, "u_light_color", light_color);
            rendering_device->set_uniform_vec3(program, "u_light_position", light_data->transform.origin.coords);
            rendering_device->set_uniform_int(program, "u_light_type", light_data->type);
            rendering_device->set_uniform_vec3(program, "u_light_direction", light_data->direction.coords);
        }
    }

    // 渲染实例
    for (DrawItem item : draw_items)
    {
        // instance transform
        real_t transform[16];
        item.transform.get_mat4(transform);
        rendering_device->set_uniform_mat4(program, "u_model", transform);

        // instance material
        MaterialData *material = item.material;
        real_t vec4[4];
        material->albedo.linear(vec4);
        rendering_device->set_uniform_vec4(program, "u_albedo", vec4);
        rendering_device->set_uniform_float(program, "u_metallic", material->metallic);
        rendering_device->set_uniform_float(program, "u_roughness", material->roughness);

        RenderingDevice::get_singleton()->draw_call(item);
    }
}
