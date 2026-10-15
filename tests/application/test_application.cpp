#include "application.h"
#include "3d/node3d.h"
#include "3d/camera_3d.h"
#include "os/memory.h"

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
        Vector3 position = get_position();
        velocity.y += gravity * delta;
        position += velocity * delta;
        set_position(position);
    }
};

Node3D *build_main_scene()
{
    Camera3D *main_camera = memnew(Camera3D);
    Node3D *world = memnew(Node3D);
    Player *player = memnew(Player);
    Node3D *enemy = memnew(Node3D);
    world->add_child(player);
    world->add_child(enemy);

    return world;
}

int main()
{
    Application app;
    ApplicationInitDesc desc;
    desc.fullscreen = false;
    if(!app.init(desc)) return -1;

    Node *world = build_main_scene();
    app.set_main_scene(world);
    
    app.run();
    app.shutdown();
    return 0;
}


