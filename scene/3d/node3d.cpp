#include "node3d.h"
#include "3d/world_3d.h"
#include "math/quaternion.h"

Transform3D Node3D::get_global_transform() const
{
    return global_transform;
}

Transform3D Node3D::get_transform() const
{
    return local_transform;
}

Transform3D Node3D::get_parent_global_transform() const
{
    Node *parent = get_parent();
    while (parent != nullptr)
    {
        if (auto parent_node_3d = dynamic_cast<Node3D *>(parent))
            return parent_node_3d->get_global_transform();
        parent = parent->get_parent();
    }
    return Transform3D();
}

Vector3 Node3D::get_position() const
{
    return position;
}

Vector3 Node3D::get_rotation() const
{
    return Vector3(Math::rad_to_deg(rotation.x), Math::rad_to_deg(rotation.y), Math::rad_to_deg(rotation.z));
}

real_t Node3D::get_rotation(Vector3::Axis axis) const
{
    if(axis != Vector3::AxisX && axis != Vector3::AxisY && axis != Vector3::AxisZ) return 0;
    return Math::rad_to_deg(rotation[axis]);
}

Vector3 Node3D::get_scale() const
{
    return scale;
}

bool Node3D::is_visible() const
{
    return visible;
}

void Node3D::set_position(const Vector3 &position)
{
    this->position = position;
    transform();
}

void Node3D::set_scale(const Vector3 &scale)
{
    this->scale = scale;
    transform();
}

void Node3D::set_transform(const Transform3D &p_transform)
{
    local_transform = p_transform;

    // 反解 TRS：glTF 的节点矩阵可能包含任意旋转和负缩放，而 Node3D 的
    // position/rotation/scale 只是便于读写的缓存，get_transform() 才是权威值。
    const Basis &basis = p_transform.basis;
    scale = basis.get_scale();

    // 负缩放只能确定“有一个轴被镜像”，约定把镜像归到 X 轴（与 Godot 一致）。
    if (basis.determinant() < (real_t)0)
    {
        scale.x = -scale.x;
    }

    if (!Math::is_zero_approx(scale.x) &&
        !Math::is_zero_approx(scale.y) &&
        !Math::is_zero_approx(scale.z))
    {
        Basis rotation_basis;
        rotation_basis.columns[0] = basis.columns[0] / scale.x;
        rotation_basis.columns[1] = basis.columns[1] / scale.y;
        rotation_basis.columns[2] = basis.columns[2] / scale.z;
        rotation = Quaternion(rotation_basis).euler_angles(euler_rotation_order);
    }

    position = p_transform.origin;
    _update_global_transform();
}

void Node3D::transform()
{
    // 先缩放
    local_transform = Transform3D();
    local_transform.scale(scale);
    // 旋转
    switch (euler_rotation_order)
    {
    case EulerOrder::XYZ:
        local_transform.rotate(Vector3::RIGHT, rotation[Vector3::AxisX]);
        local_transform.rotate(Vector3::UP, rotation[Vector3::AxisY]);
        local_transform.rotate(Vector3::BACKWARD, rotation[Vector3::AxisZ]);
        break;
    case EulerOrder::YXZ:
        local_transform.rotate(Vector3::UP, rotation[Vector3::AxisY]);
        local_transform.rotate(Vector3::RIGHT, rotation[Vector3::AxisX]);
        local_transform.rotate(Vector3::BACKWARD, rotation[Vector3::AxisZ]);
        break;
    case EulerOrder::ZYX:
        local_transform.rotate(Vector3::BACKWARD, rotation[Vector3::AxisZ]);
        local_transform.rotate(Vector3::UP, rotation[Vector3::AxisY]);
        local_transform.rotate(Vector3::RIGHT, rotation[Vector3::AxisX]);
        break;
    }
    // 平移
    local_transform.translate(position);
    _update_global_transform();
}

void Node3D::_update_global_transform()
{
    // 全局变换
    global_transform = get_parent_global_transform() * local_transform;
    _notification(NOTIFICATION_TRANSFORM_CHANGE);
    // 子节点变换
    _propagate_transform();
}

void Node3D::_propagate_transform()
{
    for (Node *child : get_children())
    {
        if (Node3D *child_node = dynamic_cast<Node3D *>(child))
        {
            child_node->global_transform = global_transform * child_node->local_transform;
            child_node->_notification(NOTIFICATION_TRANSFORM_CHANGE);
            child_node->_propagate_transform();
        }
    }
}

World3D *Node3D::get_world() const
{
    Node *parent = get_parent();
    while (parent != nullptr)
    {
        if (auto world = dynamic_cast<World3D *>(parent))
            return world;
        parent = parent->get_parent();
    }
    return nullptr;
}

void Node3D::rotate(Vector3::Axis axis, real_t deg)
{
    if (axis != Vector3::AxisX && axis != Vector3::AxisY && axis != Vector3::AxisZ)
        return;
    rotation[axis] += Math::deg_to_rad(deg);
    transform();
}

void Node3D::rotate(const Vector3 axis, real_t deg)
{
    if(axis == Vector3::ZERO || deg == 0) return;
    Vector3 normalized = axis.normalized();
    Quaternion quat(normalized, Math::deg_to_rad(deg));
    rotation += quat.euler_angles(euler_rotation_order);
    transform();
}

void Node3D::set_rotation(const Vector3 axis, real_t deg)
{
    if(axis == Vector3::ZERO || deg == 0) return;
    Vector3 normalized = axis.normalized();
    Quaternion quat(normalized, Math::deg_to_rad(deg));
    rotation = quat.euler_angles(euler_rotation_order);
    transform();
}
