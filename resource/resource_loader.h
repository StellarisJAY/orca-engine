#pragma once

#include "resource.h"
#include <unordered_map>

class ResourceFormatLoader
{
public:
    virtual Ref<Resource> load(const std::string url) = 0;
    virtual const std::string format_extension() const = 0;
};

class ResourceLoader
{
public:
    static ResourceLoader *get_singleton() { return singleton; }
    static void create_singleton();
    static void destroy_singleton();

    void register_loader(ResourceFormatLoader *loader);
    Ref<Resource> load(const std::string url);
    ~ResourceLoader();
private:
    static ResourceLoader *singleton;
    std::unordered_map<std::string, ResourceFormatLoader*> loaders;
};

inline ResourceLoader *ResourceLoader::singleton;