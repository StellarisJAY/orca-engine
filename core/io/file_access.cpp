#include "file_access.h"

int32_t FileAccess::get_int32()
{
    return 0;
}

int64_t FileAccess::get_int64()
{
    return 0;
}

int8_t FileAccess::get_int8()
{
    return 0;
}

int16_t FileAccess::get_int16()
{
    return 0;
}

float FileAccess::get_float()
{
    return 0.0f;
}

double FileAccess::get_double()
{
    return 0.0;
}

std::string FileAccess::get_as_text() const
{
    return std::string();
}

FileAccess FileAccess::open(const std::string path, ModeFlag flag)
{
    FILE *file;
    switch(flag)
    {
        case ModeFlag::READ:
            file = fopen(path.c_str(), "r");
            break;
        case ModeFlag::WRITE:
            file = fopen(path.c_str(), "w");
            break;
        case ModeFlag::READ_WRITE:
            file = fopen(path.c_str(), "rw");
            break;
    }
    return FileAccess(file);
}

bool FileAccess::file_exists(const std::string path)
{
    return false;
}

std::string FileAccess::get_file_as_string(const std::string path)
{
    return std::string();
}

void FileAccess::close()
{
    if(file != nullptr) fclose(file);
}

void FileAccess::flush()
{
    if(file != nullptr) fflush(file);
}

bool FileAccess::eof() const
{
    return file == nullptr || feof(file);
}

size_t FileAccess::get_length() const
{
    return size_t();
}

size_t FileAccess::get_position() const
{
    if(file == nullptr) return 0;
    fpos_t position = 0;
    fgetpos(file, &position);
    return position;
}

void FileAccess::seek(size_t position)
{
   if(file != nullptr) fseek(file, position, 0);
}

void FileAccess::seek_end()
{
    
}
