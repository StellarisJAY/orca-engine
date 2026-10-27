#include "resource_loader.h"
#include "io/path.h"
#include "os/memory.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include "loaders/obj_loader.h"
#include "loaders/image_texture_loader.h"
#include "loaders/mtl_loader.h"

void ResourceLoader::create_singleton()
{
    singleton = memnew(ResourceLoader);
    singleton->register_loader(memnew(ObjMeshLoader));
    singleton->register_loader(memnew(ImageTextureLoader));
    singleton->register_loader(memnew(MtlLoader));
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
    std::string ext = Path::get_extension(url);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto it = loaders.find(ext);
    if (it == loaders.end() || it->second == nullptr)
    {
        std::printf("[ERROR] no resource loader for extension \"%s\": %s\n", ext.c_str(), url.c_str());
        return Ref<Resource>(nullptr);
    }

    return it->second->load(url);
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
