#include "logging.h"
#include "os/memory.h"

void Logging::set_level(Level level)
{
    if(singleton) singleton->current_level = level;
}

Logging::Level Logging::get_level()
{
    return singleton ? singleton->current_level : Logging::Level::INFO;
}

void Logging::debug(const std::string message)
{
    if(singleton && singleton->current_level == Logging::Level::DEBUG)
    {
        std::printf("[DEBUG] [orca-engine]: %s\n", message.c_str());
    }
}

void Logging::info(const std::string message)
{
    if(singleton && singleton->current_level <= Logging::Level::INFO)
    {
        std::printf("[INFO] [orca-engine]: %s\n", message.c_str());
    }
}

void Logging::error(const std::string message)
{
    if(singleton && singleton->current_level <= Logging::Level::ERROR)
    {
        std::printf("[ERROR] [orca-engine]: %s\n", message.c_str());
    }
}

void Logging::warn(const std::string message)
{
    if(singleton && singleton->current_level <= Logging::Level::WARN)
    {
        std::printf("[WARN] [orca-engine]: %s\n", message.c_str());
    }
}

void Logging::init()
{
    if(singleton == nullptr)
    {
        singleton = memnew(Logging);
    }
}

void Logging::destroy()
{
    if(singleton)
    {
        memdelete(singleton);
    }
}
