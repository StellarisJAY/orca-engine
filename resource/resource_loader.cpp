#include "resource_loader.h"
#include "io/path.h"
#include "os/memory.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include "loaders/obj_loader.h"
#include "loaders/image_texture_loader.h"
#include "loaders/mtl_loader.h"
#include "loaders/gltf_loader.h"

void ResourceLoader::create_singleton()
{
    singleton = memnew(ResourceLoader);
    singleton->register_loader(memnew(ObjMeshLoader));
    singleton->register_loader(memnew(ImageTextureLoader));
    singleton->register_loader(memnew(MtlLoader));
    singleton->register_loader(memnew(GLTFLoader));
}

void ResourceLoader::destroy_singleton()
{
    memdelete(singleton);
}

void ResourceLoader::register_loader(ResourceFormatLoader *loader)
{
    for(const std::string &extension : loader->format_extensions())
    {
        loaders.insert(std::pair<std::string, ResourceFormatLoader*>(extension, loader));
    }
    owned_loaders.push_back(loader);
}

Ref<Resource> ResourceLoader::load(const std::string url)
{
    ResourceCache *cache = ResourceCache::get_singleton();
    if (cache != nullptr)
    {
        Ref<Resource> cached = cache->get_resource(url);
        if (cached.is_valid())
        {
            return cached;
        }
    }

    std::string ext = Path::get_extension(url);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto it = loaders.find(ext);
    if (it == loaders.end() || it->second == nullptr)
    {
        std::printf("[ERROR] no resource loader for extension \"%s\": %s\n", ext.c_str(), url.c_str());
        return Ref<Resource>(nullptr);
    }

    Ref<Resource> resource = it->second->load(url);
    if (resource.is_null())
    {
        return resource;
    }

    // 只有加载成功且拥有 URL 的资源才进入弱引用缓存。
    resource->set_url(url);
    if (cache != nullptr)
    {
        cache->save_resource(resource.ptr());
    }
    return resource;
}

ResourceLoader::~ResourceLoader()
{
    for(ResourceFormatLoader *loader : owned_loaders)
    {
        memdelete(loader);    
    }
    owned_loaders.clear();
    loaders.clear();
}
