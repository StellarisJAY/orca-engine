#pragma once
#include "node.h"
#include "math/vector3.h"
#include "math/transform_3d.h"
#include "math/math_defs.h"

class World3D;

class Node3D: public Node
{
private:
    EulerOrder euler_rotation_order = EulerOrder::YXZ;
    Vector3 rotation = Vector3::ZERO;
    Vector3 scale = Vector3::ONES;
    Transform3D global_transform;
    Transform3D local_transform;
    bool visible;
public:
    Node3D() {}
    Transform3D get_global_transform() const;
    Transform3D get_parent_global_transform() const;
    Vector3 get_position() const;
    Vector3 get_rotation() const;
    Vector3 get_scale() const;
    bool is_visible() const;

    void set_position(const Vector3 &position);
    void set_scale(const Vector3 &scale);
    void _propagate_transform();
    World3D *get_world() const;
};