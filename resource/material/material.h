#pragma once

#include "resource.h"
#include "object/rid.h"
#include "math/color.h"

class Material: public Resource
{
    RID material;
public:
    RID get_rid() const { return material; }
};

class StandardMaterial: public Resource
{
private:
    Color albedo;
    float metallic = 0;
    float roughness = 0.5;
    float specular = 1.0;
    float normal_scale = 1.0;
    float emission = 0.0;
    // TODO 贴图槽位
protected:
    RID rid;
public:
    RID get_rid() const override { return rid; }
    void set_rid(RID rid) { this->rid = rid; }
    Ref<Resource> duplicate() const override 
    {
        return Ref<Resource>(nullptr);
    }

    Color get_albedo() const { return albedo; }
    Color get_base_color() const { return albedo; }
    float get_metallic() const { return metallic; }
    float get_roughenss() const { return roughness; }

    void set_albedo(const Color &color) { this->albedo = color; }
    void set_roughness(float roughness) { this->roughness = roughness; }
    void set_metallic(float metallic) { this->metallic = metallic; }
};