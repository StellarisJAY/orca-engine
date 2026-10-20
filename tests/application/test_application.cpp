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

class Player: public Node3D
{
    real_t gravity = -4.9;
    Vector3 velocity = Vector3::ZERO;
public:
    void process(double delta) override
    {
        
    }

    void physics_process(double delta) override
    {
        Vector3 position = get_position();
        velocity.y += gravity * delta;
        position += velocity * delta;
        set_position(position);
    }
    void enter_tree() override { std::printf("player node enter tree\n"); }
    void exit_tree() override { std::printf("player node exit tree\n"); }
};

Node3D *build_main_scene(Application *app)
{
    Ref<Resource> sphere_res = ResourceLoader::get_singleton()->load("res://sphere.obj");
    Ref<Resource> monkey_head_res = ResourceLoader::get_singleton()->load("res://monkey_head.obj");
    if(sphere_res.is_null()) throw "can't find sphere mesh resource";

    Ref<Mesh> sphere_mesh = sphere_res.cast<Mesh>();
    Ref<Mesh> monkey_head_mesh = monkey_head_res.cast<Mesh>();

    World3D *world = memnew(World3D);
    app->set_main_scene(world);
    world->set_ambient(Color(128, 128, 128), 1.0);

    Ref<StandardMaterial> mat_gold = Ref(memnew(StandardMaterial));
    Ref<StandardMaterial> mat_red = Ref(memnew(StandardMaterial));
    Ref<StandardMaterial> mat_silver = Ref(memnew(StandardMaterial));
    mat_gold->set_albedo(Color(255, 255, 0));
    mat_gold->set_metallic(1.0f);
    mat_gold->set_roughness(0.5f);
    mat_red->set_albedo(Color(255, 0, 0));
    mat_red->set_metallic(0.0f);
    mat_red->set_roughness(0.25f);
    mat_silver->set_albedo(Color(255, 255, 255));
    mat_silver->set_metallic(1.0f);
    mat_silver->set_roughness(0.5f);

    Player *player1 = memnew(Player);
    world->add_child(player1);
    player1->set_physics_process(false);
    player1->add_child(memnew(MeshInstance3D(monkey_head_mesh, mat_gold)));
    player1->set_position({3, 0, 0});
    player1->set_scale({0.8,0.8,0.8});

    Player *player2 = memnew(Player);
    world->add_child(player2);
    player2->set_physics_process(false);
    player2->add_child(memnew(MeshInstance3D(monkey_head_mesh, mat_silver)));
    player2->set_position({0, 0, 0});
    player2->set_scale({0.8, 0.8, 0.8});

    Player *player3 = memnew(Player);
    world->add_child(player3);
    player3->set_physics_process(false);
    player3->add_child(memnew(MeshInstance3D(monkey_head_mesh, mat_red)));
    player3->set_position({-3, 0, 0});
    player3->set_scale({0.8,0.8,0.8});

    Light3D *sun = memnew(Light3D);
    sun->set_direction(Vector3(-1,-1,-1));
    world->add_child(sun);
    return world;
}

int main()
{
    Application app;
    ApplicationInitDesc desc;
    desc.fullscreen = false;
    desc.screen_w = 3840;
    desc.screen_h = 2160;
    if(!app.init(desc)) return -1;

    Node *world = build_main_scene(&app);
    
    Camera3D *main_camera = memnew(Camera3D);
    world->add_child(main_camera);
    main_camera->set_perspective(60.0, (float)desc.screen_w / (float)desc.screen_h, 0.1, 100);
    main_camera->set_position({0, 0, 5});
    main_camera->set_main_camera();

    app.run();
    app.shutdown();
    return 0;
}


