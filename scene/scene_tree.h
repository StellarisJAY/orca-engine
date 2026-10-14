#pragma once

#include "object/object.h"
#include <vector>

class Node;

class SceneTree: public Object
{
public:
    void init();
    // 游戏帧，返回true表示中止
    bool process(double time);
    // 物理帧，返回true表示中止
    bool physics_process(double time);
    void destroy();
    void add_node(Node *node);
    void remove_node(Node *node);
    Node *find_node(const char *path) const;
    // 标记场景树结构发生变化，等待下一tick重新排序节点
    void mark_dirty();
private:
    std::vector<Node *> roots = {};
    bool dirty = false;
    std::vector<Node*> process_nodes = {};
    std::vector<Node*> physics_nodes = {};
    double last_process_time = 0;
    double last_physics_process_time = 0;
private:
    void sort_dirty_tree();
};