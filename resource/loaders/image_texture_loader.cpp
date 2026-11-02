#include "image_texture_loader.h"
#include "io/path.h"
#include "os/memory.h"
#include "texture/image_texture.h"
#include <cstring>
#include <cstdio>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Ref<Resource> ImageTextureLoader::load(const std::string url)
{
    const std::string path = Path::replace_url(url);
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(1);
    uint8_t *data = stbi_load(path.c_str(), &w, &h, &channels, 0);
    if (data == nullptr)
    {
        std::printf("[ERROR] failed to load image: %s (%s)\n",
                    url.c_str(), stbi_failure_reason());
        return Ref<Resource>(nullptr);
    }

    if (w <= 0 || h <= 0 || channels <= 0)
    {
        std::printf("[ERROR] invalid image data: %s (w=%d,h=%d,c=%d)\n",
                    url.c_str(), w, h, channels);
        stbi_image_free(data);
        return Ref<Resource>(nullptr);
    }

    ImageTexture *texture = memnew(ImageTexture);
    const int total_bytes = w * h * channels;
    texture->set_pixels(std::vector<uint8_t>(data, data + total_bytes));
    texture->set_width(w);
    texture->set_height(h);
    // 资源层不知道图片最终用于哪个材质槽位；默认按线性数据处理，
    // 颜色槽位（albedo）由 MTL/材质加载器显式 set_srgb(true)，
    // 避免按通道数猜测导致 normal/roughness 等数据图被错误地做 sRGB 解码。
    texture->set_srgb(false);
    texture->set_channels(channels);
    std::printf("loaded image texture: \"%s\", w=%d,h=%d,c=%d\n", url.c_str(), w, h, channels);

    stbi_image_free(data);
    return Ref<Resource>(texture);
}
