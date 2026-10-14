#pragma once
#include "node.h"
#include "math/vector3.h"
#include "math/transform_3d.h"

class Node3D: public Node
{
private:
    Vector3 position;
    Vector3 rotation;
    Vector3 scale;
    Transform3D transform;
public:
    Transform3D get_global_transform() const;
    Vector3 get_position() const;
    Vector3 get_rotation() const;
    Vector3 get_scale() const;

    void set_position(const Vector3 &position);
    void set_scale(const Vector3 &scale);
};