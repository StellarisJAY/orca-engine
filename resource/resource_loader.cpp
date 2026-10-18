#include "resource_loader.h"
#include "io/path.h"
#include "os/memory.h"
#include "loaders/obj_loader.h"

void ResourceLoader::create_singleton()
{
    singleton = memnew(ResourceLoader);
    singleton->register_loader(memnew(ObjMeshLoader));
}

void ResourceLoader::destroy_singleton()
{
    memdelete(singleton);
}

void ResourceLoader::register_loader(ResourceFormatLoader *loader)
{
    loaders.insert(std::pair<std::string, ResourceFormatLoader*>(loader->format_extension(), loader));
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
    for(auto pair : loaders)
    {
        memdelete(pair.second);
    }
    loaders.clear();
}
