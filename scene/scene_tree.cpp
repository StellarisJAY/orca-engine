#include "scene_tree.h"
#include "node.h"
#include <algorithm>
#include <stack>

void SceneTree::init()
{
}

bool SceneTree::process(double time)
{
    double delta = time - last_process_time;
    for(Node *node : process_nodes) 
    {
        node->process(delta);
    }
    return false;
}

bool SceneTree::physics_process(double time)
{
    double delta = time - last_physics_process_time;
    for(Node *node : physics_nodes) 
    {
        node->physics_process(delta);
    }
    return false;
}

void SceneTree::destroy()
{
}

void SceneTree::add_node(Node *node)
{
    if(node->get_parent() != nullptr) return;
    for(Node *exist : roots)
    {
        if(exist == node) return;
    }
    roots.push_back(node);
}

void SceneTree::remove_node(Node *node)
{
    
}

Node *SceneTree::find_node(const char *path) const
{
    return nullptr;
}

void SceneTree::mark_dirty()
{
    dirty = true;
}

void SceneTree::sort_dirty_tree()
{
    if(!dirty) return;
    process_nodes.clear();
    physics_nodes.clear();
    std::stack<Node*> stack;
    std::vector<Node*> rootset = roots;
    std::reverse(rootset.begin(), rootset.end());
    for(Node *node : rootset) stack.push(node);
    while(stack.size()>0)
    {
        Node *node = stack.top();
        stack.pop();
        
        process_nodes.push_back(node);
        physics_nodes.push_back(node);
    }
}
