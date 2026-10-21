#pragma once

#include "resource_loader.h"

class ImageTextureLoader: public ResourceFormatLoader
{
public:
    std::vector<std::string> format_extensions() const override { return {"png", "jpeg", "bmp", "jpg"}; }

    Ref<Resource> load(const std::string url) override ;
};