#pragma once

#include "resource_loader.h"
#include "texture/image_texture.h"

class ImageTextureLoader: public ResourceFormatLoader
{
public:
    std::vector<std::string> format_extensions() const override { return {".png", ".jpeg", ".bmp", ".jpg"}; }

    Ref<Resource> load(const std::string url) override;

    /// @brief 直接解码内存中的图片字节，用于 .gltf 的 data URI / .glb 内嵌图片
    /// @param data 图片字节
    /// @param size 字节数
    /// @param srgb 是否是颜色数据（baseColor/emissive 为 true，法线/粗糙度等为 false）
    /// @param debug_name 仅用于日志
    /// @return 解码成功返回 ImageTexture，失败返回空 Ref
    static Ref<ImageTexture> load_from_memory(
        const uint8_t *data, size_t size,
        bool srgb, const char *debug_name);
};