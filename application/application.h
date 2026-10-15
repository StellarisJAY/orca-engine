#pragma once

#include "window.h"
#include "node.h"
#include "scene_tree.h"

struct ApplicationInitDesc
{
    bool fullscreen = false;
    uint32_t screen_w = 1280;
    uint32_t screen_h = 720;
    uint32_t frame_rate = 60;
    bool vsync = false;
};

class Application
{
private:
    std::unique_ptr<Window> window;
    SceneTree *scene_tree;
public:
    bool init(const ApplicationInitDesc &desc);
    void run();
    void shutdown();
    void set_main_scene(Node *node);
};