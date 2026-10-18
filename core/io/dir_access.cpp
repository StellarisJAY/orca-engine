#include "dir_access.h"

DirAccess DirAccess::open(const std::string path)
{
    return DirAccess();
}

bool DirAccess::dir_exists(const std::string path)
{
    return false;
}

bool DirAccess::is_dir_absolute(const std::string path)
{
    return false;
}

bool DirAccess::file_exists(const std::string path) const
{
    return false;
}

bool DirAccess::is_file(const std::string path) const
{
    return false;
}

bool DirAccess::is_dir(const std::string path) const
{
    return false;
}

void DirAccess::close()
{
}

bool DirAccess::mkdir(const std::string name)
{
    return false;
}
