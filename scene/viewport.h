#pragma once

#include "node.h"
#include "object/rid.h"
#include "3d/world_3d.h"

class Viewport: public Node
{
    RID viewport;
public:
    void init();
    World3D *find_world_3d() const;
    void render() const;
};