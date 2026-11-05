#pragma once

#include "resource.h"
#include "object/rid.h"
#include "skybox.h"

class Environment3D: public Resource
{
    RID env;
    Ref<CubeMapSkybox> cubemap_skybox;
public:
    RID get_rid() const { return env; }
};