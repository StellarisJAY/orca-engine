#include "application.h"
#include "3d/node3d.h"
#include "3d/camera_3d.h"
#include "os/memory.h"
#include "io/file_access.h"
#include "resource_loader.h"
#include "mesh/mesh.h"
#include "3d/mesh_instance_3d.h"

class Player: public Node3D
{
    real_t gravity = -9.8;
    Vector3 velocity = Vector3::ZERO;
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
        // const Vector3 tp = get_global_transform().origin;
        // std::printf("player at : (%.3f, %.3f, %.3f)", tp.x, tp.y, tp.z);
    }
    void enter_tree() override { std::printf("player node enter tree\n"); }
    void exit_tree() override { std::printf("player node exit tree\n"); }
};

Node3D *build_main_scene(Application *app)
{
    Ref<Resource> sphere_res = ResourceLoader::get_singleton()->load("res://sphere.obj");
    Ref<Resource> monkey_head_res = ResourceLoader::get_singleton()->load("res://monkey_head.obj");
    
    MeshInstance3D *player_mesh = nullptr;
    MeshInstance3D *enemy_mesh = nullptr;

    if(sphere_res.is_valid())
    {
        Ref<Mesh> sphere_mesh = sphere_res.cast<Mesh>();
        const Mesh::Surface surface = sphere_mesh->get_surface(0);
        std::printf("sphere, vertices: %d, faces: %d\n", surface.get_vertex_count(), surface.get_triangle_count());
        player_mesh = memnew(MeshInstance3D(sphere_mesh));
    }
    if(monkey_head_res.is_valid())
    {
        Ref<Mesh> monkey_head_mesh = monkey_head_res.cast<Mesh>();
        const Mesh::Surface surface = monkey_head_mesh->get_surface(0);
        std::printf("monkey head, vertices: %d, faces: %d\n", surface.get_vertex_count(), surface.get_triangle_count());
        enemy_mesh = memnew(MeshInstance3D(monkey_head_mesh));
    }
    World3D *world = memnew(World3D);
    app->set_main_scene(world);
    
    Player *player = memnew(Player);
    world->add_child(player);;
    player->add_child(player_mesh);
    player->set_position({0, 0, 0});

    Camera3D *main_camera = memnew(Camera3D);
    world->add_child(main_camera);
    main_camera->set_perspective(75.0, 1280.0/720.0, 0.1, 100);
    main_camera->set_position({0, 0, 3});
    main_camera->set_main_camera();
    return world;
}

int main()
{
    Application app;
    ApplicationInitDesc desc;
    desc.fullscreen = false;
    if(!app.init(desc)) return -1;

    Node *world = build_main_scene(&app);
    app.run();
    app.shutdown();
    return 0;
}


