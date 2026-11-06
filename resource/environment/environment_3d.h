#pragma once

#include "resource.h"
#include "object/rid.h"
#include "skybox.h"
#include "math/color.h"

class Environment3D: public Resource
{
    RID env;
    Ref<CubeMapSkybox> cubemap_skybox;
    Color ambient_color = {0, 0, 0, 255};
    float ambient_strength = 1.0f;
public:
    Environment3D() {}
    RID get_rid() const override { return env; }
    Ref<Resource> duplicate() const override { return Ref<Resource>(); }
    void set_rid(RID rid) { env = rid; }
    Ref<CubeMapSkybox> get_skybox() const { return cubemap_skybox; }

    void set_skybox(const Ref<CubeMapSkybox> &skybox) { cubemap_skybox = skybox; }
    void set_ambient_color(const Color &color, float strength)
    {
        ambient_color = color;
        ambient_strength = strength;
    }

    const Color &get_ambiemt_color() const { return ambient_color; }
    float get_ambient_strength() const { return ambient_strength; }
};