#pragma once

#include "visual_instance_3d.h"
#include "math/color.h"
#include "rendering_server.h"

class Light3D: public VisualInstance3D
{
public:
    enum LightType {
        DIRECTIONAL,
    };
private:
    LightType type = LightType::DIRECTIONAL;
    Color color;
    Vector3 direction;
    bool shadow = false;
    Color correlated_color;
    float temperature = 6000;
    float intensity = 1.0f;
protected:
    RID rid;

public:
    void set_color(const Color &color) { this->color = color; }
    void set_shadow(bool shadow) { this->shadow = shadow; }
    void set_temperature(float temp) { temperature = temp; }
    void set_direction(const Vector3 &direction) { this->direction = direction; }
    void set_intensity(float value)
    {
        intensity = value;
        if (rid != 0)
            RenderingServer::get_singleton()->set_light_intensity(rid, intensity);
    }
    float get_intensity() const { return intensity; }
    
    void _notification(int what) override
    {
        VisualInstance3D::_notification(what);
        RenderingServer *rendering_server = RenderingServer::get_singleton();
        switch(what)
        {
        case NOTIFICATION_ENTER_TREE:
            rid = rendering_server->create_light();
            rendering_server->set_light_transform(rid, get_global_transform());
            rendering_server->set_light_temperature(rid, temperature);
            rendering_server->set_light_color(rid, color);
            rendering_server->set_light_intensity(rid, intensity);
            rendering_server->set_light_cast_shadow(rid, shadow);
            rendering_server->set_light_direction(rid, direction);
            rendering_server->set_light_scene(rid, get_world()->get_scene());
            break;
        case NOTIFICATION_EXIT_TREE:
            rendering_server->free(rid);
            rid = 0;
            break;
        }
    }
};