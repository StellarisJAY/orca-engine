#pragma once

#include "resource.h"
#include "object/rid.h"
#include "math/color.h"
#include "object/ref_counted.h"
#include "texture/image_texture.h"
#include "math/vector2.h"

enum class MaterialTextureSlot : uint8_t
{
    Albedo = 0,
    Normal,
    Roughness,
    Metallic,
    AO,
    Height,
};

class StandardMaterial : public Resource
{
private:
    Color albedo;
    float metallic = 1.0;
    float roughness = 1.0;
    float specular = 1.0;
    float normal_scale = 1.0;
    float emission = 0.0;
    // UV缩放
    Vector2 uv_scale = {1.0, 1.0};
    // 高度缩放
    float parallax_scale = 1.0;
    bool parallax_flip = false;
    // 贴图槽位
    Ref<ImageTexture> texture_slots[6];

protected:
    RID rid = 0;

public:
    friend class MeshInstance3D;
    RID get_rid() const override { return rid; }
    void set_rid(RID rid) { this->rid = rid; }
    Ref<Resource> duplicate() const override
    {
        // TODO 复制材质
        return Ref<Resource>(nullptr);
    }

    Color get_albedo() const { return albedo; }
    Color get_base_color() const { return albedo; }
    float get_metallic() const { return metallic; }
    float get_roughenss() const { return roughness; }
    const Vector2 &get_uv_scale() const { return uv_scale; }
    float get_parallax_scale() const { return parallax_scale; }
    bool get_parallax_flip() const { return parallax_flip; }

    void set_albedo(const Color &color) { this->albedo = color; }
    void set_roughness(float roughness) { this->roughness = roughness; }
    void set_metallic(float metallic) { this->metallic = metallic; }
    void set_uv_scale(const Vector2 &scales) { uv_scale = scales; }
    void set_parallax_scale(float scale) { parallax_scale = scale; }
    void set_parallax_flip(bool flip) { parallax_flip = flip; }

    void set_texture_albedo(Ref<ImageTexture> texture) { texture_slots[(uint8_t)(MaterialTextureSlot::Albedo)] = texture; }
    void set_texture_normal(Ref<ImageTexture> texture) { texture_slots[(uint8_t)(MaterialTextureSlot::Normal)] = texture; }
    void set_texture_roughness(Ref<ImageTexture> texture) { texture_slots[(uint8_t)(MaterialTextureSlot::Roughness)] = texture; }
    void set_texture_metallic(Ref<ImageTexture> texture) { texture_slots[(uint8_t)(MaterialTextureSlot::Metallic)] = texture; }
    void set_texture_height(Ref<ImageTexture> texture) { texture_slots[(uint8_t)(MaterialTextureSlot::Height)] = texture; }
    void set_texture_ao(Ref<ImageTexture> texture) { texture_slots[(uint8_t)(MaterialTextureSlot::AO)] = texture; }

    Ref<ImageTexture> get_texture_albedo() const { return texture_slots[(uint8_t)(MaterialTextureSlot::Albedo)]; }
    Ref<ImageTexture> get_texture_normal() const { return texture_slots[(uint8_t)(MaterialTextureSlot::Normal)]; }
    Ref<ImageTexture> get_texture_roughness() const { return texture_slots[(uint8_t)(MaterialTextureSlot::Roughness)]; }
    Ref<ImageTexture> get_texture_metallic() const { return texture_slots[(uint8_t)(MaterialTextureSlot::Metallic)]; }
    Ref<ImageTexture> get_texture_height() const { return texture_slots[(uint8_t)(MaterialTextureSlot::Height)]; }
    Ref<ImageTexture> get_texture_ao() const { return texture_slots[(uint8_t)(MaterialTextureSlot::AO)]; }
};