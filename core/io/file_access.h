#pragma once

#include "object/ref_counted.h"
#include <string>
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

    ~FileAccess() override;

    void close();
    void flush();
    bool is_eof() const;
    bool is_open() const;
    size_t get_position() const;
    void seek(size_t position);
    void seek_end();

    std::string get_as_text() const;
    std::string read_line();

private:
    size_t cursor = 0;
    FILE *file = nullptr;
};