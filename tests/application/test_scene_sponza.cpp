#include "application.h"
#include "3d/node3d.h"
#include "3d/camera_3d.h"
#include "os/memory.h"
#include "io/file_access.h"
#include "resource_loader.h"
#include "mesh/mesh.h"
#include "3d/mesh_instance_3d.h"
#include "material/material.h"
#include "3d/light_3d.h"
#include "scene/scene.h"
#include "instantiate.h"
#include "input.h"
#include "input_map.h"
#include <string>

class ControlledCamera: public Camera3D
{
    real_t yaw = 0.0;
    real_t pitch = 0.0;
    real_t rotate_speed = 90;
    real_t speed = 5.0;

    constexpr static real_t MAX_PITCH = 80.0;
    constexpr static real_t MIN_PITCH = -80.0;
public:
    void process(double delta) override
    {
        auto input = Input::get_singleton();
        Vector2 dir = input->get_direction("left", "right", "forward", "backward");
        real_t dir_y = input->get_axis("down", "up");
        Vector3 direction(dir.x, dir_y, dir.y);
        direction.normalize();

        real_t axis_yaw = input->get_axis("cam_right", "cam_left");
        yaw += axis_yaw * rotate_speed * (real_t)delta;

        set_rotation(Vector3::UP, yaw);
        set_position(get_position() + direction.rotated(Vector3::UP, Math::deg_to_rad(yaw)) * speed * (real_t)delta);
    }
};

Ref<CubeMapSkybox> load_skybox()
{
    auto resource_loader = ResourceLoader::get_singleton();
    Ref<Resource> front_res = resource_loader->load("res://tests/skybox/front.jpg");
    Ref<Resource> back_res = resource_loader->load("res://tests/skybox/back.jpg");
    Ref<Resource> left_res = resource_loader->load("res://tests/skybox/left.jpg");
    Ref<Resource> right_res = resource_loader->load("res://tests/skybox/right.jpg");
    Ref<Resource> bottom_res = resource_loader->load("res://tests/skybox/bottom.jpg");
    Ref<Resource> top_res = resource_loader->load("res://tests/skybox/top.jpg");

    Ref<CubeMapSkybox> skybox = Ref(memnew(CubeMapSkybox));
    skybox->set_image_texture(CubeMapSkybox::Slot::NEG_X, left_res.cast<ImageTexture>());
    skybox->set_image_texture(CubeMapSkybox::Slot::POS_X, right_res.cast<ImageTexture>());
    skybox->set_image_texture(CubeMapSkybox::Slot::POS_Z, front_res.cast<ImageTexture>());
    skybox->set_image_texture(CubeMapSkybox::Slot::NEG_Z, back_res.cast<ImageTexture>());
    skybox->set_image_texture(CubeMapSkybox::Slot::NEG_Y, bottom_res.cast<ImageTexture>());
    skybox->set_image_texture(CubeMapSkybox::Slot::POS_Y, top_res.cast<ImageTexture>());
    return skybox;
}

int main()
{
    Application app;
    ApplicationInitDesc desc;
    desc.fullscreen = false;
    desc.screen_w = 800;
    desc.screen_h = 600;
    if (!app.init(desc))
        return -1;

    Ref<Resource> sponza_res = ResourceLoader::get_singleton()->load("res://tests/sponza/Sponza.gltf");
    if(sponza_res.is_null())
    {
        app.shutdown();
        return -1;
    }

    Ref<PackedScene> sponza_scene = sponza_res.cast<PackedScene>();

    Ref<Environment3D> environment = Ref(memnew(Environment3D));
    environment->set_ambient_color({128, 128, 128}, 1.0);
    environment->set_skybox(load_skybox());
    
    World3D *world = memnew(World3D(environment));
    app.set_main_scene(world);

    Node3D *sponza_node = Instantiate::instantiate_scene(sponza_scene);
    world->add_child(sponza_node);

    Light3D *sun = memnew(Light3D);
    sun->set_color({255,255,255});
    sun->set_direction({-1, -1, -1});
    sun->set_intensity(5.0);
    world->add_child(sun);

    ControlledCamera *main_camera = memnew(ControlledCamera);
    world->add_child(main_camera);
    main_camera->set_perspective(60.0, (float)desc.screen_w / (float)desc.screen_h, 0.1, 1000);
    main_camera->set_position({0, 0, 4});
    main_camera->set_main_camera();

    auto input_map = InputMap::get_singleton();
    input_map->add_action(InputAction{
        .action_code = "cam_up",
        .keys = {KeyboardKeyCode::Up}
    });
    input_map->add_action(InputAction{
        .action_code = "cam_down",
        .keys = {KeyboardKeyCode::Down}
    });
    input_map->add_action(InputAction{
        .action_code = "cam_right",
        .keys = {KeyboardKeyCode::E}
    });
    input_map->add_action(InputAction{
        .action_code = "cam_left",
        .keys = {KeyboardKeyCode::Q}
    });
    input_map->add_action(InputAction{
        .action_code = "forward",
        .keys = {KeyboardKeyCode::W}
    });
    input_map->add_action(InputAction{
        .action_code = "backward",
        .keys = {KeyboardKeyCode::S}
    });
    input_map->add_action(InputAction{
        .action_code = "left",
        .keys = {KeyboardKeyCode::A}
    });
    input_map->add_action(InputAction{
        .action_code = "right",
        .keys = {KeyboardKeyCode::D}
    });
    input_map->add_action(InputAction{
        .action_code = "up",
        .keys = {KeyboardKeyCode::Space}
    });
    input_map->add_action(InputAction{
        .action_code = "down",
        .keys = {KeyboardKeyCode::Shift}
    });

    app.run();
    app.shutdown();
    return 0;
}
