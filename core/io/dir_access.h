#pragma once

#include "object/ref_counted.h"
#include <string>

class DirAccess: public RefCounted
{
public:
    static DirAccess open(const std::string path);
    static bool dir_exists(const std::string path);
    static bool is_dir_absolute(const std::string path);

    bool file_exists(const std::string path) const;
    bool is_file(const std::string path) const;
    bool is_dir(const std::string path) const;
    void close();
    bool mkdir(const std::string name);
    bool remove_file(const std::string file);
    bool remove_dir(const std::string dir);
};