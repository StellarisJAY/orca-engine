#pragma once

#include "resource.h"
#include "object/rid.h"

class Environment3D: public Resource
{
    RID env;
public:
    RID get_rid() const { return env; }
};