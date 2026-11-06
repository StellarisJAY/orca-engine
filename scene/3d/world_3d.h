#pragma once

#include "node3d.h"
#include "object/rid.h"
#include "rendering_server.h"
#include "math/color.h"
#include "environment/environment_3d.h"

class World3D : public Node3D
{
    RID scene = 0;
    Ref<Environment3D> environment;

public:
    World3D(const Ref<Environment3D> &env) : environment(env) {}

    void _notification(int what) override
    {
        Node3D::_notification(what);
        auto rendering_server = RenderingServer::get_singleton();
        switch (what)
        {
        case NOTIFICATION_ENTER_TREE:
            scene = rendering_server->create_scene();
            environment->set_rid(rendering_server->create_environment());
            rendering_server->set_scene_env(scene, environment->get_rid());
            rendering_server->set_environment_ambient_light(environment->get_rid(), environment->get_ambiemt_color(), environment->get_ambient_strength());
            ensure_skybox();
            break;
        case NOTIFICATION_EXIT_TREE:
            if (environment->get_skybox().is_valid())
                rendering_server->free(environment->get_skybox()->get_rid());
            rendering_server->free(environment->get_rid());
            environment = 0;
            rendering_server->free(scene);
            scene = 0;
            break;
        }
    }

    void set_main_camera(RID camera)
    {
        RenderingServer::get_singleton()->set_scene_main_camera(scene, camera);
    }

    RID get_scene() const { return scene; }

private:
    void ensure_skybox();
};