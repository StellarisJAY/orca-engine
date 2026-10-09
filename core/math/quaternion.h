#pragma once

#include "math_defs.h"

struct Quaternion
{
    union
    {
        struct {
            real_t x;
            real_t y;
            real_t z;
            real_t w;
        };

        real_t components[4] = {0, 0, 0, 0};
    };

};