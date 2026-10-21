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
        rotate(Vector3::AxisY, 180 * delta);
        // rotate(Vector3(-1,1,0), 180 * delta);
    }
    void enter_tree() override { std::printf("player node enter tree\n"); }
    void exit_tree() override { std::printf("player node exit tree\n"); }
};

Node3D *build_main_scene(Application *app)
{
    Ref<Resource> sphere_res = ResourceLoader::get_singleton()->load("res://sphere.obj");
    Ref<Resource> monkey_head_res = ResourceLoader::get_singleton()->load("res://monkey_head.obj");
    if(sphere_res.is_null()) throw "can't find sphere mesh resource";
    if(monkey_head_res.is_null()) throw "can't find monkey head resource";

    // Ref<Resource> normal_res = ResourceLoader::get_singleton()->load("res://normal.png");
    // if(normal_res.is_null()) throw "can't find normal map resource";

    Ref<Mesh> sphere_mesh = sphere_res.cast<Mesh>();
    Ref<Mesh> monkey_head_mesh = monkey_head_res.cast<Mesh>();
    // Ref<ImageTexture> text_normal = normal_res.cast<ImageTexture>();

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

    for(int i = -4; i <= 4; i+=2)
    {
        for(int j = -2; j <= 2; j+=2)
        {
            Ref<StandardMaterial> mat = Ref(memnew(StandardMaterial));
            mat->set_albedo({200, 0, 0});
            mat->set_metallic(j == -2 ? 1.0 : (j == 0 ? 0.5 : 0.0));
            mat->set_roughness((i + 4.0)/10.0 + 0.1);
            Player *p = memnew(Player);
            world->add_child(p);
            p->add_child(memnew(MeshInstance3D(monkey_head_mesh, mat)));
            p->set_position({(float)i, (float)j, 0});
            p->set_scale({0.8, 0.8, 0.8});
        }
    }
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
    desc.screen_w = 2560;
    desc.screen_h = 1440;
    if(!app.init(desc)) return -1;

    Node *world = build_main_scene(&app);
    
    Camera3D *main_camera = memnew(Camera3D);
    world->add_child(main_camera);
    main_camera->set_perspective(60.0, (float)desc.screen_w / (float)desc.screen_h, 0.1, 100);
    main_camera->set_position({0, 0, 5});
    main_camera->set_main_camera();

    auto p = main_camera->get_global_transform().origin;
    std::printf("camera at: (%lf,%lf,%lf)\n", p.x, p.y, p.z);

    app.run();
    app.shutdown();
    return 0;
}


