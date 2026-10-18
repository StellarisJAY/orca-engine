#pragma once

#include "resource_cache.h"
#include "object/rid.h"
#include "object/ref_counted.h"
#include <string>

class Resource: public RefCounted
{
private:
    std::string url;
public:
    virtual RID get_rid() const = 0;
    virtual Ref<Resource> duplicate() const = 0;
    ~Resource();
    const std::string get_url() const { return url; }
};

inline Resource::~Resource()
{
    if(auto cache = ResourceCache::get_singleton()) {
        cache->remove_resource(url);
    }
}