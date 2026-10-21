#include "node.h"
#include <algorithm>

SceneTree *Node::get_scene_tree() const
{
    return scene_tree;
}

Node *Node::get_child(uint32_t idx) const
{
    if(idx >= children.size()) return nullptr;
    return children[idx];
}

Node *Node::get_parent() const
{
    return parent;
}

void Node::add_child(Node *child)
{
    if(child->parent != nullptr) return;
    for(Node *c : children)
    {
        if(c == child) return;
    }
    children.push_back(child);
    child->parent = this;
    child->scene_tree = scene_tree;
    if(scene_tree != nullptr)
    {
        child->_notification(NOTIFICATION_ENTER_TREE);
        child->_propagate_enter_tree();
    }
}

Node *Node::remove_child(uint32_t idx)
{
    if(idx >= children.size()) return nullptr;
    Node *child = children[idx];
    children.erase(children.begin() + idx);
    child->parent = nullptr;
    child->scene_tree = nullptr;

    child->_notification(NOTIFICATION_EXIT_TREE);
    child->_propagate_exit_tree();
    return child;
}

Node *Node::remove_child(Node *child)
{
    int idx = -1;
    for(int i = 0; i < children.size(); i++)
    {
        if(children[i] == child) {idx = i; break;}
    }
    if(idx == -1) return nullptr;
    children.erase(children.begin() + idx);
    child->parent = nullptr;
    child->scene_tree = nullptr;
    child->_notification(NOTIFICATION_EXIT_TREE);
    child->_propagate_exit_tree();
    return child;
}

void Node::queued_free()
{
    parent->remove_child(this);
    _notification(NOTIFICATION_EXIT_TREE);
    _propagate_exit_tree();
    // TODO 释放内存
}

void Node::ready()
{
}

void Node::process(double delta)
{
}

void Node::physics_process(double delta)
{
}

void Node::destroyed()
{
}

void Node::enter_tree()
{
}

void Node::exit_tree()
{
}

void Node::_propagate_process(double delta)
{
    for(Node *child : children)
    {
        if(child->is_process()) child->process(delta);
        child->_propagate_process(delta);
    }
}

void Node::_propagate_physics_process(double delta)
{
    for(Node *child : children)
    {
        if(child->is_physics_process()) child->physics_process(delta);
        child->_propagate_physics_process(delta);
    }
}

void Node::_propagate_enter_tree()
{
    for(Node *child : children)
    {
        child->_notification(NOTIFICATION_ENTER_TREE);
        child->_propagate_enter_tree();
    }
}

void Node::_propagate_exit_tree()
{
    for(Node *child : children)
    {
        child->_notification(NOTIFICATION_EXIT_TREE);
        child->_propagate_exit_tree();
    }
}
