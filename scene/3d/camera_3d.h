#pragma once

#include "node3d.h"
#include "object/rid.h"
#include "rendering_server.h"

class Camera3D : public Node3D
{
    RID rid;
public:
    void _notification(int what) override
    {
        switch (what)
        {
        case NOTIFICATION_ENTER_TREE:
            rid = RenderingServer::get_singleton()->create_camera();
            break;
        case NOTIFICATION_EXIT_TREE:
            RenderingServer::get_singleton()->free(rid);
            break;
        case NOTIFICATION_TRANSFORM_CHANGE:
            RenderingServer::get_singleton()->set_camera_transform(rid, get_global_transform());
            break;
        }
    }
};