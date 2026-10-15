#pragma once

#include "object/ref_counted.h"
#include <string>

class DirAccess: public RefCounted
{
public:
    static DirAccess open(const std::string path);
public:
    bool dir_exists(const std::string path) const;
    bool file_exists(const std::string path) const;
};