#pragma once

#include "vector4.h"

struct Projection
{

    union
    {
        // 透视变换矩阵
        Vector4 columns[4] = {
            {1, 0, 0, 0},
            {0, 1, 0, 0},
            {0, 0, 1, 0},
            {0, 0, 0, 1}};
        real_t data[16];
    };

    // 创建正交投影
    static Projection create_orthogonal(real_t left, real_t right, real_t bottom, real_t top, real_t near, real_t far);
    // 创建视角投影
    static Projection create_perspective(real_t fovy_deg, real_t aspect, real_t near, real_t far);

    real_t get_fovy() const;
    real_t get_near() const;
    real_t get_far() const;
    real_t get_aspect() const;

    bool is_orthogonal() const;
};