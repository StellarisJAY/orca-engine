#include "image_texture_loader.h"
#include "io/file_access.h"
#include "io/path.h"
#include "os/memory.h"
#include "texture/image_texture.h"
#include <cstring>
#include <cstdio>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace
{
    /// @brief 把 stb 解码结果转成 ImageTexture。
    /// 返回裸指针，方便 load() 与 load_from_memory() 各自只建立一次 Ref 所有权。
    ImageTexture *decode_image(
        const uint8_t *data, size_t size,
        bool srgb, const char *debug_name)
    {
        if (data == nullptr || size == 0)
        {
            std::printf("[ERROR] empty image data: %s\n",
                        debug_name != nullptr ? debug_name : "(unknown)");
            return nullptr;
        }

        int w = 0, h = 0, channels = 0;
        // 引擎约定：贴图解码后统一上下翻转，CPU 侧 UV 不再翻转。
        stbi_set_flip_vertically_on_load(1);
        uint8_t *pixels = stbi_load_from_memory(
            data, static_cast<int>(size), &w, &h, &channels, 0);
        if (pixels == nullptr)
        {
            std::printf("[ERROR] failed to decode image: %s (%s)\n",
                        debug_name != nullptr ? debug_name : "(unknown)",
                        stbi_failure_reason());
            return nullptr;
        }

        if (w <= 0 || h <= 0 || channels <= 0)
        {
            std::printf("[ERROR] invalid image data: %s (w=%d,h=%d,c=%d)\n",
                        debug_name != nullptr ? debug_name : "(unknown)", w, h, channels);
            stbi_image_free(pixels);
            return nullptr;
        }

        ImageTexture *texture = memnew(ImageTexture);
        const int total_bytes = w * h * channels;
        texture->set_pixels(std::vector<uint8_t>(pixels, pixels + total_bytes));
        texture->set_width(w);
        texture->set_height(h);
        texture->set_channels(channels);
        texture->set_srgb(srgb);
        stbi_image_free(pixels);
        return texture;
    }
}

Ref<Resource> ImageTextureLoader::load(const std::string url)
{
    // 先读原始字节再由内存解码：.gltf 内嵌图片走的是同一条解码路径。
    const std::string path = Path::replace_url(url);
    std::vector<uint8_t> bytes;
    if (!FileAccess::read_file_bytes(path, bytes))
    {
        std::printf("[ERROR] failed to read image file: %s\n", url.c_str());
        return Ref<Resource>(nullptr);
    }

    // 资源层不知道图片最终用于哪个材质槽位；默认按线性数据处理，
    // 颜色槽位（albedo）由 MTL/材质加载器显式 set_srgb(true)，
    // 避免按通道数猜测导致 normal/roughness 等数据图被错误地做 sRGB 解码。
    ImageTexture *texture = decode_image(bytes.data(), bytes.size(), false, url.c_str());
    if (texture == nullptr)
    {
        return Ref<Resource>(nullptr);
    }

    std::printf("loaded image texture: \"%s\", w=%d,h=%d,c=%d\n",
                url.c_str(),
                static_cast<int>(texture->get_width()),
                static_cast<int>(texture->get_height()),
                static_cast<int>(texture->get_channels()));
    return Ref<Resource>(texture);
}

Ref<ImageTexture> ImageTextureLoader::load_from_memory(
    const uint8_t *data, size_t size,
    bool srgb, const char *debug_name)
{
    ImageTexture *texture = decode_image(data, size, srgb, debug_name);
    if (texture == nullptr)
    {
        return Ref<ImageTexture>();
    }
    return Ref<ImageTexture>(texture);
}
