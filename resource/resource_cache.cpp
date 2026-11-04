#include "resource_cache.h"
#include "os/memory.h"
#include "resource.h"

void ResourceCache::create_singleton()
{
    singleton = memnew(ResourceCache);
}

void ResourceCache::destroy_singleton()
{
    if (singleton == nullptr) return;
    memdelete(singleton);
    singleton = nullptr;
}

Ref<Resource> ResourceCache::get_resource(const std::string url) const
{
    auto it = cache.find(url);
    if (it == cache.end() || it->second == nullptr)
    {
        return Ref<Resource>(nullptr);
    }

    // Ref(T*) 是接管指针，不会增加引用计数；缓存是弱引用，这里必须显式加一次。
    it->second->ref();
    return Ref<Resource>(it->second);
}

void ResourceCache::save_resource(Resource *resource)
{
    if (resource == nullptr) return;

    const std::string url = resource->get_url();
    if (url.empty()) return;

    cache[url] = resource;
}

void ResourceCache::remove_resource(const std::string url, Resource *resource)
{
    auto it = cache.find(url);
    // 同一 URL 可能已经被重新加载成新实例，旧实例析构时不能误删新缓存。
    if (it != cache.end() && it->second == resource)
    {
        cache.erase(it);
    }
}
