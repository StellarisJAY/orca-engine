#pragma once

#include "visual_instance_3d.h"
#include "object/ref_counted.h"
#include "mesh/mesh.h"
#include "material/material.h"

class MeshInstance3D: public VisualInstance3D
{
    Ref<Mesh> mesh;
    Ref<Material> material;
public:
};