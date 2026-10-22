#include "rendering_server.h"
#include "rendering_device.h"
#include "os/memory.h"
#include "shader_loader.h"

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

        data.surface = device->create_mesh_surface(data.vertices, data.normals, data.uvs, data.tangents, data.bitangents, data.indices);
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
    // 从 glsl 文件加载 builtin shader
    const ShaderSource source = ShaderLoader::load("builtin");
    if (!source.is_valid())
    {
        std::printf("[ERROR] failed to load builtin shader source\n");
        return false;
    }

    // 创建builtin shader程序
    const uint64_t shader_program = RenderingDevice::get_singleton()->create_shader_program(source.vertex, source.fragment);
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
    if (auto scene_data = storage.scenes.get_data(scene))
    {
        scene_data->env = env;
    }
}

void RenderingServer::set_scene_main_camera(RID scene, RID camera)
{
    if (auto scene_data = storage.scenes.get_data(scene))
    {
        scene_data->main_camera = camera;
    }
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
    const std::vector<Vector2> &uvs,
    const std::vector<Vector3> &tangents,
    const std::vector<Vector3> &bitangents)
{
    MeshData *data = storage.meshes.get_data(mesh);
    if (data == nullptr)
        return;

    data->vertices = vertices;
    data->normals = normals;
    data->uvs = uvs;
    data->has_vertices = true;
    data->tangents = tangents;
    data->bitangents = bitangents;
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
    if (auto instance_data = storage.instances.get_data(instance))
    {
        instance_data->mesh = mesh;
    }
}

void RenderingServer::set_instance_transform(RID instance, const Transform3D &transform)
{
    if (auto instance_data = storage.instances.get_data(instance))
    {
        instance_data->transfrom = transform;
    }
}

void RenderingServer::set_instance_scene(RID instance, RID scene)
{
    if (auto instance_data = storage.instances.get_data(instance))
    {
        instance_data->scene = scene;
    }
}

void RenderingServer::set_instance_visible(RID instance, bool visible)
{
    if (auto instance_data = storage.instances.get_data(instance))
    {
        instance_data->visible = visible;
    }
}

void RenderingServer::set_instance_material(RID instance, RID material)
{
    if (auto instance_data = storage.instances.get_data(instance))
    {
        instance_data->material = material;
    }
}

RID RenderingServer::create_material()
{
    return storage.materials.create(MaterialData());
}

void RenderingServer::set_material_albedo(RID mat, const Color &color)
{
    if (auto material_data = storage.materials.get_data(mat))
    {
        material_data->albedo = color;
    }
}

void RenderingServer::set_material_roughness(RID mat, float roughness)
{
    if (auto material_data = storage.materials.get_data(mat))
    {
        material_data->roughness = roughness;
    }
}

void RenderingServer::set_material_metallic(RID mat, float metallic)
{
    if (auto material_data = storage.materials.get_data(mat))
    {
        material_data->metallic = metallic;
    }
}

void RenderingServer::set_material_texture(RID mat, uint8_t slot, RID texture)
{
    TextureData *texture_data = storage.textures.get_data(texture);
    if (texture_data == nullptr)
        return;

    if (auto material_data = storage.materials.get_data(mat))
    {
        switch (slot)
        {
        case TextureSlotAlbedo:
            material_data->texture_albedo = texture;
            break;
        case TextureSlotNormal:
            material_data->texture_normal = texture;
            break;
        case TextureSlotRoughness:
            material_data->texture_roughness = texture;
            break;
        case TextureSlotMetallic:
            material_data->texture_metallic = texture;
            break;
        case TextureSlotAO:
            material_data->texture_ao = texture;
            break;
        }
    }
}

RID RenderingServer::create_texture()
{
    return storage.textures.create(TextureData());
}

void RenderingServer::set_texture_image(
    RID texture,
    uint32_t width,
    uint32_t height,
    uint8_t channels,
    const std::vector<uint8_t> &pixels,
    bool srgb,
    bool repeat,
    bool linear_filter,
    bool gen_mipmap)
{
    if (TextureData *texture_data = storage.textures.get_data(texture))
    {
        TextureDesc desc{
            .width = width,
            .height = height,
            .channels = channels,
            .gen_mipmap = gen_mipmap,
            .repeat = repeat,
            .linear_filter = linear_filter,
            .srgb = srgb};
        uint32_t handle = RenderingDevice::get_singleton()->create_texture_2d(desc, pixels.data());
        texture_data->width = width;
        texture_data->height = height;
        texture_data->texture_2d = handle;
        texture_data->srgb = srgb;
        texture_data->mipmap = gen_mipmap;
        texture_data->repeat = repeat;
        texture_data->linear_filter = linear_filter;
    }
}

RID RenderingServer::create_camera()
{
    CameraData data;
    return storage.cameras.create(data);
}

void RenderingServer::set_camera_transform(RID camera, const Transform3D &transform)
{
    if (auto camera_data = storage.cameras.get_data(camera))
    {
        camera_data->transform = transform;
    }
}

void RenderingServer::set_camera_perspective(RID camera, real_t fovy, real_t aspect, real_t near, real_t far)
{
    if (auto camera_data = storage.cameras.get_data(camera))
    {
        camera_data->projection = Projection::create_perspective(fovy, aspect, near, far);
    }
}

void RenderingServer::set_camera_orthogonal(RID camera, real_t left, real_t right, real_t bottom, real_t top, real_t near, real_t far)
{
    if (auto camera_data = storage.cameras.get_data(camera))
    {
        camera_data->projection = Projection::create_orthogonal(left, right, bottom, top, near, far);
    }
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
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->transform = transform;
    }
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
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->type = type;
    }
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
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->cast_shadow = shadow;
    }
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
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->direction = direction;
    }
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

        // 基础色贴图
        TextureData *tex_albedo = storage.textures.get_data(material->texture_albedo);
        if (tex_albedo)
        {
            rendering_device->bind_texture_2d(0, tex_albedo->texture_2d);
            rendering_device->set_uniform_int(program, "u_albedo_texture", 0);
        }
        // 法线贴图
        TextureData *tex_normal = storage.textures.get_data(material->texture_normal);
        if (tex_normal)
        {
            rendering_device->bind_texture_2d(1, tex_normal->texture_2d);
            rendering_device->set_uniform_int(program, "u_normal_texture", 1);
        }
        // 金属度贴图
        TextureData *tex_metallic = storage.textures.get_data(material->texture_metallic);
        if (tex_metallic)
        {
            rendering_device->bind_texture_2d(2, tex_metallic->texture_2d);
            rendering_device->set_uniform_int(program, "u_metallic_texture", 2);
        }
        // 粗糙度贴图
        TextureData *tex_roughness = storage.textures.get_data(material->texture_roughness);
        if (tex_roughness)
        {
            rendering_device->bind_texture_2d(3, tex_roughness->texture_2d);
            rendering_device->set_uniform_int(program, "u_roughness_texture", 3);
        }
        RenderingDevice::get_singleton()->draw_call(item);
    }
}
