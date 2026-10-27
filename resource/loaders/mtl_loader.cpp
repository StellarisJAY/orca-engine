#include "mtl_loader.h"
#include "io/file_access.h"
#include "io/path.h"
#include "material/material_library.h"
#include "os/memory.h"
#include "math/color.h"
#include "math/vector2.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_map>

namespace
{
    std::string trim(const std::string &value)
    {
        const size_t begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return std::string();
        const size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    std::string to_lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string unquote(const std::string &value)
    {
        if (value.size() >= 2)
        {
            const char begin = value.front();
            const char end = value.back();
            if ((begin == '"' && end == '"') || (begin == '\'' && end == '\''))
                return value.substr(1, value.size() - 2);
        }
        return value;
    }

    float clamp01(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }

    uint8_t to_byte(float value)
    {
        return static_cast<uint8_t>(clamp01(value) * 255.0f + 0.5f);
    }

    Color color_from_rgb(float r, float g, float b, float a = 1.0f)
    {
        return Color(to_byte(r), to_byte(g), to_byte(b), to_byte(a));
    }

    bool parse_float(std::istringstream &stream, float &value)
    {
        return static_cast<bool>(stream >> value);
    }

    bool parse_rgb(std::istringstream &stream, Color &out)
    {
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (!(stream >> r >> g >> b)) return false;
        out = color_from_rgb(r, g, b);
        return true;
    }

    struct MapDescription
    {
        std::string path;
        Vector2 uv_scale = {1.0f, 1.0f};
        bool has_uv_scale = false;
        float bump_scale = 1.0f;
        bool has_bump_scale = false;
    };

    /// @brief 解析 MTL 贴图行，跳过常见选项，返回路径和 -s / -bm。
    bool parse_map_line(std::istringstream &stream, MapDescription &out)
    {
        std::string token;
        while (stream >> token)
        {
            if (token.empty()) continue;

            if (token.front() != '-')
            {
                // 文件的剩余部分都属于路径，允许路径中包含空格。
                std::string rest;
                std::getline(stream, rest);
                out.path = trim(token + rest);
                out.path = unquote(out.path);
                return !out.path.empty();
            }

            const std::string option = to_lower(token);
            if (option == "-s")
            {
                float u = 1.0f, v = 1.0f, w = 1.0f;
                if (!(stream >> u >> v >> w)) return false;
                out.uv_scale = Vector2(u, v);
                out.has_uv_scale = true;
            }
            else if (option == "-bm")
            {
                if (!(stream >> out.bump_scale)) return false;
                out.has_bump_scale = true;
            }
            else if (option == "-o" || option == "-t")
            {
                // 这些选项通常带 1~3 个浮点参数，这里按常见的 3 个处理。
                float x = 0.0f, y = 0.0f, z = 0.0f;
                stream >> x >> y >> z;
            }
            else if (option == "-mm")
            {
                float x = 0.0f, y = 0.0f;
                stream >> x >> y;
            }
            else if (option == "-clamp" || option == "-blendu" || option == "-blendv" ||
                     option == "-boost" || option == "-texres" || option == "-imfchan" ||
                     option == "-type")
            {
                std::string dummy;
                stream >> dummy;
            }
            else
            {
                // 未知选项不做猜测，避免把真正的路径吞掉。
                return false;
            }
        }
        return false;
    }

    Ref<ImageTexture> load_texture(const std::string &mtl_url, const std::string &path,
                                   bool srgb, const char *slot_name)
    {
        ResourceLoader *loader = ResourceLoader::get_singleton();
        if (loader == nullptr)
        {
            std::printf("[WARN] MTL texture skipped, ResourceLoader is not initialized: %s\n", path.c_str());
            return Ref<ImageTexture>(nullptr);
        }

        const std::string texture_url = Path::join(Path::get_base_dir(mtl_url), path);
        Ref<Resource> resource = loader->load(texture_url);
        Ref<ImageTexture> texture = resource.cast<ImageTexture>();
        if (texture.is_null())
        {
            std::printf("[WARN] MTL %s texture failed to load: %s\n", slot_name, texture_url.c_str());
            return Ref<ImageTexture>(nullptr);
        }

        // 贴图最终用途决定是否做 sRGB 解码：albedo 是颜色，其他贴图是数据。
        texture->set_srgb(srgb);
        return texture;
    }
} // namespace

Ref<Resource> MtlLoader::load(const std::string url)
{
    const std::string text = FileAccess::get_file_as_text(url);
    if (text.empty())
    {
        std::printf("[ERROR] failed to open or empty .mtl file: %s\n", url.c_str());
        return Ref<Resource>(nullptr);
    }

    MaterialLibrary *library = memnew(MaterialLibrary);

    Ref<StandardMaterial> current;
    std::string current_name;
    float current_opacity = 1.0f;

    auto apply_opacity = [&]() {
        if (current.is_null()) return;
        Color albedo = current->get_albedo();
        albedo.A = to_byte(current_opacity);
        current->set_albedo(albedo);
    };

    std::istringstream file(text);
    std::string line;
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;

        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;
        const std::string key = to_lower(keyword);

        if (key == "newmtl")
        {
            std::string name;
            std::getline(stream, name);
            current_name = trim(name);
            if (current_name.empty())
            {
                std::printf("[WARN] MTL contains an unnamed material: %s\n", url.c_str());
                current = Ref<StandardMaterial>(nullptr);
                continue;
            }

            if (library->has_material(current_name))
                std::printf("[WARN] duplicate material \"%s\" in %s, the later one overrides\n",
                            current_name.c_str(), url.c_str());

            current = Ref<StandardMaterial>(memnew(StandardMaterial));
            // MTL 没有 metallic 标准字段，默认按非金属处理；Pm / map_Pm 会覆盖。
            current->set_metallic(0.0f);
            current->set_emission(Color(0, 0, 0, 255));
            current_opacity = 1.0f;
            library->add_material(current_name, current);
            continue;
        }

        if (current.is_null()) continue;

        if (key == "kd")
        {
            Color color;
            if (parse_rgb(stream, color))
            {
                current->set_albedo(color);
                apply_opacity();
            }
        }
        else if (key == "ka")
        {
            Color color;
            if (parse_rgb(stream, color)) current->set_ambient(color);
        }
        else if (key == "ks")
        {
            Color color;
            if (parse_rgb(stream, color)) current->set_specular(color);
        }
        else if (key == "ke")
        {
            Color color;
            if (parse_rgb(stream, color)) current->set_emission(color);
        }
        else if (key == "ns")
        {
            float ns = 0.0f;
            if (parse_float(stream, ns))
            {
                // Blinn-Phong 高光指数近似转换为 PBR 粗糙度。
                const float roughness = ns > 0.0f ? std::sqrt(2.0f / (ns + 2.0f)) : 1.0f;
                current->set_roughness(clamp01(roughness));
            }
        }
        else if (key == "pr")
        {
            float roughness = 1.0f;
            if (parse_float(stream, roughness)) current->set_roughness(clamp01(roughness));
        }
        else if (key == "pm")
        {
            float metallic = 0.0f;
            if (parse_float(stream, metallic)) current->set_metallic(clamp01(metallic));
        }
        else if (key == "d")
        {
            float opacity = 1.0f;
            if (parse_float(stream, opacity))
            {
                current_opacity = clamp01(opacity);
                apply_opacity();
            }
        }
        else if (key == "tr")
        {
            float transparency = 0.0f;
            if (parse_float(stream, transparency))
            {
                current_opacity = 1.0f - clamp01(transparency);
                apply_opacity();
            }
        }
        else if (key == "map_kd" || key == "map_ka" || key == "map_ks" ||
                 key == "map_ns" || key == "map_pr" || key == "map_pm" ||
                 key == "norm" || key == "map_bump" || key == "bump" ||
                 key == "map_ke" || key == "map_d")
        {
            MapDescription map;
            if (!parse_map_line(stream, map))
            {
                std::printf("[WARN] failed to parse MTL texture line \"%s\" in %s\n", line.c_str(), url.c_str());
                continue;
            }

            if (map.has_uv_scale) current->set_uv_scale(map.uv_scale);

            if (key == "map_kd")
            {
                current->set_texture_albedo(load_texture(url, map.path, true, "albedo"));
            }
            else if (key == "map_ka")
            {
                current->set_texture_ao(load_texture(url, map.path, false, "ao"));
            }
            else if (key == "map_ns" || key == "map_pr")
            {
                current->set_texture_roughness(load_texture(url, map.path, false, "roughness"));
            }
            else if (key == "map_pm")
            {
                current->set_texture_metallic(load_texture(url, map.path, false, "metallic"));
            }
            else if (key == "norm")
            {
                current->set_texture_normal(load_texture(url, map.path, false, "normal"));
            }
            else if (key == "map_bump" || key == "bump")
            {
                // MTL 的 bump 是高度/凹凸图，对应 Height 槽位；-bm 作为视差强度。
                current->set_texture_height(load_texture(url, map.path, false, "height"));
                if (map.has_bump_scale) current->set_parallax_scale(map.bump_scale);
            }
            else if (key == "map_ks")
            {
                // 当前 StandardMaterial 没有 specular 贴图槽位，忽略但保留解析路径。
                std::printf("[WARN] MTL map_Ks is not supported yet: %s\n", map.path.c_str());
            }
            // map_ke / map_d 当前没有对应纹理槽位，静默忽略。
        }
        // 其余字段（Ni / illum / Tf / map_refl ...）暂不处理。
    }

    return Ref<Resource>(library);
}
