#pragma once

#include "resource_loader.h"

class GLTFLoader: public ResourceFormatLoader
{
public:
    Ref<Resource> load(const std::string url) override;
    std::vector<std::string> format_extensions() const override { return {".glb", ".gltf"}; }
};