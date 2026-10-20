#pragma once

#include "node3d.h"
#include "object/rid.h"
#include "rendering_server.h"
#include "math/color.h"

class World3D: public Node3D
{
    RID scene;
    RID environment;
    Color ambient_color = {0, 0, 0, 255};
    float ambient_strength = 1.0f;
public:
    World3D() { scene = RenderingServer::get_singleton()->create_scene(); }

    void _notification(int what) override
    {
        Node3D::_notification(what);
        auto rendering_server = RenderingServer::get_singleton();
        switch(what)
        {
        case NOTIFICATION_ENTER_TREE:
            scene = rendering_server->create_scene();
            environment = rendering_server->create_environment();
            rendering_server->set_environment_ambient_light(environment, ambient_color, ambient_strength);
            rendering_server->set_scene_env(scene, environment);
            break;
        case NOTIFICATION_EXIT_TREE:
            rendering_server->free(scene);
            scene = 0;
            break;
        }
    }

    void set_main_camera(RID camera) 
    {
        RenderingServer::get_singleton()->set_scene_main_camera(scene, camera);
    }

    void set_ambient(const Color &color, float strength)
    {
        ambient_color = color;
        ambient_strength = strength;
        RenderingServer::get_singleton()->set_environment_ambient_light(environment, ambient_color, ambient_strength);
    }

    RID get_scene() const { return scene; }
};