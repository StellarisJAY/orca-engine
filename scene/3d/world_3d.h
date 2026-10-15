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
};