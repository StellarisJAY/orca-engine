#pragma once

#include "object/object.h"
#include "scene_tree.h"
#include <cstdint>
#include <vector>
#include <memory>

class Node: public Object
{
    std::vector<Node*> children = {};
    Node *parent = nullptr;
    SceneTree *scene_tree = nullptr;
public:
    SceneTree *get_scene_tree() const;
    Node *get_child(uint32_t idx) const;
    Node *get_parent() const;
    void add_child(Node *child);
    Node *remove_child(uint32_t idx);
    void free();

public:
    virtual void ready();
    virtual void process(double delta);
    virtual void physics_process(double delta);
    virtual void destroyed();
};