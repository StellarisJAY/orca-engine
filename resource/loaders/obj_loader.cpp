#include "obj_loader.h"
#include "io/file_access.h"
#include "mesh/mesh.h"
#include "os/memory.h"
#include "math/vector2.h"
#include "math/vector3.h"

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
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

    /// @brief 当 OBJ 未提供法线时，用三角形几何计算平滑顶点法线。
    void compute_vertex_normals(Mesh::Surface &surface)
    {
        for (Vector3 &normal : surface.normals)
        {
            normal = Vector3();
        }

        for (std::size_t i = 0; i + 2 < surface.indices.size(); i += 3)
        {
            const uint32_t a = surface.indices[i];
            const uint32_t b = surface.indices[i + 1];
            const uint32_t c = surface.indices[i + 2];

            const Vector3 face_normal =
                (surface.vertices[b] - surface.vertices[a]).cross(surface.vertices[c] - surface.vertices[a]);

            surface.normals[a] += face_normal;
            surface.normals[b] += face_normal;
            surface.normals[c] += face_normal;
        }

        for (Vector3 &normal : surface.normals)
        {
            normal.normalize();
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

    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    bool has_normals = false;

    Mesh::Surface surface;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertex_map;

    std::istringstream file(text);
    std::string line;
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
            has_normals = true;
        }
        else if (keyword == "vt")
        {
            const real_t u = parse_real(stream);
            const real_t v = parse_real(stream);
            uvs.emplace_back(u, v);
        }
        else if (keyword == "f")
        {
            std::vector<VertexKey> face;
            std::string token;
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

            // 多边形面用扇形三角化： (0, i, i + 1)。
            for (std::size_t i = 1; i + 1 < face.size(); ++i)
            {
                const VertexKey triangle[3] = { face[0], face[i], face[i + 1] };
                for (const VertexKey &key : triangle)
                {
                    auto it = vertex_map.find(key);
                    if (it == vertex_map.end())
                    {
                        const uint32_t index = static_cast<uint32_t>(surface.vertices.size());
                        vertex_map.emplace(key, index);

                        surface.vertices.push_back(positions[std::get<0>(key)]);
                        surface.normals.push_back(std::get<2>(key) >= 0 ? normals[std::get<2>(key)] : Vector3());
                        surface.uvs.push_back(std::get<1>(key) >= 0 ? uvs[std::get<1>(key)] : Vector2());
                        surface.indices.push_back(index);
                    }
                    else
                    {
                        surface.indices.push_back(it->second);
                    }
                }
            }
        }
        // 其余关键字 (o / g / s / usemtl / mtllib ...) 暂不处理。
    }

    if (!has_normals && !surface.normals.empty())
    {
        compute_vertex_normals(surface);
    }

    Mesh *mesh = memnew(Mesh);
    mesh->add_surface(surface);
    return Ref<Resource>(mesh);
}
