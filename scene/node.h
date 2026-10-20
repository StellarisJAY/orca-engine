#pragma once

#include "object/object.h"
#include "scene_tree.h"
#include <cstdint>
#include <vector>
#include <memory>

class Node: public Object
{
    friend class SceneTree;
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
    const std::vector<Node*> &get_children() const { return children; }
public:
    virtual void ready();
    virtual void process(double delta);
    virtual void physics_process(double delta);
    virtual void destroyed();
    virtual void enter_tree();
    virtual void exit_tree();
    
    void _propagate_process(double delta);
    void _propagate_physics_process(double delta);
    void _propagate_enter_tree();
    void _propagate_exit_tree();


    void _notification(int what) override
    {
        Object::_notification(what);
        switch(what)
        {
        case NOTIFICATION_ENTER_TREE:
            enter_tree();
            break;
        case NOTIFICATION_EXIT_TREE:
            exit_tree();
            break;
        default:
            break;
        }
    }
};