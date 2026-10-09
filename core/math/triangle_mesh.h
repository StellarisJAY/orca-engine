#pragma once

#include <vector>
#include "vector3.h"

class TriangleMesh
{
public:
    struct Triangle
    {
        Vector3 normal;
        int indices[3];
        int32_t surface_group = 0;
    };
private:
    std::vector<Triangle> triangles;
    std::vector<Vector3> vertices;
};