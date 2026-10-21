#include "resource_loader.h"
#include "io/path.h"
#include "os/memory.h"
#include "loaders/obj_loader.h"
#include "loaders/image_texture_loader.h"

void ResourceLoader::create_singleton()
{
    singleton = memnew(ResourceLoader);
    singleton->register_loader(memnew(ObjMeshLoader));
    singleton->register_loader(memnew(ImageTextureLoader));
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
    const std::string ext = Path::get_extension(url);
    ResourceFormatLoader *loader = loaders.at(ext);
    if(loader != nullptr) return loader->load(url);
    return Ref<Resource>(nullptr);
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
