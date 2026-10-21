#include "shader_loader.h"
#include "io/file_access.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr const char *RUNTIME_SHADER_ROOT = "res://shaders/";

    std::string join_path(const std::string &root, const std::string &name, const std::string &file)
    {
        std::string path = root;
        if (!path.empty() && path.back() != '/')
            path += '/';
        path += name;
        path += '/';
        path += file;
        return path;
    }

    std::vector<std::string> get_roots()
    {
        std::vector<std::string> roots;
        // 运行时资源目录：<运行目录>/resources/shaders/
        roots.emplace_back(RUNTIME_SHADER_ROOT);

        // 开发期回退：直接读源码树，改完无需重新拷贝资源
#ifdef ORCA_SHADER_SOURCE_DIR
        roots.emplace_back(std::string(ORCA_SHADER_SOURCE_DIR) + "/");
#endif
        return roots;
    }
} // namespace

std::string ShaderLoader::read_text_file(const std::string &path)
{
    if (!FileAccess::file_exists(path))
        return std::string();

    return FileAccess::get_file_as_text(path);
}

std::string ShaderLoader::resolve_and_read(const std::string &shader_name, const std::string &file_name)
{
    std::vector<std::string> tried_paths;
    for (const std::string &root : get_roots())
    {
        const std::string path = join_path(root, shader_name, file_name);
        tried_paths.push_back(path);

        std::string text = read_text_file(path);
        if (!text.empty())
            return text;
    }

    // 所有候选路径都失败时打印一次，方便定位资源目录问题。
    std::printf("[ERROR] shader file not found, tried:\n");
    for (const std::string &path : tried_paths)
        std::printf("    %s\n", path.c_str());
    return std::string();
}

ShaderSource ShaderLoader::load(const std::string &shader_name)
{
    ShaderSource source;
    source.vertex = resolve_and_read(shader_name, "vert.glsl");
    source.fragment = resolve_and_read(shader_name, "frag.glsl");

    if (!source.is_valid())
    {
        std::printf("[ERROR] failed to load shader '%s'\n", shader_name.c_str());
    }
    return source;
}
