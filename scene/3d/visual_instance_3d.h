#pragma once

#include "node3d.h"
#include "object/rid.h"
#include "rendering/rendering_server.h"

class VisualInstance3D : public Node3D
{
    RID instance;
    bool in_tree = false;

public:
    void _notification(int what) override
    {
        switch (what)
        {
        case NOTIFICATION_ENTER_TREE:
            instance = RenderingServer::get_singleton()->create_instance();
            break;
        case NOTIFICATION_EXIT_TREE:
            RenderingServer::get_singleton()->free(instance);
            break;
        case NOTIFICATION_VISIBILITY_CHANGE:
            RenderingServer::get_singleton()->set_instance_visible(instance, is_visible());
            break;
        }
    }
protected:
    const RID get_instance() const { return instance; }
};