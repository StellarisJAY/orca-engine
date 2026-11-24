#include "rendering_server.h"

namespace
{
    Format get_texture_format(uint8_t channels, bool srgb)
    {
        if (srgb)
            return Format::RGBA8UnormSrgb;
        else if (channels == 4)
            return Format::RGBA8Unorm;
        else if (channels == 1)
            return Format::R8Unorm;
        else if (channels == 2)
            return Format::RG8Unorm;
        else
        {
            throw std::invalid_argument("unknown texture format");
        }
    }

    TextureData &create_texture_internal(uint32_t width, uint32_t height, uint8_t channels, bool srgb, TextureDimension dim)
    {
        if (width == 0 || height == 0 || channels == 0 || channels > 4)
        {
            std::printf("[ERROR] set_texture_image: invalid texture size %ux%u, channels=%u\n",
                        width, height, static_cast<unsigned>(channels));
            throw std::invalid_argument("invalid texture size");
        }

        auto rendering_device = RenderingDeviceRefactor::get_singleton();

        TextureDescRefactor desc;
        desc.dim = dim;
        desc.width = width;
        desc.height = height;
        desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::COPY_DST;
        desc.samples_count = 1;
        desc.format = get_texture_format(channels, srgb);

        TextureData texture_data;
        texture_data.handle = rendering_device->create_texture(desc);
        texture_data.width = width;
        texture_data.height = height;
        texture_data.channels = channels;
        texture_data.layers = dim == TextureDimension::Cube ? 6 : 1;
        texture_data.format = desc.format;
        texture_data.srgb = srgb;
        return texture_data;
    }
};

RID RenderingServer::create_texture()
{
    return storage.textures.create(TextureData());
}

void RenderingServer::set_texture_image(
    RID texture,
    uint32_t width,
    uint32_t height,
    uint8_t channels,
    const std::vector<uint8_t> &pixels,
    bool srgb,
    bool repeat,
    bool linear_filter,
    bool gen_mipmap)
{
    
}

RID RenderingServer::create_texture_2d(uint32_t width, uint32_t height, uint8_t channels, bool srgb)
{
    return storage.textures.create(create_texture_internal(width, height, channels, srgb, TextureDimension::Texture2D));
}

RID RenderingServer::create_texture_3d(uint32_t width, uint32_t height, uint8_t channels, bool srgb)
{

    return storage.textures.create(create_texture_internal(width, height, channels, srgb, TextureDimension::Cube));
}

void RenderingServer::set_texture_sampler(RID texture, bool repeat, bool linear_filter)
{
    const auto texture_data = storage.textures.get_data(texture);
    if (texture_data == nullptr)
    {
        throw std::invalid_argument("invalid texture RID");
    }
    SamplerDesc desc;
    desc.address_u = repeat ? AddressMode::Repeat : AddressMode::ClampToEdge;
    desc.address_v = repeat ? AddressMode::Repeat : AddressMode::ClampToEdge;
    desc.address_w = repeat ? AddressMode::Repeat : AddressMode::ClampToEdge;
    desc.mag_filter = linear_filter ? FilterMode::Linear : FilterMode::Nearest;
    desc.min_filter = linear_filter ? FilterMode::Linear : FilterMode::Nearest;
    desc.mipmap_filter = linear_filter ? FilterMode::Linear : FilterMode::Nearest;
    
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    texture_data->sampler = rendering_device->create_sampler(desc);
    texture_data->linear_filter = linear_filter;
    texture_data->repeat = repeat;
}

void RenderingServer::write_texture(RID texture, uint8_t layer = 0, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const std::vector<uint8_t> &pixels)
{
    const auto texture_data = storage.textures.get_data(texture);
    if (texture_data == nullptr)
    {
        throw std::invalid_argument("invalid texture RID");
    }

    const size_t expected_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(texture_data->channels);
    if (pixels.size() < expected_bytes)
    {
        throw std::invalid_argument("invalid pixel buffer, mismatch size");
    }

    if (x + width > texture_data->width || y + height > texture_data->height)
    {
        throw std::invalid_argument("invalid origin x,y");
    }

    if (layer >= texture_data->layers)
    {
        throw std::invalid_argument("invalid texture layer");
    }

    if (!texture_data->handle.is_valid())
    {
        throw std::invalid_argument("invalid texture rid, handle is invalid");
    }

    TextureWriteDesc desc;
    desc.array_layer = layer;
    desc.bytes_per_row = 0;
    desc.width = width;
    desc.height = height;
    desc.origin_x = x;
    desc.origin_y = y;
    desc.data_size = pixels.size();
    desc.data = pixels.data();

    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    rendering_device->queue_write_texture(texture_data->handle, desc);
}

void RenderingServer::free_texture(RID texture)
{
    auto texture_data = storage.textures.get_data(texture);
    if(texture_data == nullptr) return;
    if(texture_data->handle.is_valid())
    {
        auto rendering_device = RenderingDeviceRefactor::get_singleton();
        rendering_device->destroy_texture(texture_data->handle);
        storage.textures.free(texture);
    }
}
