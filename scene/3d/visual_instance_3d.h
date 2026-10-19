#pragma once

#include "node3d.h"
#include "object/rid.h"
#include "rendering_server.h"

class VisualInstance3D : public Node3D
{
    RID instance;
    bool in_tree = false;

public:
    void _notification(int what) override
    {
        Node3D::_notification(what);
        switch (what)
        {
        case NOTIFICATION_ENTER_TREE:
            in_tree = true;
            instance = RenderingServer::get_singleton()->create_instance();
            RenderingServer::get_singleton()->set_instance_visible(instance, in_tree);
            RenderingServer::get_singleton()->set_instance_transform(instance, get_global_transform());
            // TODO 实例的场景
            break;
        case NOTIFICATION_EXIT_TREE:
            RenderingServer::get_singleton()->free(instance);
            break;
        case NOTIFICATION_VISIBILITY_CHANGE:
            RenderingServer::get_singleton()->set_instance_visible(instance, is_visible());
            break;
        case NOTIFICATION_TRANSFORM_CHANGE:
            RenderingServer::get_singleton()->set_instance_transform(instance, get_global_transform());
            break;
        }
    }
protected:
    const RID get_instance() const { return instance; }
};