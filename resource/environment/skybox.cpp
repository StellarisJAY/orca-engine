#include "skybox.h"
#include <cstring>

namespace
{
    /// @brief 把错误信息写入可选输出参数
    void set_error(std::string *r_error, const std::string &message)
    {
        if (r_error != nullptr)
            *r_error = message;
    }

    /// @brief 单面像素数据的期望字节数
    size_t expected_face_bytes(const ImageTexture &texture)
    {
        return static_cast<size_t>(texture.get_width()) * texture.get_height() * texture.get_channels();
    }
}

const char *CubeMapSkybox::get_slot_name(Slot slot)
{
    switch (slot)
    {
    case Slot::POS_X:
        return "+X";
    case Slot::NEG_X:
        return "-X";
    case Slot::POS_Y:
        return "+Y";
    case Slot::NEG_Y:
        return "-Y";
    case Slot::POS_Z:
        return "+Z";
    case Slot::NEG_Z:
        return "-Z";
    }
    return "?";
}

Ref<Resource> CubeMapSkybox::duplicate() const
{
    auto copied = memnew(CubeMapSkybox);
    for(int i = 0; i < SLOT_COUNT; i++)
    {
        copied->cubemap_slots[i] = cubemap_slots[i];
    }
    copied->refresh_size();
    copied->mark_dirty(true);
    return Ref<Resource>(copied);
}

void CubeMapSkybox::set_image_texture(Slot slot, const Ref<ImageTexture> &texture)
{
    if(texture.is_valid())
    {
        cubemap_slots[static_cast<uint8_t>(slot)] = texture;
        refresh_size();
        mark_dirty(true);
    }
}

void CubeMapSkybox::refresh_size()
{
    width = 0;
    height = 0;
    channels = 0;
    for (uint8_t i = 0; i < SLOT_COUNT; i++)
    {
        if (cubemap_slots[i].is_null())
            continue;
        width = cubemap_slots[i]->get_width();
        height = cubemap_slots[i]->get_height();
        channels = cubemap_slots[i]->get_channels();
        return;
    }
}

const Ref<ImageTexture> &CubeMapSkybox::get_slot(Slot slot) const
{
    return cubemap_slots[static_cast<uint8_t>(slot)];
}

bool CubeMapSkybox::validate_faces(std::string *r_error) const
{
    const ImageTexture *reference = nullptr;
    const char *reference_name = nullptr;

    for (uint8_t i = 0; i < SLOT_COUNT; i++)
    {
        const Slot slot = static_cast<Slot>(i);
        const Ref<ImageTexture> &texture = cubemap_slots[i];
        if (texture.is_null())
        {
            set_error(r_error, std::string("missing skybox face ") + get_slot_name(slot));
            return false;
        }

        const uint32_t w = texture->get_width();
        const uint32_t h = texture->get_height();
        const uint8_t c = texture->get_channels();
        if (w == 0 || h == 0 || c == 0)
        {
            set_error(r_error, std::string("skybox face ") + get_slot_name(slot) + " has invalid size " +
                                   std::to_string(w) + "x" + std::to_string(h) + "x" + std::to_string(c));
            return false;
        }
        if (w != h)
        {
            set_error(r_error, std::string("skybox face ") + get_slot_name(slot) + " is not square (" +
                                   std::to_string(w) + "x" + std::to_string(h) + ")");
            return false;
        }
        if (texture->get_pixels().size() != expected_face_bytes(*texture.ptr()))
        {
            set_error(r_error, std::string("skybox face ") + get_slot_name(slot) + " pixel data size mismatch");
            return false;
        }

        if (reference == nullptr)
        {
            reference = texture.ptr();
            reference_name = get_slot_name(slot);
            continue;
        }
        if (w != reference->get_width() || h != reference->get_height())
        {
            set_error(r_error, std::string("skybox face ") + get_slot_name(slot) + " is " + std::to_string(w) + "x" +
                                   std::to_string(h) + " but face " + reference_name + " is " +
                                   std::to_string(reference->get_width()) + "x" + std::to_string(reference->get_height()));
            return false;
        }
        if (c != reference->get_channels())
        {
            set_error(r_error, std::string("skybox face ") + get_slot_name(slot) + " has " + std::to_string(c) +
                                   " channels but face " + reference_name + " has " +
                                   std::to_string(reference->get_channels()));
            return false;
        }
    }
    return true;
}

bool CubeMapSkybox::build_cubemap_pixels(std::vector<uint8_t> &r_pixels, std::string *r_error) const
{
    r_pixels.clear();
    if (!validate_faces(r_error))
        return false;

    const Ref<ImageTexture> &first = cubemap_slots[0];
    const uint32_t w = first->get_width();
    const uint32_t h = first->get_height();
    const uint8_t c = first->get_channels();
    const size_t row_bytes = static_cast<size_t>(w) * c;
    const size_t face_bytes = row_bytes * h;

    r_pixels.resize(face_bytes * SLOT_COUNT);

    for (uint8_t i = 0; i < SLOT_COUNT; i++)
    {
        const std::vector<uint8_t> &src = cubemap_slots[i]->get_pixels();
        uint8_t *dst = r_pixels.data() + face_bytes * i;

        // ImageTextureLoader 按 2D 纹理的约定在解码时做了垂直翻转（行 0 是图片底部），
        // 而 GL 的 cubemap 面以左上角为原点，上传数据的第一行必须是面的顶部。
        // 因此这里逐面把行序翻回去，避免天空盒上下颠倒。
        for (uint32_t y = 0; y < h; y++)
        {
            std::memcpy(dst + static_cast<size_t>(y) * row_bytes,
                        src.data() + static_cast<size_t>(h - 1 - y) * row_bytes,
                        row_bytes);
        }
    }
    return true;
}
