#include "node3d.h"

Transform3D Node3D::get_global_transform() const
{
    return Transform3D();
}

Vector3 Node3D::get_position() const
{
    return Vector3();
}

Vector3 Node3D::get_rotation() const
{
    return Vector3();
}

Vector3 Node3D::get_scale() const
{
    return Vector3();
}

bool Node3D::is_visible() const
{
    return false;
}

void Node3D::set_position(const Vector3 &position)
{
}

void Node3D::set_scale(const Vector3 &scale)
{
}

void Node3D::_propagate_transform()
{
}
