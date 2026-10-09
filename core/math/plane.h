#pragma once

#include "math_defs.h"
#include "vector3.h"

struct Plane
{
    Vector3 normal;
    real_t d = 0;    
};
