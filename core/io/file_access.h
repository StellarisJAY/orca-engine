#pragma once

#include <string>
#include "object/ref_counted.h"
#include <cstdio>

class FileAccess : public RefCounted
{
    enum ModeFlag
    {
        READ,
        WRITE,
        READ_WRITE,
        WRITE_READ
    };

public:
    static FileAccess open(const std::string path, ModeFlag flag);
    static bool file_exists(const std::string path);
    static std::string get_file_as_string(const std::string path);
public:
    FileAccess(FILE *file) : file(file) {}
    void close();
    void flush();
    bool eof() const;

    size_t get_length() const;

    size_t get_position() const;
    void seek(size_t position);
    void seek_end();

    int32_t get_int32();
    int64_t get_int64();
    int8_t get_int8();
    int16_t get_int16();
    float get_float();
    double get_double();
 
    std::string get_as_text() const;

private:
    FILE *file = nullptr;
};