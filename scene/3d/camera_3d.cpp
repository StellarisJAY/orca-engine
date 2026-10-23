#include "camera_3d.h"

/// @brief 设置该相机为viewport的主相机
void Camera3D::set_main_camera()
{
    World3D *world = get_world();
    if(world != nullptr) world->set_main_camera(rid);
}

/// @brief 获取该相机所在的viewport
/// @return 
Viewport *Camera3D::get_viewport() const
{
    // 场景树最外层的viewport
    Viewport *root_viewport = static_cast<Viewport*>(get_scene_tree()->get_root());
    Node *parent = get_parent();
    while(parent != root_viewport)
    {
        if(auto parent_viewport = dynamic_cast<Viewport*>(parent))
        {
            return parent_viewport;
        }
        parent = parent->get_parent();
    }
    return root_viewport;
}
