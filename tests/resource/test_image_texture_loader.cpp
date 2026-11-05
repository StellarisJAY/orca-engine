#include "doctest/doctest.h"
#include "loaders/image_texture_loader.h"
#include "texture/image_texture.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace
{
    // 1x1 RGBA PNG（红色），用于验证内存解码路径
    const uint8_t PNG_1X1[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xcf, 0xc0, 0x50,
        0x0f, 0x00, 0x04, 0x85, 0x01, 0x80, 0x84, 0xa9, 0x8c, 0x21, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

    const char *TEXTURE_TEST_PATH = "orca_texture_loader_test.png";

    void write_png_file()
    {
        std::ofstream file(TEXTURE_TEST_PATH, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        file.write(reinterpret_cast<const char *>(PNG_1X1), sizeof(PNG_1X1));
    }
}

TEST_CASE("ImageTextureLoader: 从内存解码 PNG")
{
    Ref<ImageTexture> texture = ImageTextureLoader::load_from_memory(
        PNG_1X1, sizeof(PNG_1X1), true, "test_1x1.png");
    REQUIRE(texture.is_valid());

    CHECK(texture->get_width() == 1);
    CHECK(texture->get_height() == 1);
    CHECK(texture->get_channels() == 4);
    CHECK(texture->get_pixels().size() == 4);
    // 1x1 红色像素（半透明），验证解码后的像素顺序为 RGBA
    CHECK(texture->get_pixels()[0] == 255);
    CHECK(texture->get_pixels()[1] == 0);
    CHECK(texture->get_pixels()[2] == 0);
    CHECK(texture->get_pixels()[3] > 0);
    CHECK(texture->is_srgb());

    Ref<ImageTexture> linear = ImageTextureLoader::load_from_memory(
        PNG_1X1, sizeof(PNG_1X1), false, "test_1x1_linear.png");
    REQUIRE(linear.is_valid());
    CHECK_FALSE(linear->is_srgb());
}

TEST_CASE("ImageTextureLoader: 非法字节返回空引用")
{
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    CHECK(ImageTextureLoader::load_from_memory(garbage, sizeof(garbage), true, "garbage.png").is_null());
    CHECK(ImageTextureLoader::load_from_memory(nullptr, 0, true, "empty.png").is_null());
}

TEST_CASE("ImageTextureLoader: load 走磁盘读取路径")
{
    write_png_file();

    ImageTextureLoader loader;
    Ref<Resource> resource = loader.load(TEXTURE_TEST_PATH);
    std::remove(TEXTURE_TEST_PATH);

    REQUIRE(resource.is_valid());
    Ref<ImageTexture> texture = resource.cast<ImageTexture>();
    REQUIRE(texture.is_valid());
    CHECK(texture->get_width() == 1);
    CHECK(texture->get_height() == 1);
    // 文件加载默认按线性数据处理，由材质/MTL 层按槽位覆盖 sRGB
    CHECK_FALSE(texture->is_srgb());

    // 资源不存在时返回空 Ref
    CHECK(loader.load("orca_missing_texture_for_test.png").is_null());
}

TEST_CASE("ImageTexture: sampler setter")
{
    Ref<ImageTexture> texture = ImageTexture::get_default_white(2, 2, 4);

    CHECK(texture->is_repeat());
    CHECK(texture->is_linear_filter());
    CHECK(texture->is_gen_mipmap());

    texture->set_repeat(false);
    texture->set_linear_filter(false);
    texture->set_gen_mipmap(false);
    CHECK_FALSE(texture->is_repeat());
    CHECK_FALSE(texture->is_linear_filter());
    CHECK_FALSE(texture->is_gen_mipmap());

    // duplicate 会带上 sampler 设置
    Ref<Resource> copy = texture->duplicate();
    REQUIRE(copy.is_valid());
    Ref<ImageTexture> copied_texture = copy.cast<ImageTexture>();
    REQUIRE(copied_texture.is_valid());
    CHECK_FALSE(copied_texture->is_repeat());
    CHECK_FALSE(copied_texture->is_linear_filter());
    CHECK_FALSE(copied_texture->is_gen_mipmap());
}
