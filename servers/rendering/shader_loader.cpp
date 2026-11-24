#include "shader_loader.h"
#include "io/file_access.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr const char *RUNTIME_SHADER_ROOT = "res://shaders/";
    // Vulkan 变体由 CMake 的 orca-shader-spirv 目标编译到构建目录，没有源码树回退。
    constexpr const char *RUNTIME_SPIRV_ROOT = "res://shaders-vulkan/";

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

    std::string text = FileAccess::get_file_as_text(path);
    // Windows 编辑器可能给 GLSL 加上 UTF-8 BOM，GL 编译器会把它当成非法字符
    // 报 "illegal non-ASCII character"；这里统一剥掉。
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
    {
        text.erase(0, 3);
    }
    return text;
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

std::vector<uint8_t> ShaderLoader::resolve_and_read_binary(const std::string &shader_name, const std::string &file_name)
{
    const std::string path = join_path(RUNTIME_SPIRV_ROOT, shader_name, file_name);
    std::vector<uint8_t> bytes;
    if (!FileAccess::read_file_bytes(path, bytes))
    {
        std::printf("[ERROR] SPIR-V shader file not found: %s\n", path.c_str());
        return {};
    }
    return bytes;
}

ShaderBinarySource ShaderLoader::load_spirv(const std::string &shader_name)
{
    ShaderBinarySource source;
    source.vertex = resolve_and_read_binary(shader_name, "vert.spv");
    source.fragment = resolve_and_read_binary(shader_name, "frag.spv");

    if (!source.is_valid())
    {
        std::printf("[ERROR] failed to load SPIR-V shader '%s'; build the project (orca-shader-spirv) "
                    "and run from the build directory\n",
                    shader_name.c_str());
    }
    return source;
}
