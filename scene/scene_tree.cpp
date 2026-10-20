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
    last_process_time = last_frame_time;
    last_physics_process_time = last_frame_time;
    physics_frame = 0;
    physics_accumulator = 0.0;
}

bool SceneTree::tick(double time)
{
    // 先推进物理，再做随渲染帧变化的逻辑，最后渲染。
    if(physics_process(time)) return true;
    if(process(time)) return true;
    static_cast<Viewport*>(root)->render();
    return false;
}

bool SceneTree::process(double time)
{
    double delta = time - last_process_time;
    if(delta < 0.0) delta = 0.0;
    if(delta > MAX_FRAME_TIME) delta = MAX_FRAME_TIME;
    last_process_time = time;

    Node *scene = get_current_scene();
    if(scene != nullptr)
    {
        if(scene->is_process()) scene->process(delta);
        scene->_propagate_process(delta);
    }
    return false;
}

bool SceneTree::physics_process(double time)
{
    // 实际帧时间只影响累积器，物理回调永远收到固定的 physics_delta。
    double frame_time = time - last_physics_process_time;
    last_physics_process_time = time;
    if(frame_time < 0.0) frame_time = 0.0;
    if(frame_time > MAX_FRAME_TIME) frame_time = MAX_FRAME_TIME;
    physics_accumulator += frame_time;

    // 精度保护：避免 1/60 这类无法精确表示的数导致循环差一点点不退出。
    const double epsilon = physics_delta * 1e-9;
    int steps = 0;
    while(physics_accumulator + epsilon >= physics_delta)
    {
        Node *scene = get_current_scene();
        if(scene != nullptr)
        {
            if(scene->is_physics_process()) scene->physics_process(physics_delta);
            scene->_propagate_physics_process(physics_delta);
        }
        physics_accumulator -= physics_delta;
        if(physics_accumulator < 0.0) physics_accumulator = 0.0;
        ++physics_frame;

        // 补帧超上限时丢弃剩余时间，宁可让物理时间变慢，也不让程序越卡越慢。
        if(++steps >= MAX_PHYSICS_STEPS_PER_FRAME)
        {
            physics_accumulator = 0.0;
            break;
        }
    }
    return false;
}

void SceneTree::set_physics_ticks_per_second(double tps)
{
    if(tps <= 0.0) return;
    physics_ticks_per_second = tps;
    physics_delta = 1.0 / tps;
}

double SceneTree::get_physics_ticks_per_second() const
{
    return physics_ticks_per_second;
}

double SceneTree::get_physics_delta() const
{
    return physics_delta;
}

uint64_t SceneTree::get_physics_frame() const
{
    return physics_frame;
}

double SceneTree::get_physics_interpolation_fraction() const
{
    if(physics_delta <= 0.0) return 0.0;
    double fraction = physics_accumulator / physics_delta;
    if(fraction < 0.0) return 0.0;
    if(fraction > 1.0) return 1.0;
    return fraction;
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
