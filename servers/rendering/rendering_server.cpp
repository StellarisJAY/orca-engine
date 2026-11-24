#include "rendering_server.h"
#include "rendering_uniforms.h"
#include "os/memory.h"
#include "shader_loader.h"
#include "skybox_cube.h"

#include <algorithm>
#include <cstdio>

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
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    default_material = storage.materials.create(MaterialData{});

    // 创建帧全局uniform buffer
    GPUBufferDesc frame_uniforms_desc;
    frame_uniforms_desc.label = "frame_uniforms";
    frame_uniforms_desc.size = sizeof(FrameUniforms);
    frame_uniforms_desc.usage = GPUBufferUsage::COPY_DST | GPUBufferUsage::UNIFORM;
    frame_uniform_buffer = rendering_device->create_buffer(frame_uniforms_desc);

    // 创建物体uniform buffer，dynamic offsets
    GPUBufferDesc objects_uniforms_desc;
    objects_uniforms_desc.label = "objects_uniforms";
    // TODO 最大物体数量
    constexpr uint32_t MAX_OBJECTS = 1024;
    objects_uniforms_desc.size = sizeof(ObjectUniforms) * MAX_OBJECTS;
    objects_uniforms_desc.usage = GPUBufferUsage::COPY_DST | GPUBufferUsage::UNIFORM;
    objects_uniform_buffer = rendering_device->create_buffer(objects_uniforms_desc);

    // bindGroup layout： 帧全局uniform和物体uniforms
    BindGroupLayoutDesc group0_desc;
    group0_desc.label = "frame&objects";
    // binding(0) 帧全局uniforms
    group0_desc.entries.emplace_back(BindGroupLayoutEntry{
        .binding = 0,
        .visibility = ShaderStage::VERTEX | ShaderStage::FRAGMENT,
        .type = BindingType::UniformBuffer,
    });
    // binding(1) 物体uniforms
    group0_desc.entries.emplace_back(BindGroupLayoutEntry{
        .binding = 1,
        .visibility = ShaderStage::VERTEX | ShaderStage::FRAGMENT,
        .type = BindingType::UniformBuffer,
        .name = "object",
        .min_binding_size = 0,
        .has_dynamic_offset = true,
    });
    frame_objects_layout = rendering_device->create_bind_group_layout(group0_desc);

    // bindGroup layout：材质和纹理
    BindGroupLayoutDesc group1_desc;
    group1_desc.label = "material";
    // binding(0) 材质uniforms
    group1_desc.entries.emplace_back(BindGroupLayoutEntry{
        .binding = 0,
        .visibility = ShaderStage::FRAGMENT,
        .type = BindingType::UniformBuffer,
    });
    // binding(1) 材质纹理sampler
    group1_desc.entries.emplace_back(BindGroupLayoutEntry{
        .binding = 1,
        .visibility = ShaderStage::FRAGMENT,
        .type = BindingType::Sampler,
    });
    // binding(2~8) 材质纹理槽
    for (uint32_t slot = 0; slot < 6; slot++)
    {
        group1_desc.entries.emplace_back(BindGroupLayoutEntry{
            .binding = slot + 2,
            .visibility = ShaderStage::FRAGMENT,
            .type = BindingType::Texture2D,
        });
    }
    material_layout = rendering_device->create_bind_group_layout(group1_desc);

    // TODO
    // bindGroup layout：灯光

    // 物体uniform buffer绑定组
    BindGroupDesc frame_obj_bg_desc;
    frame_obj_bg_desc.label = "objects";
    frame_obj_bg_desc.layout = frame_objects_layout;
    frame_obj_bg_desc.entries.emplace_back(BindGroupEntry{
        .binding = 0,
        .resource = BindingResource{
            .type = BindingType::UniformBuffer,
            .buffer = frame_uniform_buffer,
            .size = sizeof(FrameUniforms),
        }
    });
    frame_obj_bg_desc.entries.emplace_back(BindGroupEntry{
        .binding = 1,
        .resource = BindingResource{
            .type = BindingType::UniformBuffer,
            .buffer = objects_uniform_buffer,
            .size = sizeof(ObjectUniforms) * MAX_OBJECTS,
        }
    });
    frame_obj_bind_group = rendering_device->create_bind_group(frame_obj_bg_desc);

    init_pass_scene();

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

void RenderingServer::set_environment_skybox(RID env, RID skybox)
{
    if (auto env_data = storage.environments.get_data(env))
    {
        env_data->skybox = skybox;
    }
}

RID RenderingServer::create_skybox()
{
    // 每个天空盒复用 init() 中创建的默认立方体，只额外持有一张立方体贴图。
    return storage.skyboxes.create(SkyboxData{});
}

void RenderingServer::set_skybox_cubemap(RID skybox, uint32_t width, uint32_t height, uint8_t channels, const std::vector<uint8_t> &pixels, bool srgb, bool linear_filter)
{
    
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

void RenderingServer::set_instance_override_material(RID instance, RID material)
{
    if (auto instance_data = storage.instances.get_data(instance))
    {
        instance_data->override_material = material;
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
        .final_color = Color(255, 255, 255),
        .type = 0,
        .intensity = 1.0f,
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
        light_data->light_space_transform = Transform3D::look_at(direction * light_data->shadow_distance * -1, direction, Vector3::UP);
    }
}

void RenderingServer::set_light_intensity(RID light, float intensity)
{
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->intensity = std::max(0.0f, intensity);
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
        storage.meshes.free(rid);
        break;
    case RIDType::Instance:
        storage.instances.free(rid);
        break;
    case RIDType::Texture:
        storage.textures.free(rid);
        break;
    case RIDType::Material:
        storage.materials.free(rid);
        break;
    case RIDType::Scene:
        if (current_scene == rid)
            current_scene = 0;
        storage.scenes.free(rid);
        break;
    case RIDType::Environment:
        storage.environments.free(rid);
        break;
    case RIDType::Skybox:
        storage.skyboxes.free(rid);
        break;
    case RIDType::Shader:
        storage.shaders.free(rid);
        break;
    case RIDType::Light:
        storage.lights.free(rid);
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
    auto rendering_device = RenderingDeviceRefactor::get_singleton();

    // 数据准备 ---------------------------------------------------------------------------------

    SceneData *scene_data = storage.scenes.get_data(current_scene);
    if (scene_data == nullptr)
    {
        std::printf("[WARN] draw: current scene is missing\n");
        return;
    }
    CameraData *camera_data = storage.cameras.get_data(scene_data->main_camera);
    if (camera_data == nullptr)
    {
        std::printf("[WARN] draw: main camera is missing\n");
        return;
    }
    EnvironmentData *env_data = storage.environments.get_data(scene_data->env);
    if (env_data == nullptr)
    {
        std::printf("[WARN] draw: environment is missing\n");
        return;
    }
    // auto skybox_data = storage.skyboxes.get_data(env_data->skybox);

    // 相机变换
    real_t view[16];
    camera_data->transform.affine_inverse().get_mat4(view);
    real_t *projection = camera_data->projection.data;

    draw_items.clear();

    // 过滤出可渲染的实例
    for (InstanceData *instance : storage.instances)
    {
        if (!instance->visible)
            continue;
        MeshData *mesh_data = storage.meshes.get_data(instance->mesh);
        if (mesh_data == nullptr)
        {
            std::printf("[WARN] drawing instance with empty mesh data\n");
            continue;
        }

        for (const Surface &surface : mesh_data->surfaces)
        {
            // TODO 暂时不实现材质
            // MaterialData *material_data = storage.materials.get_data(surface.material);
            // 没有绑定材质，用回退材质或默认材质
            // if (material_data == nullptr)
            // {
            //     material_data = storage.materials.get_data(instance->override_material == 0 ? default_material : instance->override_material);
            // }
            // if (material_data == nullptr)
            // {
            //     std::printf("[WARN] drawing surface without a valid material\n");
            //     continue;
            // }
            DrawItem item;
            item.surface = surface.surface;
            // item.material = material_data;
            item.transform = instance->transfrom;
            item.index_count = surface.index_count;
            item.vertex_buffer = surface.vertex_buffer;
            item.index_buffer = surface.index_buffer;
            draw_items.push_back(item);
        }
    }

    // 写入frame uniform buffer
    FrameUniforms frame_uniforms{};
    // view变换
    memcpy(frame_uniforms.view, view, sizeof(view));
    // projection变换
    memcpy(frame_uniforms.projection, projection, 16 * sizeof(real_t));
    // 相机世界坐标
    frame_uniforms.camera_position[0] = camera_data->transform.origin.x;
    frame_uniforms.camera_position[1] = camera_data->transform.origin.y;
    frame_uniforms.camera_position[2] = camera_data->transform.origin.z;
    frame_uniforms.camera_position[3] = 1;
    // TODO 环境光颜色和强度
    // real_t ambient_color[3];
    // env_data->ambient_color.normalize_rgb(ambient_color);
    // memcpy(frame_uniforms.ambient_color_strength, ambient_color, 3);
    // frame_uniforms.ambient_color_strength[3] = env_data->ambient_strength;
    // TODO 灯光uniforms

    // 写入frame uniform buffer
    rendering_device->queue_write_buffer(frame_uniform_buffer, 0, &frame_uniforms, sizeof(FrameUniforms));

    // 写入object uniform buffer
    if(!draw_items.empty())
    {
        std::vector<ObjectUniforms> object_buffer;
        object_buffer.resize(draw_items.size());
        uint32_t index = 0;
        for(const auto &item : draw_items)
        {
            real_t obj_transform[16];
            item.transform.get_mat4(obj_transform);
            memcpy(object_buffer[index].model, obj_transform, sizeof(obj_transform));
            index++;
        }
        rendering_device->queue_write_buffer(objects_uniform_buffer, 0, object_buffer.data(), sizeof(ObjectUniforms) * draw_items.size());
    }

    encoder = rendering_device->create_command_encoder();
    render_pass_scene(scene_data, env_data, camera_data);
    const auto command_buffer = rendering_device->encoder_finish(encoder);
    rendering_device->queue_submit(command_buffer);
    rendering_device->present();
}

void RenderingServer::render_pass_depth_map(const LightData &light)
{
    
}

void RenderingServer::render_pass_skybox(SkyboxData *skybox_data, real_t *camera_transform, real_t *projection)
{
    
}