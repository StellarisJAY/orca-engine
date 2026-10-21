#pragma once

#include "node3d.h"
#include "object/rid.h"
#include "rendering_server.h"
#include "math/math_defs.h"
#include "viewport.h"

class Camera3D : public Node3D
{
public:
    enum CameraProjectionType {
        ORTHOGONAL,
        PERSPECTIVE,
    };
private:
    RID rid;
    CameraProjectionType projection_type = PERSPECTIVE;
    real_t fov = 75; real_t aspect = 800.0 / 600.0;
    real_t near = 0.1; real_t far = 100;
    real_t left; real_t right;
    real_t top; real_t bottom;

public:
    void _notification(int what) override
    {
        Node3D::_notification(what);
        switch (what)
        {
        case NOTIFICATION_ENTER_TREE:
            rid = RenderingServer::get_singleton()->create_camera();
            RenderingServer::get_singleton()->set_camera_transform(rid, get_global_transform());
            if(projection_type == PERSPECTIVE) {
                RenderingServer::get_singleton()->set_camera_perspective(rid, fov, aspect, near, far);
            } else if(projection_type == ORTHOGONAL) {
                RenderingServer::get_singleton()->set_camera_orthogonal(rid, left, right, bottom, top, near, far);
            }
            break;
        case NOTIFICATION_EXIT_TREE:
            RenderingServer::get_singleton()->free(rid);
            break;
        case NOTIFICATION_TRANSFORM_CHANGE:
            RenderingServer::get_singleton()->set_camera_transform(rid, get_global_transform());
            break;
        }
    }
    void set_perspective(real_t fov, real_t aspect, real_t near, real_t far) 
    {
        projection_type = PERSPECTIVE;
        this->fov = fov;
        this->aspect = aspect;
        this->near = near;
        this->far = far;
        RenderingServer::get_singleton()->set_camera_perspective(rid, fov, aspect, near, far);
    }

    void set_orthognal(real_t left, real_t right, real_t bottom, real_t top, real_t near, real_t far)
    {
        projection_type = ORTHOGONAL;
        this->left = left;
        this->right = right;
        this->bottom = bottom;
        this->top = top;
        this->near = near;
        this->far = far;
        RenderingServer::get_singleton()->set_camera_orthogonal(rid, left, right, bottom, top, near, far);
    }

    void set_main_camera();
    Viewport *get_viewport() const;
};