#pragma once

#include "resource_loader.h"

class ObjMeshLoader: public ResourceFormatLoader
{
public:
    Ref<Resource> load(const std::string url) override;
    const std::string format_extension() const override { return ".obj"; }
};