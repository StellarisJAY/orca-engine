#pragma once

#include <unordered_map>
#include <string>
#include "object/ref_counted.h"

class Resource;

class ResourceCache
{
public:
    static ResourceCache *get_singleton() { return singleton; }
    static void create_singleton();
    static void destroy_singleton();
    Ref<Resource> get_resource(const std::string url) const;
    void save_resource(Resource *resource);
    void remove_resource(const std::string url);
private:
    static ResourceCache *singleton;
    std::unordered_map<std::string, Resource*> cache;
};

inline ResourceCache *ResourceCache::singleton;