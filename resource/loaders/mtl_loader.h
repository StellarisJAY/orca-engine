#pragma once

#include "resource_loader.h"

/// @brief 加载 .mtl 文件，返回包含多个 StandardMaterial 的 MaterialLibrary。
class MtlLoader : public ResourceFormatLoader
{
public:
    std::vector<std::string> format_extensions() const override { return {".mtl"}; }
    Ref<Resource> load(const std::string url) override;
};
