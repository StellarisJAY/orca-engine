#include "image_texture_loader.h"
#include "io/path.h"
#include "os/memory.h"
#include "texture/image_texture.h"
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Ref<Resource> ImageTextureLoader::load(const std::string url)
{
    const std::string path = Path::replace_url(url);
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(1);
    uint8_t *data = stbi_load(path.c_str(), &w, &h, &channels, 0);
    if (data == nullptr)
        return Ref<Resource>(nullptr);

    ImageTexture *texture = memnew(ImageTexture);
    // 暂时只考虑RGBA纹理
    const int total_bytes = w * h * channels;
    texture->set_pixels(std::vector<uint8_t>(data, data + total_bytes));
    texture->set_width(w);
    texture->set_height(h);
    texture->set_channels(channels);
    
    stbi_image_free(data);
    return Ref<Resource>(texture);
}
