#pragma once

#include <iostream>

class Logging
{
public:
    enum class Level
    {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };
    static void set_level(Level level);
    static Level get_level();
    static void debug(const std::string message);
    static void info(const std::string message);
    static void error(const std::string message);
    static void warn(const std::string message);
    static void init();
    static void destroy();
private:
    static Logging *singleton;
    Level current_level = Level::INFO;
};

inline Logging *Logging::singleton;