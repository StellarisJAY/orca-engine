#pragma once

#include "resource.h"
#include "object/ref_counted.h"
#include "texture/image_texture.h"
#include <string>
#include <vector>
/// @brief cubemap skybox
class CubeMapSkybox : public Resource
{
    RID rid = 0;
    bool dirty = false;
    Ref<ImageTexture> cubemap_slots[6];
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t channels = 0;

    /// @brief 从第一个非空面推导 width/height/channels，保证与拼接输出一致
    void refresh_size();

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
    /// @brief 面数量；Slot 的顺序就是 GL_TEXTURE_CUBE_MAP_POSITIVE_X + i 的顺序
    static constexpr uint8_t SLOT_COUNT = 6;

    RID get_rid() const override { return rid; }
    void set_rid(RID rid) { this->rid = rid; }
    void mark_dirty(bool dirty) { this->dirty = dirty; }
    bool is_dirty() const { return dirty; }
    Ref<Resource> duplicate() const override;
    void set_image_texture(Slot slot, const Ref<ImageTexture> &texture);
    const Ref<ImageTexture> &get_slot(Slot slot) const;

    /// @brief Slot 的可读名称，用于错误信息
    static const char *get_slot_name(Slot slot);

    /// @brief 校验六个面是否齐全，且宽高、通道数、像素数据长度完全一致。
    /// cubemap 面必须是正方形，否则底层无法创建纹理。
    /// 失败时填充 r_error（可为 nullptr），不修改对象状态。
    bool validate_faces(std::string *r_error = nullptr) const;

    /// @brief 按 GL cubemap 面顺序（+X,-X,+Y,-Y,+Z,-Z）拼出六个面的像素数据。
    /// 六面都经过校验；拼装时逐面恢复行序，原因见实现处的注释。
    /// 失败时清空 r_pixels 并填充 r_error。
    bool build_cubemap_pixels(std::vector<uint8_t> &r_pixels, std::string *r_error = nullptr) const;

    uint32_t get_width() const { return width; }
    uint32_t get_height() const { return height; }
    uint8_t get_channels() const { return channels; }
};
