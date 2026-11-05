#pragma once

#include "object/ref_counted.h"
#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>

class FileAccess: public RefCounted
{
public:
    enum ModeFlag {
        READ,
        WRITE,
        READ_WRITE
    };

    static FileAccess *open(const std::string path, ModeFlag flag);
    static std::string get_file_as_text(const std::string path);
    static bool file_exists(const std::string path);

    /// @brief 一次性读取整个文件的原始字节（用于 .glb、.bin、内嵌图片等二进制资源）
    /// @param path 文件路径或 URL
    /// @param out 读出的字节；失败时不会被修改
    /// @return 读取成功返回 true
    static bool read_file_bytes(const std::string &path, std::vector<uint8_t> &out);

    ~FileAccess() override;

    void close();
    void flush();
    bool is_eof() const;
    bool is_open() const;
    size_t get_position() const;
    void seek(size_t position);
    void seek_end();

    /// @brief 文件总字节数，与当前读取位置无关
    size_t get_length() const;

    /// @brief 从当前读取位置读到文件末尾的原始字节
    std::vector<uint8_t> get_buffer() const;

    std::string get_as_text() const;
    std::string read_line();

private:
    size_t cursor = 0;
    FILE *file = nullptr;
};