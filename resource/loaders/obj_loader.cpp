#include "obj_loader.h"
#include "io/file_access.h"
#include "io/path.h"
#include "mesh/mesh.h"
#include "material/material.h"
#include "material/material_library.h"
#include "os/memory.h"
#include "resource_loader.h"
#include "math/vector2.h"
#include "math/vector3.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    /// @brief 面顶点引用：(position, uv, normal) 三个 0 基索引，-1 表示缺省。
    using VertexKey = std::tuple<int, int, int>;

    struct VertexKeyHash
    {
        std::size_t operator()(const VertexKey &key) const
        {
            std::size_t seed = std::hash<int>()(std::get<0>(key));
            seed ^= std::hash<int>()(std::get<1>(key)) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            seed ^= std::hash<int>()(std::get<2>(key)) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    /// @brief OBJ 面在写入 Mesh 之前按材质分组，每个分组最终成为一个 Surface。
    struct SurfaceBuilder
    {
        Mesh::Surface surface;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertex_map;
        std::string material_name;
        /// @brief 是否有面引用了缺失的法线；只有这类 surface 需要补算。
        bool missing_normals = false;
    };

    /// @brief 单次预扫描的计数。只用于 reserve，不参与负索引换算。
    struct ObjScanStats
    {
        std::size_t positions = 0;
        std::size_t normals = 0;
        std::size_t uvs = 0;
        std::size_t faces = 0;
        std::unordered_map<std::string, std::size_t> faces_per_material;
    };

    bool is_line_space(char c)
    {
        return c == ' ' || c == '\t' || c == '\r';
    }

    const char *skip_line_space(const char *p, const char *end)
    {
        while (p < end && is_line_space(*p)) ++p;
        return p;
    }

    /// @brief 判断一个完整关键字，要求后面是空白或行尾，避免把 usemtlfoo 当成 usemtl。
    bool matches_keyword(const char *p, const char *end, const char *keyword)
    {
        const std::size_t length = std::strlen(keyword);
        if (static_cast<std::size_t>(end - p) < length) return false;
        if (std::memcmp(p, keyword, length) != 0) return false;
        return p + length == end || is_line_space(p[length]);
    }

    std::string trim_range(const char *begin, const char *end)
    {
        begin = skip_line_space(begin, end);
        while (end > begin && is_line_space(end[-1])) --end;
        return std::string(begin, end);
    }

    ObjScanStats scan_obj(const std::string &text)
    {
        ObjScanStats stats;

        const char *p = text.data();
        const char *end = p + text.size();

        std::string current_material;
        std::size_t current_faces = 0;
        bool has_current = false;

        auto flush_current_material = [&]() {
            if (!has_current) return;
            stats.faces_per_material[current_material] += current_faces;
        };

        while (p < end)
        {
            const char *line_end = static_cast<const char *>(
                std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            if (line_end == nullptr) line_end = end;

            const char *q = skip_line_space(p, line_end);
            if (q < line_end && *q != '#')
            {
                if (*q == 'v' && q + 1 < line_end)
                {
                    if (is_line_space(q[1]))
                    {
                        ++stats.positions;
                    }
                    else if (q[1] == 'n' && q + 2 < line_end && is_line_space(q[2]))
                    {
                        ++stats.normals;
                    }
                    else if (q[1] == 't' && q + 2 < line_end && is_line_space(q[2]))
                    {
                        ++stats.uvs;
                    }
                }
                else if (*q == 'f' && q + 1 < line_end && is_line_space(q[1]))
                {
                    ++stats.faces;
                    ++current_faces;
                    has_current = true;
                }
                else if (*q == 'u' && matches_keyword(q, line_end, "usemtl"))
                {
                    flush_current_material();
                    current_material = trim_range(q + 6, line_end);
                    current_faces = 0;
                    has_current = true;
                }
            }

            p = (line_end < end) ? line_end + 1 : end;
        }

        flush_current_material();
        return stats;
    }

    std::string trim(const std::string &value)
    {
        const size_t begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return std::string();
        const size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
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

    /// @brief 解析 OBJ 单行中的浮点数，缺省时返回 0。
    real_t parse_real(std::istringstream &stream)
    {
        real_t value = 0;
        stream >> value;
        return value;
    }

    /// @brief 解析一个面顶点引用 token，例如 "3"、"3/4"、"3//5"、"3/4/5"。
    /// OBJ 索引从 1 开始，负数表示相对当前列表末尾的索引；统一转换为 0 基索引。
    /// 返回 false 表示 token 非法。
    bool parse_face_vertex(const std::string &token, int position_count, int uv_count, int normal_count,
                           int &out_position, int &out_uv, int &out_normal)
    {
        out_position = -1;
        out_uv = -1;
        out_normal = -1;

        const char *ptr = token.c_str();
        char *end = nullptr;

        long value = std::strtol(ptr, &end, 10);
        if (end == ptr) return false;
        out_position = value > 0 ? static_cast<int>(value) - 1 : position_count + static_cast<int>(value);

        if (*end != '/') return true; // 仅位置：v
        ++end;                        // 跳过 '/'

        if (*end != '/' && *end != '\0')
        {
            ptr = end;
            value = std::strtol(ptr, &end, 10);
            out_uv = value > 0 ? static_cast<int>(value) - 1 : uv_count + static_cast<int>(value);
        }

        if (*end != '/') return true; // 位置 / UV：v/vt
        ++end;

        if (*end != '\0')
        {
            ptr = end;
            value = std::strtol(ptr, &end, 10);
            out_normal = value > 0 ? static_cast<int>(value) - 1 : normal_count + static_cast<int>(value);
        }

        return true;
    }

    /// @brief 只给缺失法线（当前为零向量）的顶点补平滑法线，保留 OBJ 自带的法线。
    void compute_missing_vertex_normals(Mesh::Surface &surface)
    {
        const std::size_t vertex_count = surface.vertices.size();
        if (vertex_count == 0 || surface.normals.size() != vertex_count) return;

        std::vector<Vector3> sums(vertex_count);
        for (std::size_t i = 0; i + 2 < surface.indices.size(); i += 3)
        {
            const uint32_t a = surface.indices[i];
            const uint32_t b = surface.indices[i + 1];
            const uint32_t c = surface.indices[i + 2];
            if (a >= vertex_count || b >= vertex_count || c >= vertex_count) continue;

            const Vector3 face_normal =
                (surface.vertices[b] - surface.vertices[a]).cross(surface.vertices[c] - surface.vertices[a]);

            sums[a] += face_normal;
            sums[b] += face_normal;
            sums[c] += face_normal;
        }

        for (std::size_t i = 0; i < vertex_count; ++i)
        {
            if (surface.normals[i].length_squared() > 1e-12f) continue;
            surface.normals[i] = sums[i].normalized();
        }
    }
} // namespace

Ref<Resource> ObjMeshLoader::load(const std::string url)
{
    const std::string text = FileAccess::get_file_as_text(url);
    if (text.empty())
    {
        std::printf("[ERROR] failed to open or empty .obj file: %s\n", url.c_str());
        return Ref<Resource>(nullptr);
    }

    const ObjScanStats stats = scan_obj(text);

    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    positions.reserve(stats.positions);
    normals.reserve(stats.normals);
    uvs.reserve(stats.uvs);

    std::vector<SurfaceBuilder> builders;
    builders.reserve(stats.faces_per_material.size());

    std::unordered_map<std::string, uint32_t> material_surfaces;
    material_surfaces.reserve(stats.faces_per_material.size());

    std::unordered_map<std::string, Ref<StandardMaterial>> materials;
    materials.reserve(stats.faces_per_material.size());

    std::string current_material_name;
    uint32_t current_surface = UINT32_MAX;

    auto ensure_surface = [&](const std::string &material_name) -> SurfaceBuilder & {
        auto it = material_surfaces.find(material_name);
        if (it != material_surfaces.end())
        {
            current_surface = it->second;
            return builders[current_surface];
        }

        current_surface = static_cast<uint32_t>(builders.size());
        material_surfaces.emplace(material_name, current_surface);
        builders.emplace_back();

        SurfaceBuilder &builder = builders.back();
        builder.material_name = material_name;

        auto count_it = stats.faces_per_material.find(material_name);
        if (count_it != stats.faces_per_material.end() && count_it->second > 0)
        {
            const std::size_t face_count = count_it->second;
            builder.surface.indices.reserve(face_count * 3);

            // 唯一顶点数不会超过该 surface 的角点数，也不会超过全局位置数。
            const std::size_t vertex_hint = std::min(face_count, stats.positions);
            builder.surface.vertices.reserve(vertex_hint);
            builder.surface.normals.reserve(vertex_hint);
            builder.surface.uvs.reserve(vertex_hint);
        }

        return builder;
    };

    // 加载 mtllib 指定的材质库；失败时只打印警告，OBJ 几何仍然可用。
    auto load_material_library = [&](const std::string &mtl_path) {
        ResourceLoader *loader = ResourceLoader::get_singleton();
        if (loader == nullptr)
        {
            std::printf("[WARN] OBJ mtllib skipped, ResourceLoader is not initialized: %s\n", mtl_path.c_str());
            return;
        }

        const std::string mtl_url = Path::join(Path::get_base_dir(url), mtl_path);
        Ref<Resource> resource = loader->load(mtl_url);
        Ref<MaterialLibrary> library = resource.cast<MaterialLibrary>();
        if (library.is_null())
        {
            std::printf("[WARN] failed to load OBJ material library: %s\n", mtl_url.c_str());
            return;
        }

        for (const auto &entry : library->get_materials())
        {
            materials[entry.first] = entry.second;
        }
    };

    std::istringstream file(text);
    std::string line;
    std::string token;
    std::vector<VertexKey> face;
    face.reserve(8);

    while (std::getline(file, line))
    {
        if (line.empty() || line.at(0) == '#') continue;

        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;

        if (keyword == "v")
        {
            const real_t x = parse_real(stream);
            const real_t y = parse_real(stream);
            const real_t z = parse_real(stream);
            positions.emplace_back(x, y, z);
        }
        else if (keyword == "vn")
        {
            const real_t x = parse_real(stream);
            const real_t y = parse_real(stream);
            const real_t z = parse_real(stream);
            normals.emplace_back(x, y, z);
        }
        else if (keyword == "vt")
        {
            const real_t u = parse_real(stream);
            const real_t v = parse_real(stream);
            uvs.emplace_back(u, v);
        }
        else if (keyword == "mtllib")
        {
            std::string rest;
            std::getline(stream, rest);
            rest = trim(rest);

            if (!rest.empty() && (rest.front() == '"' || rest.front() == '\''))
            {
                load_material_library(unquote(rest));
            }
            else
            {
                std::istringstream names(rest);
                std::string mtl_name;
                while (names >> mtl_name)
                {
                    load_material_library(mtl_name);
                }
            }
        }
        else if (keyword == "usemtl")
        {
            std::string name;
            std::getline(stream, name);
            current_material_name = trim(name);
            // 下一批面到来时再创建 surface，避免 usemtl 后面没有面时产生空 surface。
            current_surface = UINT32_MAX;
        }
        else if (keyword == "f")
        {
            face.clear();
            while (stream >> token)
            {
                int position = -1;
                int uv = -1;
                int normal = -1;
                if (!parse_face_vertex(token, static_cast<int>(positions.size()), static_cast<int>(uvs.size()),
                                       static_cast<int>(normals.size()), position, uv, normal))
                {
                    continue;
                }

                // 越界引用直接丢弃，避免后续访问崩溃。
                if (position < 0 || position >= static_cast<int>(positions.size())) continue;
                if (uv < 0 || uv >= static_cast<int>(uvs.size())) uv = -1;
                if (normal < 0 || normal >= static_cast<int>(normals.size())) normal = -1;

                face.emplace_back(position, uv, normal);
            }

            // 少于 3 个有效角点的面不产生 surface，避免空 surface 被上传到 GPU。
            if (face.size() < 3) continue;

            if (current_surface == UINT32_MAX)
            {
                ensure_surface(current_material_name);
            }
            SurfaceBuilder &builder = builders[current_surface];

            for (const VertexKey &key : face)
            {
                if (std::get<2>(key) < 0)
                {
                    builder.missing_normals = true;
                    break;
                }
            }

            // 多边形面用扇形三角化： (0, i, i + 1)。
            for (std::size_t i = 1; i + 1 < face.size(); ++i)
            {
                const VertexKey triangle[3] = { face[0], face[i], face[i + 1] };
                for (const VertexKey &key : triangle)
                {
                    auto it = builder.vertex_map.find(key);
                    if (it == builder.vertex_map.end())
                    {
                        const uint32_t index = static_cast<uint32_t>(builder.surface.vertices.size());
                        builder.vertex_map.emplace(key, index);

                        builder.surface.vertices.push_back(positions[std::get<0>(key)]);
                        builder.surface.normals.push_back(std::get<2>(key) >= 0 ? normals[std::get<2>(key)] : Vector3());
                        builder.surface.uvs.push_back(std::get<1>(key) >= 0 ? uvs[std::get<1>(key)] : Vector2());
                        builder.surface.indices.push_back(index);
                    }
                    else
                    {
                        builder.surface.indices.push_back(it->second);
                    }
                }
            }
        }
        // 其余关键字 (o / g / s ...) 暂不处理。
    }

    Mesh *mesh = memnew(Mesh);
    mesh->reserve_surfaces(builders.size());

    for (SurfaceBuilder &builder : builders)
    {
        if (builder.surface.indices.empty()) continue;

        if (builder.missing_normals)
        {
            compute_missing_vertex_normals(builder.surface);
        }

        if (!builder.material_name.empty())
        {
            auto it = materials.find(builder.material_name);
            if (it != materials.end())
            {
                builder.surface.material = it->second;
            }
            else
            {
                std::printf("[WARN] OBJ references unknown material \"%s\"\n", builder.material_name.c_str());
            }
        }

        // 移动而不是拷贝，避免加载期再复制一整份顶点/索引数据。
        mesh->add_surface(std::move(builder.surface));
    }

    // 加载顶点完成，按材质需要预计算切线空间
    mesh->compute_tangents();
    return Ref<Resource>(mesh);
}
