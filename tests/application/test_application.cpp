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
#include <string>

class Player : public Node3D
{
    real_t gravity = -4.9;
    Vector3 velocity = Vector3::ZERO;
    real_t angle = 0.0;

public:
    void process(double delta) override
    {
    }

    void physics_process(double delta) override
    {
        // Vector3 position = get_position();
        // velocity.y += gravity * delta;
        // position += velocity * delta;
        // set_position(position);
        rotate(Vector3::AxisY, 15 * delta);
        // rotate(Vector3(-1,1,0), 180 * delta);
    }
    void enter_tree() override { std::printf("player node enter tree\n"); }
    void exit_tree() override { std::printf("player node exit tree\n"); }
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

    if(normal_res.is_null() || albedo_res.is_null() || roughness_res.is_null())
        throw "can't find textures";

    auto tex_normal = normal_res.cast<ImageTexture>();
    auto tex_albedo = albedo_res.cast<ImageTexture>();
    auto tex_roughness = roughness_res.cast<ImageTexture>();
    auto tex_metallic = metallic_res.cast<ImageTexture>();
    auto tex_height = height_res.cast<ImageTexture>();
    auto tex_ao = ao_res.cast<ImageTexture>();

    auto material = Ref(memnew(StandardMaterial));
    material->set_texture_albedo(tex_albedo);
    material->set_texture_normal(tex_normal);
    material->set_texture_roughness(tex_roughness);
    material->set_texture_metallic(tex_metallic);
    material->set_texture_height(tex_height);
    material->set_texture_ao(tex_ao);
    material->set_uv_scale({2.0, 2.0});
    material->set_parallax_flip(false);

    std::printf("material loaded: %s\n", texture_group.c_str());
    return material;
}

Node3D *build_main_scene(Application *app)
{
    Ref<Resource> sphere_res = ResourceLoader::get_singleton()->load("res://sphere.obj");
    if (sphere_res.is_null())
        throw "can't find sphere mesh resource";
    Ref<Mesh> sphere_mesh = sphere_res.cast<Mesh>();
    std::printf("meshed loaded\n");

    World3D *world = memnew(World3D);
    app->set_main_scene(world);
    world->set_ambient(Color(128, 128, 128), 1.0);

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

    Light3D *sun = memnew(Light3D);
    sun->set_direction(Vector3(-1, -1, -1));
    world->add_child(sun);
    return world;
}

int main()
{
    Application app;
    ApplicationInitDesc desc;
    desc.fullscreen = false;
    desc.screen_w = 800;
    desc.screen_h = 600;
    if (!app.init(desc))
        return -1;

    Node *world = build_main_scene(&app);

    Camera3D *main_camera = memnew(Camera3D);
    world->add_child(main_camera);
    main_camera->set_perspective(60.0, (float)desc.screen_w / (float)desc.screen_h, 0.1, 100);
    main_camera->set_position({0, 0, 4});
    main_camera->set_main_camera();

    auto p = main_camera->get_global_transform().origin;
    std::printf("camera at: (%lf,%lf,%lf)\n", p.x, p.y, p.z);

    app.run();
    app.shutdown();
    return 0;
}
