#pragma once

#include "resource.h"
#include "object/rid.h"

class Material: public Resource
{
    RID material;
public:
    RID get_rid() const { return material; }
};