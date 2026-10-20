#include "node3d.h"
#include "3d/world_3d.h"

Transform3D Node3D::get_global_transform() const
{
    return global_transform;
}

Transform3D Node3D::get_parent_global_transform() const
{
    Node *parent = get_parent();
    while(parent != nullptr)
    {
        if(auto parent_node_3d = static_cast<Node3D*>(parent)) return parent_node_3d->get_global_transform();
        parent = parent->get_parent();
    }
    return Transform3D();
}

Vector3 Node3D::get_position() const
{
    return local_transform.origin;
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
    local_transform.origin = position;
    global_transform = get_parent_global_transform() * local_transform;
    _propagate_transform();
    _notification(NOTIFICATION_TRANSFORM_CHANGE);
}

void Node3D::set_scale(const Vector3 &scale)
{
    local_transform.scale(scale);
    global_transform = get_parent_global_transform() * local_transform;
    _propagate_transform();
    _notification(NOTIFICATION_TRANSFORM_CHANGE);
}

void Node3D::_propagate_transform()
{
    for(Node *child : get_children())
    {
        if(Node3D *child_node = static_cast<Node3D*>(child))
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
    while(parent != nullptr)
    {
        if(auto world = static_cast<World3D*>(parent)) return world;
        parent = parent->get_parent();
    }
    return nullptr;
}
