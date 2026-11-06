#include "application.h"
#include "3d/node3d.h"
#include "3d/camera_3d.h"
#include "os/memory.h"
#include "io/file_access.h"
#include "resource_loader.h"
#include "mesh/mesh.h"
#include "3d/mesh_instance_3d.h"
#include "material/material.h"
#include "3d/light_3d.h"
#include "input.h"
#include "input_map.h"
#include <string>
#include "math/math_funcs.h"

class Player : public Node3D
{
    real_t rotation_speed = 15.0;
    real_t max_rotation_speed = 90.0;
    real_t min_rotation_speed = 5.0;
    real_t rotation_acc = 45.0;

public:
    void process(double delta) override
    {
    }

    void physics_process(double delta) override
    {
        auto input = Input::get_singleton();
        rotate(Vector3::AxisY, rotation_speed * delta);
    }
    void enter_tree() override { std::printf("player node enter tree\n"); }
    void exit_tree() override { std::printf("player node exit tree\n"); }
};

class Main : public Node
{
public:
    void process(double delta) override
    {
        if (Input::get_singleton()->is_action_just_pressed("close"))
        {
            std::printf("just pressed\n");
        }
        if (Input::get_singleton()->is_action_just_released("close"))
        {
            std::printf("just released\n");
        }
    }
};

class MainCamera: public Camera3D
{
    real_t speed = 1.0;
public:
    void process(double delta) override
    {
        auto input = Input::get_singleton();
        Vector3 direction = Vector3::ZERO;
        if(input->is_action_pressed("forward"))
        {
            direction.z = -speed;
        }
        if(input->is_action_pressed("backward"))
        {
            direction.z = speed;
        }
        if(input->is_action_pressed("left"))
        {
            direction.x = -speed;
        }
        if(input->is_action_pressed("right"))
        {
            direction.x = speed;
        }
        if(input->is_action_pressed("up"))
        {
            direction.y = speed;
        }
        if(input->is_action_pressed("down"))
        {
            direction.y = -speed;
        }
        set_position(get_position() + direction.normalized() * (real_t)delta);
    }
};

Ref<StandardMaterial> load_material(const std::string &texture_group)
{
    const std::string base = "res://textures/" + texture_group + "/";

    auto normal_res = ResourceLoader::get_singleton()->load(base + "normal.png");
    auto albedo_res = ResourceLoader::get_singleton()->load(base + "albedo.png");
    auto roughness_res = ResourceLoader::get_singleton()->load(base + "roughness.png");
    auto metallic_res = ResourceLoader::get_singleton()->load(base + "metallic.png");
    auto height_res = ResourceLoader::get_singleton()->load(base + "height.png");
    auto ao_res = ResourceLoader::get_singleton()->load(base + "ao.png");

    // 贴图缺失不应中断加载：缺失的槽位保持为空，shader 会回退到标量参数。
    if (normal_res.is_null() || albedo_res.is_null() || roughness_res.is_null())
        std::printf("[WARN] material \"%s\" is missing some textures, fallback to scalar parameters\n",
                    texture_group.c_str());

    auto tex_normal = normal_res.cast<ImageTexture>();
    auto tex_albedo = albedo_res.cast<ImageTexture>();
    auto tex_roughness = roughness_res.cast<ImageTexture>();
    auto tex_metallic = metallic_res.cast<ImageTexture>();
    auto tex_height = height_res.cast<ImageTexture>();
    auto tex_ao = ao_res.cast<ImageTexture>();

    auto material = Ref(memnew(StandardMaterial));
    material->set_uv_scale({2.0, 2.0});
    if (tex_albedo.is_valid())
    {
        tex_albedo->set_srgb(true);
        material->set_texture_albedo(tex_albedo);
    }
    if (tex_normal.is_valid())
    {
        tex_normal->set_srgb(false);
        material->set_texture_normal(tex_normal);
    }
    if (tex_roughness.is_valid())
    {
        tex_roughness->set_srgb(false);
        material->set_texture_roughness(tex_roughness);
    }
    if (tex_metallic.is_valid())
    {
        tex_metallic->set_srgb(false);
        material->set_texture_metallic(tex_metallic);
    }
    if (tex_height.is_valid())
    {
        tex_height->set_srgb(false);
        material->set_texture_height(tex_height);
    }
    if (tex_ao.is_valid())
    {
        tex_ao->set_srgb(false);
        material->set_texture_ao(tex_ao);
    }

    std::printf("material loaded: %s\n", texture_group.c_str());
    return material;
}

Ref<Mesh> load_shield_mesh()
{
    const Ref<Resource> shield_res = ResourceLoader::get_singleton()->load("res://gggg/obj.obj");
    if (shield_res.is_null())
        throw "can't find shield obj";
    return shield_res.cast<Mesh>();
}

Node3D *build_main_scene(Application *app)
{
    Ref<Resource> sphere_res = ResourceLoader::get_singleton()->load("res://sphere.obj");
    if (sphere_res.is_null())
        throw "can't find sphere mesh resource";
    Ref<Mesh> sphere_mesh = sphere_res.cast<Mesh>();
    // Ref<Mesh> shield_mesh = load_shield_mesh();
    std::printf("meshed loaded\n");

    Ref<Environment3D> environment = Ref(memnew(Environment3D));
    environment->set_ambient_color({128, 128, 128}, 1.0);
    World3D *world = memnew(World3D(environment));
    app->set_main_scene(world);

    const char *texture_groups[] = {"rock", "stone", "panel", "copper"};
    const real_t positions_x[] = {-1, -1, 1, 1};
    const real_t positions_y[] = {-1, 1, -1, 1};

    for (int i = 0; i < 4; ++i)
    {
        Ref<StandardMaterial> material = load_material(texture_groups[i]);

        Player *player = memnew(Player);
        world->add_child(player);
        player->set_physics_process(true);
        player->add_child(memnew(MeshInstance3D(sphere_mesh, material)));
        player->set_position({positions_x[i], positions_y[i], 0});
        player->set_scale({0.8, 0.8, 0.8});
    }

    std::printf("materials loaded\n");

    // Player *player = memnew(Player);
    // world->add_child(player);
    // player->set_physics_process(true);
    // player->add_child(memnew(MeshInstance3D(shield_mesh)));
    // player->set_position({0, -1, 0});
    // player->set_scale({3,3,3});

    Light3D *sun = memnew(Light3D);
    sun->set_direction(Vector3(-1, -1, -1));
    sun->set_intensity(10.0f);
    world->add_child(sun);
    return world;
}

int main()
{
    Application app;
    ApplicationInitDesc desc;
    desc.fullscreen = false;
    desc.screen_w = 1920;
    desc.screen_h = 1080;
    if (!app.init(desc))
        return -1;

    Node *world = build_main_scene(&app);

    MainCamera *main_camera = memnew(MainCamera);
    world->add_child(main_camera);
    main_camera->set_perspective(60.0, (float)desc.screen_w / (float)desc.screen_h, 0.1, 100);
    main_camera->set_position({0, 0, 4});
    main_camera->set_main_camera();

    InputMap::get_singleton()->add_action(InputAction{
        .action_code = "close",
        .keys = {KeyboardKeyCode::Escape},
        .mouse_buttons = {MouseButton::ButtonLeft},
    });

    InputMap::get_singleton()->add_action(InputAction{
        .action_code = "forward",
        .keys = {KeyboardKeyCode::W, KeyboardKeyCode::Up},
    });

    InputMap::get_singleton()->add_action(InputAction{
        .action_code = "backward",
        .keys = {KeyboardKeyCode::S, KeyboardKeyCode::Down},
    });

    InputMap::get_singleton()->add_action(InputAction{
        .action_code = "left",
        .keys = {KeyboardKeyCode::A, KeyboardKeyCode::Left},
    });

    InputMap::get_singleton()->add_action(InputAction{
        .action_code = "right",
        .keys = {KeyboardKeyCode::D, KeyboardKeyCode::Right},
    });

    InputMap::get_singleton()->add_action(InputAction{
        .action_code = "up",
        .keys = {KeyboardKeyCode::Space},
    });

    InputMap::get_singleton()->add_action(InputAction{
        .action_code = "down",
        .keys = {KeyboardKeyCode::C},
    });

    world->add_child(memnew(Main));

    app.run();
    app.shutdown();
    return 0;
}
