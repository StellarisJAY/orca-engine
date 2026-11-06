#include "doctest/doctest.h"
#include "environment/skybox.h"

namespace
{
    Ref<ImageTexture> make_face(uint32_t width, uint32_t height, uint8_t channels, uint8_t value)
    {
        Ref<ImageTexture> texture = Ref(memnew(ImageTexture));
        texture->set_width(width);
        texture->set_height(height);
        texture->set_channels(channels);
        texture->set_pixels(std::vector<uint8_t>(static_cast<size_t>(width) * height * channels, value));
        return texture;
    }

    /// @brief 每一行的所有通道都填成行号，用于验证拼接时的行序
    Ref<ImageTexture> make_row_marked_face(uint32_t size, uint8_t channels)
    {
        Ref<ImageTexture> texture = Ref(memnew(ImageTexture));
        texture->set_width(size);
        texture->set_height(size);
        texture->set_channels(channels);
        std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * channels);
        for (uint32_t y = 0; y < size; y++)
        {
            for (size_t i = 0; i < static_cast<size_t>(size) * channels; i++)
                pixels[static_cast<size_t>(y) * size * channels + i] = static_cast<uint8_t>(y);
        }
        texture->set_pixels(pixels);
        return texture;
    }

    /// @brief 六个面齐全、尺寸一致的天空盒；每个面填成自己的槽位下标
    Ref<CubeMapSkybox> make_complete_skybox(uint32_t width, uint32_t height, uint8_t channels)
    {
        Ref<CubeMapSkybox> skybox = Ref(memnew(CubeMapSkybox));
        for (uint8_t i = 0; i < CubeMapSkybox::SLOT_COUNT; i++)
        {
            skybox->set_image_texture(static_cast<CubeMapSkybox::Slot>(i),
                                      make_face(width, height, channels, i));
        }
        return skybox;
    }
}

TEST_CASE("CubeMapSkybox: 六面齐全且尺寸一致时校验通过")
{
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);

    std::string error = "unset";
    CHECK(skybox->validate_faces(&error));
    CHECK(error == "unset");
    CHECK(skybox->get_width() == 4);
    CHECK(skybox->get_height() == 4);
    CHECK(skybox->get_channels() == 3);

    std::vector<uint8_t> pixels;
    CHECK(skybox->build_cubemap_pixels(pixels, &error));
    CHECK(pixels.size() == 6u * 4u * 4u * 3u);
}

TEST_CASE("CubeMapSkybox: 缺少面时校验失败")
{
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);
    // 清掉一个面：Slot 没有单独的 remove 接口，用新建对象模拟更贴近真实用法
    Ref<CubeMapSkybox> incomplete = Ref(memnew(CubeMapSkybox));
    for (uint8_t i = 0; i < CubeMapSkybox::SLOT_COUNT - 1; i++)
    {
        incomplete->set_image_texture(static_cast<CubeMapSkybox::Slot>(i),
                                      make_face(4, 4, 3, i));
    }
    std::string error;
    CHECK_FALSE(incomplete->validate_faces(&error));
    CHECK(error.find("-Z") != std::string::npos);

    std::vector<uint8_t> pixels;
    CHECK_FALSE(incomplete->build_cubemap_pixels(pixels, &error));

    // 完整对象不受影响
    CHECK(skybox->validate_faces());
}

TEST_CASE("CubeMapSkybox: 面的尺寸不一致时校验失败")
{
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);
    skybox->set_image_texture(CubeMapSkybox::Slot::POS_Y, make_face(8, 8, 3, 9));

    std::string error;
    CHECK_FALSE(skybox->validate_faces(&error));
    CHECK(error.find("+Y") != std::string::npos);

    std::vector<uint8_t> pixels;
    CHECK_FALSE(skybox->build_cubemap_pixels(pixels, &error));
}

TEST_CASE("CubeMapSkybox: 面的通道数不一致时校验失败")
{
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);
    skybox->set_image_texture(CubeMapSkybox::Slot::NEG_X, make_face(4, 4, 4, 9));

    std::string error;
    CHECK_FALSE(skybox->validate_faces(&error));
    CHECK(error.find("-X") != std::string::npos);
}

TEST_CASE("CubeMapSkybox: 非正方形贴图被拒绝")
{
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);
    skybox->set_image_texture(CubeMapSkybox::Slot::POS_Z, make_face(4, 8, 3, 9));

    std::string error;
    CHECK_FALSE(skybox->validate_faces(&error));
    CHECK(error.find("square") != std::string::npos);
}

TEST_CASE("CubeMapSkybox: 像素数据长度与尺寸不符时校验失败")
{
    Ref<ImageTexture> broken = make_face(4, 4, 3, 0);
    // 制造一个声明尺寸与像素数据不一致的贴图
    broken->set_pixels(std::vector<uint8_t>(10, 0));
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);
    skybox->set_image_texture(CubeMapSkybox::Slot::POS_X, broken);

    std::string error;
    CHECK_FALSE(skybox->validate_faces(&error));
    CHECK(error.find("size mismatch") != std::string::npos);
}

TEST_CASE("CubeMapSkybox: 拼接顺序遵循 +X,-X,+Y,-Y,+Z,-Z")
{
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);

    std::vector<uint8_t> pixels;
    REQUIRE(skybox->build_cubemap_pixels(pixels));

    const size_t face_bytes = 4u * 4u * 3u;
    REQUIRE(pixels.size() == face_bytes * 6);
    for (size_t face = 0; face < 6; face++)
    {
        for (size_t i = 0; i < face_bytes; i++)
            CHECK(pixels[face * face_bytes + i] == static_cast<uint8_t>(face));
    }
}

TEST_CASE("CubeMapSkybox: 拼接时逐面恢复行序")
{
    // 解码层统一垂直翻转了行序（2D 约定）；cubemap 面要求第一行是顶部，
    // 因此拼接输出中第 y 行必须来自源贴图的第 (h-1-y) 行。
    const uint32_t size = 4;
    const uint8_t channels = 3;

    Ref<CubeMapSkybox> skybox = Ref(memnew(CubeMapSkybox));
    for (uint8_t i = 0; i < CubeMapSkybox::SLOT_COUNT; i++)
    {
        skybox->set_image_texture(static_cast<CubeMapSkybox::Slot>(i),
                                  make_row_marked_face(size, channels));
    }

    std::vector<uint8_t> pixels;
    REQUIRE(skybox->build_cubemap_pixels(pixels));

    const size_t row_bytes = static_cast<size_t>(size) * channels;
    const size_t face_bytes = row_bytes * size;
    for (size_t face = 0; face < 6; face++)
    {
        for (uint32_t y = 0; y < size; y++)
        {
            // 源贴图的行 y 填的是 y，翻转后输出第 y 行应当是 size-1-y
            const uint8_t expected = static_cast<uint8_t>(size - 1 - y);
            for (size_t i = 0; i < row_bytes; i++)
                CHECK(pixels[face * face_bytes + y * row_bytes + i] == expected);
        }
    }
}

TEST_CASE("CubeMapSkybox: 校验失败时不写出像素数据")
{
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);
    skybox->set_image_texture(CubeMapSkybox::Slot::NEG_Y, make_face(4, 4, 4, 0));

    std::vector<uint8_t> pixels(64, 0xAB);
    std::string error;
    CHECK_FALSE(skybox->build_cubemap_pixels(pixels, &error));
    CHECK(pixels.empty());
    CHECK_FALSE(error.empty());
}

TEST_CASE("CubeMapSkybox: duplicate 保留六个面与尺寸信息")
{
    Ref<CubeMapSkybox> skybox = make_complete_skybox(4, 4, 3);
    Ref<Resource> copied = skybox->duplicate();
    REQUIRE(copied.is_valid());

    Ref<CubeMapSkybox> copied_skybox = copied.cast<CubeMapSkybox>();
    CHECK(copied_skybox->get_width() == 4);
    CHECK(copied_skybox->get_height() == 4);
    CHECK(copied_skybox->get_channels() == 3);
    CHECK(copied_skybox->validate_faces());
}
