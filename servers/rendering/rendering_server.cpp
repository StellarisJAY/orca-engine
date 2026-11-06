#include "rendering_server.h"
#include "rendering_device.h"
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

void RenderingServer::create_default_textures()
{
    // 默认贴图只需要让 sampler 保持完整，因此使用 1x1 即可。
    // 贴图是否参与计算由材质的 texture_mask 决定，默认值不再承担“中性参数”的语义。
    const std::vector<uint8_t> white_pixels = {255, 255, 255, 255};
    const std::vector<uint8_t> normal_pixels = {128, 128, 255, 255};

    RID white = storage.textures.create(TextureData{});
    set_texture_image(white, 1, 1, 4, white_pixels, false, true, true, false);

    RID normal = storage.textures.create(TextureData{});
    set_texture_image(normal, 1, 1, 4, normal_pixels, false, true, true, false);

    default_texture_albedo = white;
    default_texture_normal = normal;
    default_texture_roughness = white;
    default_texture_metallic = white;
    default_texture_ao = white;
    // 高度图约定白色为最高点；默认使用白色表示“完全平坦”，
    // 保证没有绑定高度图的材质不会被视差偏移。
    default_texture_height = white;
}

bool RenderingServer::create_builtin_skybox_cube()
{
    std::vector<Vector3> vertices;
    std::vector<uint32_t> indices;
    SkyboxCube::create_geometry(vertices, indices);

    // 天空盒立方体只有位置：法线/UV/切线/副切线全部传空，
    // 后端会创建仅含 position 属性的 surface。
    builtin_skybox_cube_surface = RenderingDevice::get_singleton()->create_mesh_surface(
        vertices,
        {},
        {},
        {},
        {},
        indices);

    if (builtin_skybox_cube_surface == 0)
    {
        std::printf("[ERROR] failed to create builtin skybox cube surface\n");
        return false;
    }

    builtin_skybox_cube_index_count = static_cast<uint32_t>(indices.size());
    return true;
}

void RenderingServer::create_depth_map_pass()
{
    auto rendering_device = RenderingDevice::get_singleton();
    depth_map_framebuffer = rendering_device->create_framebuffer();

    const ShaderSource shader_source = ShaderLoader::load("builtin-depth");

    const uint64_t program = rendering_device->create_shader_program(shader_source.vertex, shader_source.fragment);
    buildtin_depth_map_shader_program = storage.shaders.create(ShaderData{program});
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

    // 创建builtin 天空盒shader程序
    const ShaderSource skybox_source = ShaderLoader::load("builtin-skybox");
    if (!skybox_source.is_valid())
    {
        std::printf("[ERROR] failed to load builtin skybox shader source\n");
        return false;
    }
    const uint64_t skybox_shader_program = RenderingDevice::get_singleton()->create_shader_program(skybox_source.vertex, skybox_source.fragment);
    if (skybox_shader_program == 0)
    {
        std::printf("[ERROR] failed to create builtin skybox shader program");
        return false;
    }
    builtin_skybox_shader_program = storage.shaders.create(ShaderData{.shader_program = skybox_shader_program});

    // 天空盒默认立方体，所有天空盒共享
    if (!create_builtin_skybox_cube())
        return false;

    // 创建阴影深度贴图渲染的资源
    create_depth_map_pass();

    // 默认贴图，材质没有贴图时使用
    create_default_textures();

    // 创建默认材质，如果物体没有材质则使用该材质。
    // 默认材质不绑定任何贴图，完全走标量参数；texture_mask = 0。
    default_material = storage.materials.create(MaterialData{
        .albedo = Color(255, 255, 255),
        .metallic = 0.0,
        .roughness = 1.0,
        .specular = 1.0,
        .emission = 0.0,
        .texture_mask = 0});

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
    return storage.skyboxes.create(SkyboxData{
        .cube_map_texture = 0,
        .cube_surface = builtin_skybox_cube_surface,
        .cube_index_count = builtin_skybox_cube_index_count,
    });
}

void RenderingServer::set_skybox_cubemap(RID skybox, uint32_t width, uint32_t height, uint8_t channels, const std::vector<uint8_t> &pixels, bool srgb, bool linear_filter)
{
    if (auto skybox_data = storage.skyboxes.get_data(skybox))
    {
        const uint32_t handle = RenderingDevice::get_singleton()->create_cube_map_texture(TextureDesc{
                                                                                              .width = width,
                                                                                              .height = height,
                                                                                              .channels = channels,
                                                                                              .gen_mipmap = false,
                                                                                              .linear_filter = linear_filter,
                                                                                              .srgb = srgb,
                                                                                          },
                                                                                          pixels.data());
        if (handle == 0)
        {
            std::printf("[ERROR] set_skybox_cubemap: failed to create cube map texture\n");
            return;
        }

        // 重传时销毁旧句柄，避免 GPU 纹理泄漏。
        if (skybox_data->cube_map_texture != 0)
            RenderingDevice::get_singleton()->destroy_cube_map_texture(skybox_data->cube_map_texture);
        skybox_data->cube_map_texture = handle;
    }
}

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
    MeshData *data = storage.meshes.get_data(mesh);
    if (data == nullptr)
        return;
    Surface surface;
    surface.material = material;
    surface.index_count = indices.size();
    surface.surface = RenderingDevice::get_singleton()->create_mesh_surface(
        vertices,
        normals,
        uvs,
        tangents,
        bitangents,
        indices);
    data->surfaces.push_back(surface);
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
    MaterialData *material_data = storage.materials.get_data(mat);
    if (material_data == nullptr || slot > TextureSlotHeight)
        return;

    RID *target = nullptr;
    switch (slot)
    {
    case TextureSlotAlbedo:
        target = &material_data->texture_albedo;
        break;
    case TextureSlotNormal:
        target = &material_data->texture_normal;
        break;
    case TextureSlotRoughness:
        target = &material_data->texture_roughness;
        break;
    case TextureSlotMetallic:
        target = &material_data->texture_metallic;
        break;
    case TextureSlotAO:
        target = &material_data->texture_ao;
        break;
    case TextureSlotHeight:
        target = &material_data->texture_height;
        break;
    default:
        return;
    }

    const uint32_t bit = 1u << slot;

    // texture == 0 表示显式清空该槽位。
    if (texture == 0)
    {
        *target = 0;
        material_data->texture_mask &= ~bit;
        return;
    }

    TextureData *texture_data = storage.textures.get_data(texture);
    if (texture_data == nullptr || texture_data->texture_2d == 0)
    {
        std::printf("[WARN] set_material_texture: invalid or empty texture RID %llu, slot %u cleared\n",
                    static_cast<unsigned long long>(texture), static_cast<unsigned>(slot));
        *target = 0;
        material_data->texture_mask &= ~bit;
        return;
    }

    *target = texture;
    material_data->texture_mask |= bit;
}

void RenderingServer::set_material_scales(RID mat, const Vector2 &uv_scale, float parallax_scale, bool parallax_flip)
{
    if (auto material_data = storage.materials.get_data(mat))
    {
        material_data->uv_scale = uv_scale;
        material_data->parallax_flip = parallax_flip;
        // 视差高度以 UV 为单位，超过 0.1 时一步会跨过多个纹素，
        // 在 shader 里还会再 clamp 一次，这里提前收敛材质数据。
        material_data->parallax_scale = std::clamp(parallax_scale, 0.0f, 0.1f);
    }
}

void RenderingServer::set_material_normal_scale(RID mat, float normal_scale)
{
    if (auto material_data = storage.materials.get_data(mat))
        material_data->normal_scale = normal_scale;
}

void RenderingServer::set_material_ao_strength(RID mat, float ao_strength)
{
    if (auto material_data = storage.materials.get_data(mat))
        material_data->ao_strength = std::clamp(ao_strength, 0.0f, 1.0f);
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
        if (width == 0 || height == 0 || channels == 0 || channels > 4)
        {
            std::printf("[ERROR] set_texture_image: invalid texture size %ux%u, channels=%u\n",
                        width, height, static_cast<unsigned>(channels));
            return;
        }

        const size_t expected_size = static_cast<size_t>(width) * height * channels;
        if (pixels.size() < expected_size)
        {
            std::printf("[ERROR] set_texture_image: pixel buffer too small (%llu < %llu)\n",
                        static_cast<unsigned long long>(pixels.size()),
                        static_cast<unsigned long long>(expected_size));
            return;
        }

        TextureDesc desc{
            .width = width,
            .height = height,
            .channels = channels,
            .gen_mipmap = gen_mipmap,
            .repeat = repeat,
            .linear_filter = linear_filter,
            .srgb = srgb};
        uint32_t handle = RenderingDevice::get_singleton()->create_texture_2d(desc, pixels.data());
        if (handle == 0)
        {
            std::printf("[ERROR] set_texture_image: failed to create GPU texture\n");
            return;
        }

        // 重传时销毁旧句柄，避免 GPU 纹理泄漏。
        if (texture_data->texture_2d != 0)
            RenderingDevice::get_singleton()->destroy_texture_2d(texture_data->texture_2d);

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
    auto rendering_deivce = RenderingDevice::get_singleton();
    if (auto light_data = storage.lights.get_data(light))
    {
        light_data->cast_shadow = shadow;
        if (shadow)
        {
            // 删除可能残留的旧depth_map
            if (light_data->depth_map != 0)
                rendering_deivce->destroy_depth_map(light_data->depth_map);
            // 创建光源绑定的新深度贴图
            light_data->depth_map = rendering_deivce->create_depth_map(1024, 1024);
            // TODO 点光源的cube depth map
        }
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
        light_data->light_space_transform = Transform3D::look_at(light_data->transform.origin, direction, Vector3::UP);
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
        if (MeshData *data = storage.meshes.get_data(rid))
        {
            // 释放该渲染网格在 device 侧创建的所有 surface（VAO/VBO/EBO）。
            for (const Surface &surface : data->surfaces)
            {
                if (surface.surface != 0)
                    RenderingDevice::get_singleton()->destroy_mesh_surface(surface.surface);
            }
            data->surfaces.clear();
        }
        storage.meshes.free(rid);
        break;
    case RIDType::Instance:
        storage.instances.free(rid);
        break;
    case RIDType::Texture:
        if (TextureData *data = storage.textures.get_data(rid))
        {
            if (data->texture_2d != 0)
                RenderingDevice::get_singleton()->destroy_texture_2d(data->texture_2d);
            data->texture_2d = 0;
        }
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
        if (SkyboxData *data = storage.skyboxes.get_data(rid))
        {
            if (data->cube_map_texture != 0)
                RenderingDevice::get_singleton()->destroy_cube_map_texture(data->cube_map_texture);
            // cube_surface 是 RenderingServer 共享的内置立方体，不能在这里释放。
            data->cube_map_texture = 0;
        }
        storage.skyboxes.free(rid);
        break;
    case RIDType::Shader:
        if (ShaderData *data = storage.shaders.get_data(rid))
        {
            if (data->shader_program != 0)
                RenderingDevice::get_singleton()->destroy_shader_program(data->shader_program);
        }
        if (default_shader_program == rid)
            default_shader_program = 0;
        storage.shaders.free(rid);
        break;
    case RIDType::Light:
        // 灯退出场景树后必须从 scene 的灯光列表中移除，
        // 否则它仍会参与绘制，强度参数也就无法真正归零。
        for (SceneData *scene : storage.scenes)
        {
            auto it = std::remove(scene->lights.begin(), scene->lights.end(), rid);
            scene->lights.erase(it, scene->lights.end());
            // 清理灯光的阴影深度贴图
            auto light_data = storage.lights.get_data(rid);
            if (light_data && light_data->depth_map != 0)
            {
                RenderingDevice::get_singleton()->destroy_depth_map(light_data->depth_map);
            }
        }
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
    RenderingDevice::get_singleton()->clear();
    auto rendering_device = RenderingDevice::get_singleton();

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
    auto skybox_data = storage.skyboxes.get_data(env_data->skybox);

    // 相机变换
    real_t camera_transform[16];
    camera_data->transform.affine_inverse().get_mat4(camera_transform);
    real_t *projection = camera_data->projection.data;

    draw_items.clear();

    // 过滤出可渲染的实例
    for (InstanceData *instance : storage.instances)
    {
        if (!instance->visible)
            continue;
        // TODO 渲染surfaces
        MeshData *mesh_data = storage.meshes.get_data(instance->mesh);
        if (mesh_data == nullptr)
        {
            std::printf("[WARN] drawing instance with empty mesh data\n");
            continue;
        }

        for (const Surface &surface : mesh_data->surfaces)
        {
            MaterialData *material_data = storage.materials.get_data(surface.material);
            // 没有绑定材质，用回退材质或默认材质
            if (material_data == nullptr)
            {
                material_data = storage.materials.get_data(instance->override_material == 0 ? default_material : instance->override_material);
            }
            if (material_data == nullptr)
            {
                std::printf("[WARN] drawing surface without a valid material\n");
                continue;
            }
            DrawItem item;
            item.surface = surface.surface;
            item.material = material_data;
            item.transform = instance->transfrom;
            item.index_count = surface.index_count;
            draw_items.push_back(item);
        }
    }

    // pass0 depth map ------------------------------------------------------------------------
    
    // 灯光阴影深度贴图
    float light_intensity = 0.0f;
    if (scene_data->lights.size() > 0)
    {
        // 暂时只做一个灯光
        // TODO 多光源
        LightData *light_data = storage.lights.get_data(scene_data->lights[0]);
        if (light_data != nullptr && light_data->cast_shadow)
        {
            render_pass_depth_map(*light_data);
        }
    }

    // pass1 instances -----------------------------------------------------------------
    render_pass_scene(scene_data, env_data, camera_data);
    // pass2 skybox -------------------------------------------------------------------
    render_pass_skybox(skybox_data, camera_transform, projection);
}

void RenderingServer::bind_textures(MaterialData *material, uint64_t program)
{
    auto rendering_device = RenderingDevice::get_singleton();

    // 无效 RID 或尚未上传成功的纹理回退到默认纹理，避免解引用空指针。
    auto bind_slot = [&](uint32_t unit, RID texture, RID fallback)
    {
        TextureData *texture_data = storage.textures.get_data(texture);
        if (texture_data == nullptr || texture_data->texture_2d == 0)
            texture_data = storage.textures.get_data(fallback);
        rendering_device->bind_texture_2d(
            unit, texture_data != nullptr ? texture_data->texture_2d : 0);
    };

    // 基础色贴图
    bind_slot(0, material->texture_albedo, default_texture_albedo);
    rendering_device->set_uniform_int(program, "u_albedo_texture", 0);

    // 法线贴图
    bind_slot(1, material->texture_normal, default_texture_normal);
    rendering_device->set_uniform_int(program, "u_normal_texture", 1);

    // 粗糙度贴图
    bind_slot(2, material->texture_roughness, default_texture_roughness);
    rendering_device->set_uniform_int(program, "u_roughness_texture", 2);

    // 金属度贴图
    bind_slot(3, material->texture_metallic, default_texture_metallic);
    rendering_device->set_uniform_int(program, "u_metallic_texture", 3);

    // AO贴图
    bind_slot(4, material->texture_ao, default_texture_ao);
    rendering_device->set_uniform_int(program, "u_ao_texture", 4);

    // 高度贴图
    bind_slot(5, material->texture_height, default_texture_height);
    rendering_device->set_uniform_int(program, "u_height_texture", 5);
}

void RenderingServer::render_pass_scene(SceneData *scene_data, EnvironmentData *env_data, CameraData *camera_data)
{
    auto rendering_device = RenderingDevice::get_singleton();

    real_t camera_transform[16];
    camera_data->transform.affine_inverse().get_mat4(camera_transform);
    const real_t *projection = camera_data->projection.data;

    ShaderData *shader_data = storage.shaders.get_data(default_shader_program);
    if (shader_data == nullptr || shader_data->shader_program == 0)
        return;
    // shader程序切换，现在只有builtin shader
    const uint64_t program = shader_data->shader_program;
    rendering_device->bind_shader_program(program);

    rendering_device->set_uniform_vec3(program, "u_camera_position", camera_data->transform.origin.coords);
    rendering_device->set_uniform_mat4(program, "u_view", camera_transform);
    rendering_device->set_uniform_mat4(program, "u_projection", projection);

    // 环境；没有环境资源时使用全黑环境光，避免上一帧的 uniform 残留。
    real_t ambient_color[4] = {0.0, 0.0, 0.0, 1.0};
    float ambient_strength = 0.0f;
    if (env_data != nullptr)
    {
        env_data->ambient_color.linear(ambient_color);
        ambient_strength = env_data->ambient_strength;
    }
    rendering_device->set_uniform_vec4(program, "u_ambient_color", ambient_color);
    rendering_device->set_uniform_float(program, "u_ambient_strength", ambient_strength);

    float light_intensity = 1.0;
    if (scene_data->lights.size() > 0)
    {
        LightData *light_data = storage.lights.get_data(scene_data->lights[0]);
        if (light_data != nullptr && light_data->visible)
        {
            real_t light_color[3];
            light_data->final_color.linear(light_color);
            rendering_device->set_uniform_vec3(program, "u_light_color", light_color);
            rendering_device->set_uniform_int(program, "u_light_type", light_data->type);
            rendering_device->set_uniform_vec3(program, "u_light_direction", light_data->direction.coords);
            light_intensity = std::max(light_data->intensity, 0.0f);
        }
    }

    // 每帧都写，删灯或灯不可见时不能残留上一帧的强度。
    rendering_device->set_uniform_float(program, "u_light_intensity", light_intensity);

    // 渲染实例
    for (const DrawItem &item : draw_items)
    {
        // instance transform
        real_t transform[16];
        item.transform.get_mat4(transform);
        rendering_device->set_uniform_mat4(program, "u_model", transform);

        // instance material
        MaterialData *material = item.material;
        if (material == nullptr)
            continue;
        real_t vec4[4];
        material->albedo.linear(vec4);
        rendering_device->set_uniform_vec4(program, "u_albedo", vec4);
        rendering_device->set_uniform_float(program, "u_metallic", material->metallic);
        rendering_device->set_uniform_float(program, "u_roughness", material->roughness);
        rendering_device->set_uniform_vec2(program, "u_uv_scale", material->uv_scale.coords);
        rendering_device->set_uniform_float(program, "u_parallax_scale", material->parallax_scale);
        rendering_device->set_uniform_bool(program, "u_parallax_flip", material->parallax_flip);
        rendering_device->set_uniform_int(program, "u_texture_mask", static_cast<int>(material->texture_mask));
        rendering_device->set_uniform_float(program, "u_normal_scale", material->normal_scale);
        rendering_device->set_uniform_float(program, "u_ao_strength", material->ao_strength);
        // 绑定纹理贴图
        bind_textures(material, program);

        rendering_device->draw_call(item);
    }
}

void RenderingServer::render_pass_depth_map(const LightData &light)
{
    auto rendering_device = RenderingDevice::get_singleton();
    // 绑定灯光的深度贴图
    rendering_device->bind_framebuffer_depth_map(depth_map_framebuffer, light.depth_map);
    rendering_device->bind_framebuffer(depth_map_framebuffer);

    // 切换到深度贴图shader
    ShaderData *depth_map_shader = storage.shaders.get_data(buildtin_depth_map_shader_program);
    rendering_device->bind_shader_program(depth_map_shader->shader_program);

    // 灯光的视角变换
    real_t light_space_transform[16];
    light.transform.get_mat4(light_space_transform);
    rendering_device->set_uniform_mat4(depth_map_shader->shader_program, "u_light_space_matrix", light_space_transform);

    rendering_device->set_viewport(1024, 1024);
    rendering_device->clear();

    // 渲染场景深度贴图
    for (const DrawItem &item : draw_items)
    {
        real_t model_mat4[16];
        item.transform.get_mat4(model_mat4);
        rendering_device->set_uniform_mat4(depth_map_shader->shader_program, "u_model", model_mat4);
        rendering_device->draw_call(item);
    }
    // 切换回默认framebuffer
    rendering_device->bind_framebuffer(0);
}

void RenderingServer::render_pass_skybox(SkyboxData *skybox_data, real_t *camera_transform, real_t *projection)
{
    auto rendering_device = RenderingDevice::get_singleton();
    if (skybox_data != nullptr && skybox_data->cube_map_texture != 0)
    {
        auto skybox_shader_data = storage.shaders.get_data(builtin_skybox_shader_program);
        rendering_device->bind_shader_program(skybox_shader_data->shader_program);
        rendering_device->set_uniform_mat4(skybox_shader_data->shader_program, "u_view", camera_transform);
        rendering_device->set_uniform_mat4(skybox_shader_data->shader_program, "u_projection", projection);
        // 绑定天空盒纹理
        rendering_device->bind_cube_map_texture(1, skybox_data->cube_map_texture);
        rendering_device->set_uniform_int(skybox_shader_data->shader_program, "u_skybox_texture", 1);

        // 绘制天空盒
        DrawItem draw_skybox{
            .surface = builtin_skybox_cube_surface,
            .index_count = builtin_skybox_cube_index_count};
        rendering_device->draw_call(draw_skybox);
    }
}