#include "scene_tree.h"
#include "node.h"
#include "viewport.h"
#include "os/time.h"
#include "os/memory.h"
#include <algorithm>
#include <stack>

void SceneTree::init()
{
    Viewport *viewport = memnew(Viewport);
    viewport->init();
    set_root(viewport);

    last_frame_time = Time::now();
    last_process_time = 0;
    last_physics_process_time = 0;
}

bool SceneTree::tick(double time)
{
    if(process(time)) return true;
    if(physics_process(time)) return true;
    static_cast<Viewport*>(root)->render();
    return false;
}

bool SceneTree::process(double time)
{
    return false;
}

bool SceneTree::physics_process(double time)
{
    return false;
}

void SceneTree::destroy()
{
}

Node *SceneTree::find_node(const char *path) const
{
    return nullptr;
}

Node *SceneTree::get_root() const
{
    return root;
}

void SceneTree::set_root(Node *r)
{
    root = r;
    if(root) root->scene_tree = this;
}

Node *SceneTree::get_current_scene() const
{
    return current_scene;
}

void SceneTree::set_current_scene(Node *scene)
{
    if(current_scene) root->remove_child(current_scene);
    root->add_child(scene);
    current_scene = scene;
}
