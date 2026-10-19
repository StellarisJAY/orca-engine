#pragma once

#include "node3d.h"
#include "object/rid.h"
#include "rendering_server.h"

class World3D: public Node3D
{
    RID scene;
    RID environment;
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
};