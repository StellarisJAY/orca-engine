#pragma once

#include "resource.h"
#include "object/ref_counted.h"
#include "texture/image_texture.h"
/// @brief cubemap skybox
class CubeMapSkybox : public Resource
{
    RID rid = 0;
    bool dirty = false;
    Ref<ImageTexture> cubemap_slots[6];
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t channels = 0;
public:
    enum class Slot : uint8_t
    {
        POS_X,
        NEG_X,
        POS_Y,
        NEG_Y,
        POS_Z,
        NEG_Z,
    };
    RID get_rid() const override { return rid; }
    void set_rid(RID rid) { this->rid = rid; }
    void mark_dirty(bool dirty) { this->dirty = dirty; }
    bool is_dirty() const { return dirty; }
    Ref<Resource> duplicate() const override;
    void set_image_texture(Slot slot, const Ref<ImageTexture> &texture);
    const Ref<ImageTexture> &get_slot(Slot slot) const;

    uint32_t get_width() const { return width; }
    uint32_t get_height() const { return height; }
    uint8_t get_channels() const { return channels; }
};