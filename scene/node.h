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
    bool process_on = true;
    bool physics_process_on = true;
public:
    SceneTree *get_scene_tree() const;
    Node *get_child(uint32_t idx) const;
    Node *get_parent() const;
    void add_child(Node *child);
    Node *remove_child(uint32_t idx);
    Node *remove_child(Node *child);
    void queued_free();
    bool is_process() const { return process_on; }
    bool is_physics_process() const { return physics_process_on; }
    void set_process(bool on) { process_on = on; }
    void set_physics_process(bool on) { physics_process_on = on; }
public:
    virtual void ready();
    virtual void process(double delta);
    virtual void physics_process(double delta);
    virtual void destroyed();
    
    void _propagate_process(double delta);
    void _propagate_enter_tree();
    void _propagate_exit_tree();
};