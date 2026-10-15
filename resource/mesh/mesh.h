#pragma once

#include "resource.h"
#include "object/rid.h"

class Mesh: public Resource
{
    RID mesh;
public:
    RID get_rid() const { return mesh; }
};