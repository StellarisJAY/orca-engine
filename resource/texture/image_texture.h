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

    static Ref<ImageTexture> get_default_zeros(uint32_t width, uint32_t height, uint8_t channels) 
    {
        Ref<ImageTexture> texture = Ref(memnew(ImageTexture));
        texture->set_width(width);
        texture->set_height(height);
        texture->set_channels(channels);
        texture->set_pixels(std::vector<uint8_t>(width*height*channels));
        std::fill(texture->get_pixels().begin(), texture->get_pixels().end(), 0);
        return texture;
    }

    static Ref<ImageTexture> get_default_white(uint32_t width, uint32_t height, uint8_t channels)
    {
        Ref<ImageTexture> texture = Ref(memnew(ImageTexture));
        texture->set_width(width);
        texture->set_height(height);
        texture->set_channels(channels);
        texture->set_pixels(std::vector<uint8_t>(width*height*channels));
        std::fill(texture->get_pixels().begin(), texture->get_pixels().end(), 255);
        return texture;
    }

    static Ref<ImageTexture> get_default_normal(uint32_t width, uint32_t height)
    {
        Ref<ImageTexture> texture = Ref(memnew(ImageTexture));
        texture->set_width(width);
        texture->set_height(height);
        texture->set_channels(3);
        texture->set_pixels(std::vector<uint8_t>(width*height*3));
        // 128,128,255
        for(auto i = 0; i < texture->get_pixels().size(); i+=3)
        {
            texture->get_pixels()[i] = 128;
            texture->get_pixels()[i+1] = 128;
            texture->get_pixels()[i+2] = 255;
        }
        return texture;
    }
};