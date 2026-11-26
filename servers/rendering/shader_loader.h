#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// @brief 一份完整的 shader 源码（顶点 + 片元 + 几何）
struct ShaderSource
{
    std::string vertex;
    std::string fragment;
    std::string geometry;

    bool is_valid() const { return !vertex.empty() && !fragment.empty(); }
    bool has_geometry() const { return !geometry.empty(); }
};

/// @brief 一份已编译的 SPIR-V shader 二进制（顶点 + 片元）。
///
/// 由 CMake 的 `orca-shader-spirv` 目标用 glslc 从 `shaders-vulkan/` 编译而来，
/// 输出到 `<build>/resources/shaders-vulkan/<name>/{vert,frag,geo}.spv`。
struct ShaderBinarySource
{
    std::vector<uint8_t> vertex;
    std::vector<uint8_t> fragment;
    std::vector<uint8_t> geometry;

    bool is_valid() const { return !vertex.empty() && !fragment.empty(); }
    bool has_geometry() const { return !geometry.empty(); }
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
    /// @brief 加载 shaders/<name>/{vert,frag,geo}.glsl，失败时返回的 ShaderSource 不合法
    static ShaderSource load(const std::string &shader_name);

    /// @brief 加载 Vulkan 专用的已编译 shader：res://shaders-vulkan/<name>/{vert,frag,geo}.spv。
    ///
    /// 该目录由构建生成（glslc），没有源码树回退；调用方必须从构建目录运行。
    static ShaderBinarySource load_spirv(const std::string &shader_name);

    /// @brief 按 res:// 规则读取文本，文件不存在或读取失败返回空串
    static std::string read_text_file(const std::string &path);

private:
    /// @brief 依次尝试所有候选根目录，返回第一个存在的文件内容
    static std::string resolve_and_read(const std::string &shader_name, const std::string &file_name);

    /// @brief 读取 SPIR-V 二进制；失败返回空 vector 并打印尝试的路径
    static std::vector<uint8_t> resolve_and_read_binary(const std::string &shader_name, const std::string &file_name);
};
