#include "resource_cache.h"
#include "os/memory.h"
#include "resource.h"

void ResourceCache::create_singleton()
{
    singleton = memnew(ResourceCache);
}

void ResourceCache::destroy_singleton()
{
    memdelete(singleton);
}

Ref<Resource> ResourceCache::get_resource(const std::string url) const
{
    Resource *resource = cache.at(url);
    return Ref(resource);
}

void ResourceCache::save_resource(Resource *resource)
{
    cache.insert(std::pair<std::string, Resource*>(resource->get_url(), resource));
}

void ResourceCache::remove_resource(const std::string url)
{
    cache.erase(url);
}
