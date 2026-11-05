#include "doctest/doctest.h"
#include "io/file_access.h"
#include "io/path.h"
#include "os/memory.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    const char *BINARY_TEST_PATH = "orca_file_access_binary_test.bin";
    const char *EMPTY_TEST_PATH = "orca_file_access_empty_test.bin";

    void write_binary_file(const std::string &path, const std::vector<uint8_t> &bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        if (!bytes.empty())
        {
            file.write(reinterpret_cast<const char *>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        }
    }
}

TEST_CASE("FileAccess: binary buffer round trip")
{
    // 包含 \0 和非法 UTF-8 字节，验证不会按文本处理
    const std::vector<uint8_t> expected = {
        0x00, 0x01, 0x7f, 0x80, 0xff, 0x00,
        'g', 'l', 'T', 'F', 0x0d, 0x0a, 0x00, 0x42};
    write_binary_file(BINARY_TEST_PATH, expected);

    FileAccess *file = FileAccess::open(BINARY_TEST_PATH, FileAccess::READ);
    REQUIRE(file != nullptr);
    CHECK(file->is_open());
    CHECK(file->get_length() == expected.size());
    CHECK(file->get_buffer() == expected);
    // get_length/get_buffer 不应该改变读取位置
    CHECK(file->get_length() == expected.size());
    memdelete(file);

    SUBCASE("read_file_bytes 一次读完")
    {
        std::vector<uint8_t> bytes;
        CHECK(FileAccess::read_file_bytes(BINARY_TEST_PATH, bytes));
        CHECK(bytes == expected);
    }

    SUBCASE("read_file_bytes 失败时不清空输出")
    {
        std::vector<uint8_t> bytes = {1, 2, 3};
        CHECK_FALSE(FileAccess::read_file_bytes("orca_missing_file_for_test.bin", bytes));
        CHECK(bytes == std::vector<uint8_t>{1, 2, 3});
    }

    std::remove(BINARY_TEST_PATH);
}

TEST_CASE("FileAccess: empty file is valid")
{
    write_binary_file(EMPTY_TEST_PATH, {});

    FileAccess *file = FileAccess::open(EMPTY_TEST_PATH, FileAccess::READ);
    REQUIRE(file != nullptr);
    CHECK(file->get_length() == 0);
    CHECK(file->get_buffer().empty());
    memdelete(file);

    std::vector<uint8_t> bytes = {9};
    CHECK(FileAccess::read_file_bytes(EMPTY_TEST_PATH, bytes));
    CHECK(bytes.empty());

    std::remove(EMPTY_TEST_PATH);
}

TEST_CASE("Path: decode_uri")
{
    CHECK(Path::decode_uri("a%20b.png") == "a b.png");
    CHECK(Path::decode_uri("textures%2Fstone%20wall.jpg") == "textures/stone wall.jpg");
    CHECK(Path::decode_uri("%E4%B8%AD%E6%96%87.bin") == "\xE4\xB8\xAD\xE6\x96\x87.bin");
    CHECK(Path::decode_uri("plain_name.png") == "plain_name.png");

    // 非法的百分号转义原样保留
    CHECK(Path::decode_uri("100%.png") == "100%.png");
    CHECK(Path::decode_uri("a%ZZ.png") == "a%ZZ.png");
    CHECK(Path::decode_uri("%4") == "%4");

    // '+' 在 URI 文件路径里不是空格
    CHECK(Path::decode_uri("a+b.png") == "a+b.png");
}
