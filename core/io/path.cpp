#include "path.h"

#include <cstdlib>
#include <filesystem>

namespace
{
    constexpr const char *RES_URL = "res://";
    constexpr const char *USER_URL = "user://";

    /// @brief 将路径中的反斜杠统一替换为正斜杠
    std::string to_forward_slashes(std::string path)
    {
        for (char &c : path)
        {
            if (c == '\\') c = '/';
        }
        return path;
    }

    /// @brief 获取运行目录（当前工作目录）
    std::filesystem::path get_run_dir()
    {
        std::error_code error;
        const std::filesystem::path cwd = std::filesystem::current_path(error);
        if (error) return std::filesystem::path(".");
        return cwd;
    }

    /// @brief 获取用户根目录
    std::filesystem::path get_user_dir()
    {
        const char *home = std::getenv("HOME");
        if (home != nullptr && home[0] != '\0') return std::filesystem::path(home);

#ifdef _WIN32
        const char *user_profile = std::getenv("USERPROFILE");
        if (user_profile != nullptr && user_profile[0] != '\0') return std::filesystem::path(user_profile);

        const char *home_drive = std::getenv("HOMEDRIVE");
        const char *home_path = std::getenv("HOMEPATH");
        if (home_drive != nullptr && home_path != nullptr)
        {
            return std::filesystem::path(std::string(home_drive) + home_path);
        }
#endif
        return get_run_dir();
    }

    /// @brief 相对路径转绝对路径并规范化
    std::string make_absolute(const std::filesystem::path &path)
    {
        std::filesystem::path absolute = path;
        if (!absolute.is_absolute())
        {
            absolute = get_run_dir() / absolute;
        }
        return to_forward_slashes(absolute.lexically_normal().string());
    }
}

const std::string Path::replace_url(const std::string url)
{
    if (url.compare(0, std::char_traits<char>::length(RES_URL), RES_URL) == 0)
    {
        const std::string relative = url.substr(std::char_traits<char>::length(RES_URL));
        return make_absolute(get_run_dir() / "resources" / relative);
    }

    if (url.compare(0, std::char_traits<char>::length(USER_URL), USER_URL) == 0)
    {
        const std::string relative = url.substr(std::char_traits<char>::length(USER_URL));
        return make_absolute(get_user_dir() / relative);
    }

    return get_absolute_path(url);
}

const std::string Path::get_absolute_path(const std::string path)
{
    return make_absolute(std::filesystem::path(path));
}

const std::string Path::get_extension(const std::string path)
{
    // 只在文件名部分查找后缀，忽略目录名中的点
    const size_t separator = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
    {
        return std::string();
    }
    // 点在目录名里（如 "dir.d/file"），没有后缀
    if (separator != std::string::npos && dot < separator)
    {
        return std::string();
    }
    // 隐藏文件（如 ".gitignore"）视为没有后缀
    const size_t name_begin = separator == std::string::npos ? 0 : separator + 1;
    if (dot == name_begin)
    {
        return std::string();
    }
    return path.substr(dot);
}
