#pragma once

#include "resource.h"
#include "os/memory.h"

/// @brief 图像纹理
class ImageTexture: public Resource
{
protected:
    RID rid = 0;
private:
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t channels = 1;

    /// @brief RGBA pixels
    std::vector<uint8_t> pixels;

    bool srgb = true;
    bool generate_mipmap = true;
    bool linear_filter = true;
    bool repeat = true;

    bool rid_dirty = false;
public:
    RID get_rid() const override { return rid; }
    void set_rid(RID rid) { this->rid = rid; }
    Ref<Resource> duplicate() const override 
    {
        return Ref<Resource>(memnew(ImageTexture));
    }

    std::vector<uint8_t> &get_pixels() { return pixels; }
    const std::vector<uint8_t> &get_pixels() const { return pixels; }

    uint32_t get_width() const { return width; }
    uint32_t get_height() const { return height; }
    uint8_t get_channels() const { return channels; }
    bool is_srgb() const { return srgb; }
    bool is_repeat() const { return repeat; }
    bool is_linear_filter() const { return linear_filter; }
    bool is_gen_mipmap() const { return generate_mipmap; }

    void set_pixels(const std::vector<uint8_t> &pixels) { this->pixels = pixels; }
    void set_width(uint32_t w) { width = w; }
    void set_height(uint32_t h) { height = h; }
    void set_channels(uint8_t c) { channels = c; }
    void set_srgb(bool srgb) { this->srgb = srgb; }

    bool is_rid_dirty() const { return rid_dirty; }
    void mark_dirty(bool dirty) { rid_dirty = dirty; }
};