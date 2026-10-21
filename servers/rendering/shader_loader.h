#pragma once

#include <string>

/// @brief 一份完整的 shader 源码（顶点 + 片元）
struct ShaderSource
{
    std::string vertex;
    std::string fragment;

    bool is_valid() const { return !vertex.empty() && !fragment.empty(); }
};

/// @brief 从 glsl 文件加载 shader 源码。
///
/// 目录约定：shaders/<name>/vert.glsl 与 shaders/<name>/frag.glsl。
/// 查找顺序：
///   1. res://shaders/<name>/...   运行时资源目录（<运行目录>/resources/shaders/）
///   2. ORCA_SHADER_SOURCE_DIR     源码树，开发期回退（由 CMake 注入）
class ShaderLoader
{
public:
    /// @brief 加载 shaders/<name>/{vert,frag}.glsl，失败时返回的 ShaderSource 不合法
    static ShaderSource load(const std::string &shader_name);

    /// @brief 按 res:// 规则读取文本，文件不存在或读取失败返回空串
    static std::string read_text_file(const std::string &path);

private:
    /// @brief 依次尝试所有候选根目录，返回第一个存在的文件内容
    static std::string resolve_and_read(const std::string &shader_name, const std::string &file_name);
};
