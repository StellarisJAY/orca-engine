#pragma once

#include <string>

class Path
{
public:
    /// @brief 替换url为文件路径，如果以res://,user://开头会替换成资源目录和用户目录
    /// @param url url
    /// @return 文件路径
    static const std::string replace_url(const std::string url);

    /// @brief 获取绝对路径
    /// @param path 文件相对路径
    /// @return 绝对路径
    static const std::string get_absolute_path(const std::string path);

    /// @brief 获取文件后缀
    /// @param path 文件路径
    /// @return 文件后缀,比如:.obj,.png
    static const std::string get_extension(const std::string path);

    /// @brief 获取文件所在目录，保留 res:// / user:// 前缀
    /// @param path 文件路径或 URL
    /// @return 目录，例如 "res://models/a.obj" -> "res://models"
    static const std::string get_base_dir(const std::string path);

    /// @brief 拼接目录和相对路径；relative 为绝对路径或带协议前缀时直接返回
    /// @param dir 目录
    /// @param relative 相对路径
    /// @return 拼接后的路径
    static const std::string join(const std::string dir, const std::string relative);
};