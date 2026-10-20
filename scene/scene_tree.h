#pragma once

#include "object/object.h"

#include <cstdint>
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

    // ---- 物理帧 ----
    // 物理帧按固定步长推进，默认每秒 60 步。
    void set_physics_ticks_per_second(double tps);
    double get_physics_ticks_per_second() const;
    // 固定步长，单位秒，即 1 / physics_ticks_per_second。
    double get_physics_delta() const;
    // 已经执行的物理帧总数，可用于判断某帧是否发生了物理推进。
    uint64_t get_physics_frame() const;
    // 当前累积器占一个物理步长的比例 [0, 1)，
    // 渲染时可以用它在上一物理帧和当前物理帧之间做插值。
    double get_physics_interpolation_fraction() const;
private:
    // 节点树的根，下面挂载正在运行的场景
    Node *root = nullptr;
    Node *current_scene = nullptr;
    double last_frame_time = 0;
    double last_process_time = 0;
    double last_physics_process_time = 0;

    double physics_ticks_per_second = 60.0;
    double physics_delta = 1.0 / 60.0;
    // 尚未消耗的物理时间，每帧把实际帧时间累加进来，再按固定步长消耗。
    double physics_accumulator = 0.0;
    uint64_t physics_frame = 0;

    // 单帧最多执行的物理步数，防止卡顿后一次补太多帧（死亡螺旋）。
    static constexpr int MAX_PHYSICS_STEPS_PER_FRAME = 8;
    // 单帧时间上限，防止断点调试/窗口拖动后产生巨大的 delta。
    static constexpr double MAX_FRAME_TIME = 0.25;
private:
    // 渲染帧，返回true表示中止
    bool process(double time);
    // 物理帧，返回true表示中止
    bool physics_process(double time);
};
