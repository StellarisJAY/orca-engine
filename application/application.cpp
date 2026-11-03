#include "application.h"
#include "window.h"
#include "rendering_device.h"
#include "rendering_server.h"
#include "os/memory.h"
#include "os/time.h"
#include "resource_loader.h"
#include "input.h"
#include <iostream>

bool Application::init(const ApplicationInitDesc &desc)
{
    // 创建资源加载器
    ResourceLoader::create_singleton();
    ResourceCache::create_singleton();
    // 创建rendering device
    RenderingDevice::create_singleton();
    auto device = RenderingDevice::get_singleton();

    WindowDesc d;
    d.fullscreen = desc.fullscreen;
    d.height = desc.screen_h;
    d.width = desc.screen_w;
    d.visible = true;
    d.driver = device->get_default_driver();

    // 创建窗口
    window = Window::create();
    if(!window->init(d)) return false;
    std::printf("Window initialized.\n");

    // 创建输入管理器
    Input::create_singleton();
    // 窗口事件回调
    window->register_event_callback([this](const WindowEvent &event){
        window_event_callback(event);
    });

    // 初始化渲染后端
    RenderingWindowInfo window_info = window->get_rendering_window_info();
    if(!device->init(window_info)) return false;
    if(!device->init_device()) return false;
    if(!device->init_swapchain()) return false;
    std::printf("Rendering Device initialized.\n");

    // 初始化渲染服务器
    RenderingServer::create_singleton();
    RenderingServer::get_singleton()->init();
    std::printf("Rendering Server initialized.\n");

    // 创建场景树
    scene_tree = memnew(SceneTree);
    scene_tree->init();
    std::printf("SceneTree initialized.\n");
    return true;
}

void Application::window_event_callback(const WindowEvent &event)
{
    InputEvent input_event;
    input_event.key_code = event.key_code;
    input_event.pressed = event.pressed;
    input_event.echo = event.repeat;
    
    input_event.mouse_button = event.mouse_button;
    input_event.position = Vector2(event.x, event.y);
    input_event.relative = Vector2(event.delta_x, event.delta_y);

    input_event.scroll = Vector2(event.scroll_x, event.scroll_y);
    input_event.focus = event.focused;
    switch(event.type)
    {
    case WindowEventType::Key:
        input_event.type = InputEventType::Key;
        break;
    case WindowEventType::MouseButton:
        input_event.type = InputEventType::MouseButton;
        break;
    case WindowEventType::MouseMotion:
        input_event.type = InputEventType::MouseMotion;
        break;
    case WindowEventType::MouseWheel:
        input_event.type = InputEventType::MouseWheel;
        break;
    case WindowEventType::Focus:
        input_event.type = InputEventType::WindowFocus;
        break;
    default:
        // TODO more types
        break;
    }

    Input::get_singleton()->parse_input_event(input_event);
}

void Application::run()
{
    double last = Time::now();
    while(!window->get_should_close())
    {
        // 窗口事件
        window->poll_event();
        double t = Time::now();
        double delta = t - last;
        last = t;
        // 场景树执行一帧
        scene_tree->tick(t);
        // 交换帧缓存
        window->swap_buffers();
    }
}

void Application::shutdown()
{
    scene_tree->destroy();
    memdelete(scene_tree);
    Input::destroy_singleton();

    RenderingServer::destroy_singletone();
    RenderingDevice::destroy_singleton();
    ResourceLoader::destroy_singleton();
    window->shutdown();
}

void Application::set_main_scene(Node *node)
{
    scene_tree->set_current_scene(node);
}
