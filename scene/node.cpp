#include "node.h"
#include <algorithm>

SceneTree *Node::get_scene_tree() const
{
    return scene_tree;
}

Node *Node::get_child(uint32_t idx) const
{
    if(idx > children.size()) return nullptr;
    return children[idx];
}

Node *Node::get_parent() const
{
    return parent;
}

void Node::add_child(Node *child)
{
    for(Node *c : children)
    {
        if(c == child) return;
    }
    children.push_back(child);
    child->parent = this;
    child->scene_tree = scene_tree;
}

Node *Node::remove_child(uint32_t idx)
{
    if(idx > children.size()) return nullptr;
    auto removed = children[idx];
    children.erase(children.begin()+idx);
    removed->parent = nullptr;
    removed->scene_tree->add_node(removed);
    return removed;
}

void Node::free()
{
    scene_tree->remove_node(this);
    scene_tree = nullptr;
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
