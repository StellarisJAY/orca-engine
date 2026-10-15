#pragma once

#include "object/object.h"

#include <vector>

class Node;

class SceneTree: public Object
{
public:
    void init();
    bool tick(double time);
    void destroy();
    Node *find_node(const char *path) const;

    Node *get_root() const;
    void set_root(Node *r);
    Node *get_current_scene() const;
    void set_current_scene(Node *scene);
private:
    // 节点树的根，下面挂载正在运行的场景
    Node *root = nullptr;
    Node *current_scene = nullptr;
    double last_frame_time = 0;
    double last_process_time = 0;
    double last_physics_process_time = 0;
private:
    // 渲染帧，返回true表示中止
    bool process(double time);
    // 物理帧，返回true表示中止
    bool physics_process(double time);
};