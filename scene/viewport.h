#pragma once

#include "node.h"
#include "object/rid.h"
#include "3d/world_3d.h"
#include "rendering_server.h"

class Viewport: public Node
{
    RID rid;
public:
    void init();
    World3D *find_world_3d() const;
    void render() const;

    void _notification(int what) override
    {
        Node::_notification(what);
        switch(what)
        {
        case NOTIFICATION_ENTER_TREE:
            break;
        case NOTIFICATION_EXIT_TREE:
            break;
        }
    }
};