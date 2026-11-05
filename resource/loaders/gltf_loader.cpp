#include "gltf_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "io/file_access.h"
#include "io/path.h"
#include "loaders/image_texture_loader.h"
#include "material/material.h"
#include "mesh/mesh.h"
#include "os/memory.h"
#include "scene/scene.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    // ---------------------------------------------------------------------
    // cgltf 文件回调：外部 .bin 必须走 FileAccess/Path，不能直接 fopen，
    // 否则 res:// 与百分号编码会失效。cgltf 会为每个 file 调用 release 释放缓冲区，
    // 因此 read 返回的数据必须独立分配，不能复用同一个 vector。
    // ---------------------------------------------------------------------
    cgltf_result gltf_file_read(const struct cgltf_memory_options *memory_options,
                                const struct cgltf_file_options *file_options,
                                const char *path, cgltf_size *size, void **data)
    {
        (void)memory_options;
        (void)file_options;

        const std::string decoded_path = Path::decode_uri(path);
        std::vector<uint8_t> bytes;
        if (!FileAccess::read_file_bytes(decoded_path, bytes))
        {
            std::printf("[ERROR] gltf loader can't read external buffer: %s\n", decoded_path.c_str());
            return cgltf_result_file_not_found;
        }

        void *buffer = std::malloc(bytes.empty() ? 1 : bytes.size());
        if (buffer == nullptr)
        {
            return cgltf_result_out_of_memory;
        }
        if (!bytes.empty())
        {
            std::memcpy(buffer, bytes.data(), bytes.size());
        }

        *size = static_cast<cgltf_size>(bytes.size());
        *data = buffer;
        return cgltf_result_success;
    }

    void gltf_file_release(const struct cgltf_memory_options *memory_options,
                           const struct cgltf_file_options *file_options,
                           void *data, cgltf_size size)
    {
        (void)memory_options;
        (void)file_options;
        (void)size;
        std::free(data);
    }

    /// @brief glTF 的因子（baseColor/emissive）是线性值，而 Color 存字节并按 sRGB 解释，
    /// 因此写入前需要做 linear -> sRGB 编码，否则颜色会明显偏暗。
    uint8_t to_srgb_byte(float linear)
    {
        const float value = std::clamp(linear, 0.0f, 1.0f);
        float encoded = 0.0f;
        if (value <= 0.0031308f)
        {
            encoded = value * 12.92f;
        }
        else
        {
            encoded = 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }
        return static_cast<uint8_t>(std::clamp(encoded * 255.0f + 0.5f, 0.0f, 255.0f));
    }

    Color color_from_linear(const cgltf_float rgba[4])
    {
        return Color(
            to_srgb_byte(rgba[0]),
            to_srgb_byte(rgba[1]),
            to_srgb_byte(rgba[2]),
            static_cast<uint8_t>(std::clamp(rgba[3], 0.0f, 1.0f) * 255.0f + 0.5f));
    }

    /// @brief glTF 节点矩阵是列主序的 4x4，直接取三列 + 平移即可。
    Transform3D transform_from_gltf(const cgltf_float m[16])
    {
        Transform3D transform;
        transform.basis.columns[0] = Vector3(m[0], m[1], m[2]);
        transform.basis.columns[1] = Vector3(m[4], m[5], m[6]);
        transform.basis.columns[2] = Vector3(m[8], m[9], m[10]);
        transform.origin = Vector3(m[12], m[13], m[14]);
        return transform;
    }

    int base64_value(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    bool base64_decode(const std::string &input, std::vector<uint8_t> &out)
    {
        out.clear();
        out.reserve(input.size() * 3 / 4 + 3);

        int buffer = 0;
        int bits = 0;
        for (char c : input)
        {
            if (c == '=') break;
            if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;

            const int value = base64_value(c);
            if (value < 0)
            {
                return false;
            }

            buffer = (buffer << 6) | value;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
            }
        }

        return true;
    }

    const cgltf_attribute *find_attribute(
        const cgltf_primitive &primitive, cgltf_attribute_type type, int index = 0)
    {
        for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
        {
            const cgltf_attribute &attribute = primitive.attributes[i];
            if (attribute.type == type && attribute.index == index)
            {
                return &attribute;
            }
        }
        return nullptr;
    }

    bool read_vec3_accessor(const cgltf_accessor *accessor, std::vector<Vector3> &out)
    {
        if (accessor == nullptr || accessor->count == 0 || accessor->type != cgltf_type_vec3)
        {
            return false;
        }

        out.resize(accessor->count);
        cgltf_float value[3] = {0.0f, 0.0f, 0.0f};
        for (cgltf_size i = 0; i < accessor->count; ++i)
        {
            if (!cgltf_accessor_read_float(accessor, i, value, 3))
            {
                return false;
            }
            out[i] = Vector3(value[0], value[1], value[2]);
        }
        return true;
    }

    bool read_vec2_accessor(const cgltf_accessor *accessor, std::vector<Vector2> &out)
    {
        if (accessor == nullptr || accessor->count == 0 || accessor->type != cgltf_type_vec2)
        {
            return false;
        }

        out.resize(accessor->count);
        cgltf_float value[2] = {0.0f, 0.0f};
        for (cgltf_size i = 0; i < accessor->count; ++i)
        {
            if (!cgltf_accessor_read_float(accessor, i, value, 2))
            {
                return false;
            }
            out[i] = Vector2(value[0], value[1]);
        }
        return true;
    }

    /// @brief 顶点法线累加交叉积后归一化；退化顶点回退到 +Y。
    void compute_smooth_normals(Mesh::Surface &surface)
    {
        const size_t vertex_count = surface.vertices.size();
        surface.normals.assign(vertex_count, Vector3());
        if (vertex_count == 0)
        {
            return;
        }

        std::vector<Vector3> sums(vertex_count, Vector3());
        for (size_t i = 0; i + 2 < surface.indices.size(); i += 3)
        {
            const uint32_t a = surface.indices[i];
            const uint32_t b = surface.indices[i + 1];
            const uint32_t c = surface.indices[i + 2];
            if (a >= vertex_count || b >= vertex_count || c >= vertex_count)
            {
                continue;
            }

            const Vector3 face_normal =
                (surface.vertices[b] - surface.vertices[a]).cross(surface.vertices[c] - surface.vertices[a]);
            sums[a] += face_normal;
            sums[b] += face_normal;
            sums[c] += face_normal;
        }

        for (size_t i = 0; i < vertex_count; ++i)
        {
            surface.normals[i] = sums[i].length_squared() > 1e-12f
                                     ? sums[i].normalized()
                                     : Vector3(0, 1, 0);
        }
    }
} // namespace

class GltfImporter
{
public:
    GltfImporter(const cgltf_data *data, const std::string &url)
        : data(data), url(url)
    {
    }

    Ref<Resource> run()
    {
        if (!check_asset())
        {
            return Ref<Resource>(nullptr);
        }

        if (data->meshes_count == 0)
        {
            std::printf("[WARN] glTF has no meshes: %s\n", url.c_str());
        }
        if (data->animations_count > 0)
        {
            std::printf("[WARN] glTF animations are not supported, using static node transforms: %s\n",
                        url.c_str());
        }

        PackedScene *scene = memnew(PackedScene);
        if (!build_nodes(scene) || !build_roots(scene))
        {
            memdelete(scene);
            return Ref<Resource>(nullptr);
        }

        std::string error;
        if (!scene->validate(&error))
        {
            std::printf("[ERROR] invalid packed scene built from %s: %s\n", url.c_str(), error.c_str());
            memdelete(scene);
            return Ref<Resource>(nullptr);
        }

        if (scene->get_nodes().empty())
        {
            std::printf("[ERROR] glTF has no nodes: %s\n", url.c_str());
            memdelete(scene);
            return Ref<Resource>(nullptr);
        }

        std::printf("[INFO] glTF loaded: \"%s\" (nodes=%llu, meshes=%llu, materials=%llu)\n",
                    url.c_str(),
                    static_cast<unsigned long long>(scene->get_nodes().size()),
                    static_cast<unsigned long long>(data->meshes_count),
                    static_cast<unsigned long long>(data->materials_count));
        return Ref<Resource>(scene);
    }

private:
    const cgltf_data *data = nullptr;
    std::string url;

    std::unordered_map<size_t, Ref<Mesh>> mesh_cache;
    std::unordered_map<size_t, Ref<StandardMaterial>> material_cache;
    /// @brief key = texture index * 2 + srgb
    std::unordered_map<size_t, Ref<ImageTexture>> texture_cache;
    /// @brief key = texture index；value = {roughness, metallic}
    std::unordered_map<size_t, std::pair<Ref<ImageTexture>, Ref<ImageTexture>>> metallic_roughness_cache;

    bool warned_mirrored_wrap = false;
    bool warned_skin = false;
    bool warned_morph_target = false;

    bool check_asset() const
    {
        if (data->asset.version == nullptr)
        {
            std::printf("[ERROR] glTF asset.version is missing: %s\n", url.c_str());
            return false;
        }
        if (std::strncmp(data->asset.version, "2.", 2) != 0)
        {
            std::printf("[ERROR] only glTF 2.x is supported, got \"%s\": %s\n",
                        data->asset.version, url.c_str());
            return false;
        }

        // 完全无法处理（压缩/纹理格式）的必需扩展直接失败；
        // 材质类扩展只影响外观，按基础 PBR 尽力加载并警告。
        static const char *const tolerated_extensions[] = {
            "KHR_lights_punctual",
            "KHR_materials_unlit",
            "KHR_materials_emissive_strength",
            "KHR_materials_specular",
            "KHR_materials_ior",
            "KHR_materials_clearcoat",
            "KHR_materials_transmission",
            "KHR_materials_volume",
            "KHR_materials_sheen",
            "KHR_materials_iridescence",
            "KHR_materials_anisotropy",
            "KHR_mesh_quantization",
            "KHR_texture_transform",
            "EXT_texture_webp",
        };

        bool ok = true;
        for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
        {
            const char *extension = data->extensions_required[i];
            if (extension == nullptr)
            {
                continue;
            }

            bool tolerated = false;
            for (const char *supported : tolerated_extensions)
            {
                if (std::strcmp(extension, supported) == 0)
                {
                    tolerated = true;
                    break;
                }
            }

            if (!tolerated)
            {
                std::printf("[ERROR] unsupported required glTF extension \"%s\": %s\n",
                            extension, url.c_str());
                ok = false;
            }
        }
        return ok;
    }

    bool build_nodes(PackedScene *scene)
    {
        if (data->nodes_count == 0 || data->nodes == nullptr)
        {
            std::printf("[ERROR] glTF has no nodes: %s\n", url.c_str());
            return false;
        }
        if (data->nodes_count > static_cast<cgltf_size>(INT_MAX))
        {
            std::printf("[ERROR] glTF node count is too large: %s\n", url.c_str());
            return false;
        }

        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            const cgltf_node &source = data->nodes[i];

            PackedScene::Node node;
            node.name = source.name != nullptr ? source.name : "";
            node.parent_idx = source.parent != nullptr
                                  ? static_cast<int>(source.parent - data->nodes)
                                  : -1;
            node.children.reserve(source.children_count);
            for (cgltf_size j = 0; j < source.children_count; ++j)
            {
                node.children.push_back(static_cast<int>(source.children[j] - data->nodes));
            }

            cgltf_float matrix[16] = {0.0f};
            cgltf_node_transform_local(&source, matrix);
            node.local_transform = transform_from_gltf(matrix);

            if (source.mesh != nullptr)
            {
                Ref<Mesh> mesh = get_mesh(static_cast<size_t>(source.mesh - data->meshes));
                if (mesh.is_valid())
                {
                    node.kind = PackedScene::NodeKind::MeshInstance;
                    node.mesh = mesh;
                }
                else
                {
                    std::printf("[WARN] glTF node \"%s\" references an unusable mesh, fallback to Node3D\n",
                                node.name.c_str());
                }
            }

            if (source.skin != nullptr && !warned_skin)
            {
                warned_skin = true;
                std::printf("[WARN] glTF skins are not supported, rendering bind pose: %s\n", url.c_str());
            }
            if (source.mesh != nullptr && source.mesh->primitives_count > 0 && !warned_morph_target)
            {
                for (cgltf_size p = 0; p < source.mesh->primitives_count; ++p)
                {
                    if (source.mesh->primitives[p].targets_count > 0)
                    {
                        warned_morph_target = true;
                        std::printf("[WARN] glTF morph targets are not supported, using base mesh: %s\n",
                                    url.c_str());
                        break;
                    }
                }
            }

            scene->add_node(node);
        }

        return true;
    }

    bool build_roots(PackedScene *scene)
    {
        const cgltf_scene *selected = nullptr;
        if (data->scene != nullptr)
        {
            selected = data->scene;
        }
        else if (data->scenes_count > 0)
        {
            selected = &data->scenes[0];
        }

        if (data->scenes_count > 1)
        {
            std::printf("[WARN] glTF contains %llu scenes, only the default one is loaded: %s\n",
                        static_cast<unsigned long long>(data->scenes_count), url.c_str());
            for (cgltf_size i = 0; i < data->scenes_count; ++i)
            {
                const cgltf_scene &ignored = data->scenes[i];
                if (&ignored == selected)
                {
                    continue;
                }
                std::printf("[WARN] ignored glTF scene \"%s\"\n",
                            ignored.name != nullptr ? ignored.name : "(unnamed)");
            }
        }

        bool has_roots = false;
        if (selected != nullptr)
        {
            for (cgltf_size i = 0; i < selected->nodes_count; ++i)
            {
                const cgltf_node *root = selected->nodes[i];
                if (root == nullptr || root->parent != nullptr)
                {
                    continue;
                }
                scene->add_root(static_cast<int>(root - data->nodes));
                has_roots = true;
            }
        }

        if (!has_roots)
        {
            // 没有 scene 或 scene 为空时，把无父节点的节点当作根。
            if (selected == nullptr)
            {
                std::printf("[WARN] glTF has no scenes, using parentless nodes as roots: %s\n", url.c_str());
            }
            for (cgltf_size i = 0; i < data->nodes_count; ++i)
            {
                if (data->nodes[i].parent == nullptr)
                {
                    scene->add_root(static_cast<int>(i));
                    has_roots = true;
                }
            }
        }

        if (!has_roots)
        {
            std::printf("[ERROR] glTF has no root nodes: %s\n", url.c_str());
            return false;
        }

        if (selected != nullptr && selected->name != nullptr)
        {
            scene->set_scene_name(selected->name);
        }
        else
        {
            scene->set_scene_name(fallback_scene_name());
        }
        return true;
    }

    std::string fallback_scene_name() const
    {
        const size_t separator = url.find_last_of("/\\");
        std::string name = separator == std::string::npos ? url : url.substr(separator + 1);
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos && dot > 0)
        {
            name = name.substr(0, dot);
        }
        return name;
    }

    // -----------------------------------------------------------------
    // Mesh
    // -----------------------------------------------------------------
    Ref<Mesh> get_mesh(size_t index)
    {
        if (index >= data->meshes_count)
        {
            std::printf("[WARN] glTF mesh index %llu is out of range\n",
                        static_cast<unsigned long long>(index));
            return Ref<Mesh>(nullptr);
        }

        auto cached = mesh_cache.find(index);
        if (cached != mesh_cache.end())
        {
            return cached->second;
        }

        Ref<Mesh> mesh = build_mesh(&data->meshes[index]);
        mesh_cache.emplace(index, mesh);
        return mesh;
    }

    Ref<Mesh> build_mesh(const cgltf_mesh *gltf_mesh)
    {
        Mesh *mesh = memnew(Mesh);
        mesh->reserve_surfaces(gltf_mesh->primitives_count);

        const std::string debug_name = gltf_mesh->name != nullptr ? gltf_mesh->name : "(unnamed)";

        for (cgltf_size i = 0; i < gltf_mesh->primitives_count; ++i)
        {
            Mesh::Surface surface;
            if (!build_surface(gltf_mesh->primitives[i], debug_name, surface))
            {
                continue;
            }
            // 移动而不是拷贝，避免加载期再复制整份顶点/索引数据。
            mesh->add_surface(std::move(surface));
        }

        mesh->compute_tangents();
        return Ref<Mesh>(mesh);
    }

    bool build_surface(const cgltf_primitive &primitive, const std::string &mesh_name,
                       Mesh::Surface &surface)
    {
        if (primitive.has_draco_mesh_compression)
        {
            std::printf("[WARN] glTF mesh \"%s\" uses Draco compression, primitive skipped\n",
                        mesh_name.c_str());
            return false;
        }

        if (primitive.type != cgltf_primitive_type_triangles &&
            primitive.type != cgltf_primitive_type_triangle_strip &&
            primitive.type != cgltf_primitive_type_triangle_fan)
        {
            std::printf("[WARN] glTF mesh \"%s\" uses unsupported primitive mode %d, skipped\n",
                        mesh_name.c_str(), static_cast<int>(primitive.type));
            return false;
        }

        const cgltf_attribute *position = find_attribute(primitive, cgltf_attribute_type_position);
        if (position == nullptr || !read_vec3_accessor(position->data, surface.vertices) ||
            surface.vertices.empty())
        {
            std::printf("[WARN] glTF mesh \"%s\" has no usable POSITION attribute, primitive skipped\n",
                        mesh_name.c_str());
            return false;
        }

        const size_t vertex_count = surface.vertices.size();

        bool needs_normals = false;
        const cgltf_attribute *normal = find_attribute(primitive, cgltf_attribute_type_normal);
        if (normal == nullptr || !read_vec3_accessor(normal->data, surface.normals) ||
            surface.normals.size() != vertex_count)
        {
            needs_normals = true;
            surface.normals.assign(vertex_count, Vector3());
        }

        // glTF 的 UV 原点在左上角（v 向下），引擎贴图按“首行在底部”解码，导入时翻转 v。
        const cgltf_attribute *uv = find_attribute(primitive, cgltf_attribute_type_texcoord, 0);
        surface.uvs.assign(vertex_count, Vector2());
        if (uv != nullptr && read_vec2_accessor(uv->data, surface.uvs) &&
            surface.uvs.size() == vertex_count)
        {
            for (Vector2 &texture_coord : surface.uvs)
            {
                texture_coord.y = 1.0f - texture_coord.y;
            }
        }
        else
        {
            // GLRenderingDevice 要求 uvs 数量与顶点一致，缺失时用零向量占位。
            surface.uvs.assign(vertex_count, Vector2());
        }

        if (!build_triangle_indices(primitive, vertex_count, mesh_name, surface.indices) ||
            surface.indices.empty())
        {
            return false;
        }

        if (needs_normals)
        {
            compute_smooth_normals(surface);
        }

        if (primitive.material != nullptr)
        {
            const size_t material_index = static_cast<size_t>(primitive.material - data->materials);
            surface.material = get_material(material_index);
        }

        return true;
    }

    bool build_triangle_indices(const cgltf_primitive &primitive, size_t vertex_count,
                                const std::string &mesh_name, std::vector<uint32_t> &out)
    {
        std::vector<uint32_t> raw;
        if (primitive.indices != nullptr)
        {
            const cgltf_accessor *accessor = primitive.indices;
            raw.reserve(accessor->count);
            bool warned_out_of_range = false;
            for (cgltf_size i = 0; i < accessor->count; ++i)
            {
                const cgltf_size index = cgltf_accessor_read_index(accessor, i);
                if (index >= vertex_count)
                {
                    if (!warned_out_of_range)
                    {
                        warned_out_of_range = true;
                        std::printf("[WARN] glTF mesh \"%s\" has out-of-range indices, dropped\n",
                                    mesh_name.c_str());
                    }
                    continue;
                }
                raw.push_back(static_cast<uint32_t>(index));
            }
        }
        else
        {
            if (vertex_count > static_cast<size_t>(UINT32_MAX))
            {
                std::printf("[WARN] glTF mesh \"%s\" has too many vertices, primitive skipped\n",
                            mesh_name.c_str());
                return false;
            }
            raw.resize(vertex_count);
            for (size_t i = 0; i < vertex_count; ++i)
            {
                raw[i] = static_cast<uint32_t>(i);
            }
        }

        switch (primitive.type)
        {
        case cgltf_primitive_type_triangles:
            out = std::move(raw);
            break;
        case cgltf_primitive_type_triangle_strip:
            out.reserve((raw.size() > 2 ? raw.size() - 2 : 0) * 3);
            for (size_t i = 0; i + 2 < raw.size(); ++i)
            {
                // 奇数三角形交换前两个顶点，保持与 glTF 一致的绕序。
                if (i & 1)
                {
                    out.push_back(raw[i + 1]);
                    out.push_back(raw[i]);
                    out.push_back(raw[i + 2]);
                }
                else
                {
                    out.push_back(raw[i]);
                    out.push_back(raw[i + 1]);
                    out.push_back(raw[i + 2]);
                }
            }
            break;
        case cgltf_primitive_type_triangle_fan:
            out.reserve((raw.size() > 2 ? raw.size() - 2 : 0) * 3);
            for (size_t i = 1; i + 1 < raw.size(); ++i)
            {
                out.push_back(raw[0]);
                out.push_back(raw[i]);
                out.push_back(raw[i + 1]);
            }
            break;
        default:
            return false;
        }

        if (out.size() % 3 != 0)
        {
            std::printf("[WARN] glTF mesh \"%s\" triangle index count %llu is not a multiple of 3, truncated\n",
                        mesh_name.c_str(), static_cast<unsigned long long>(out.size()));
            out.resize(out.size() - (out.size() % 3));
        }

        return true;
    }

    // -----------------------------------------------------------------
    // Material / texture
    // -----------------------------------------------------------------
    Ref<StandardMaterial> get_material(size_t index)
    {
        if (index >= data->materials_count)
        {
            return Ref<StandardMaterial>(nullptr);
        }

        auto cached = material_cache.find(index);
        if (cached != material_cache.end())
        {
            return cached->second;
        }

        Ref<StandardMaterial> material = build_material(&data->materials[index]);
        material_cache.emplace(index, material);
        return material;
    }

    Ref<StandardMaterial> build_material(const cgltf_material *source)
    {
        StandardMaterial *material = memnew(StandardMaterial);

        // glTF 默认值：白色、metallic=1、roughness=1、normal scale=1、AO strength=1。
        material->set_albedo(Color(255, 255, 255, 255));
        material->set_metallic(1.0f);
        material->set_roughness(1.0f);
        material->set_normal_scale(1.0f);
        material->set_ao_strength(1.0f);
        material->set_emission(Color(0, 0, 0, 255));

        if (source->has_pbr_metallic_roughness)
        {
            const cgltf_pbr_metallic_roughness &pbr = source->pbr_metallic_roughness;
            material->set_albedo(color_from_linear(pbr.base_color_factor));
            material->set_metallic(pbr.metallic_factor);
            material->set_roughness(pbr.roughness_factor);

            const Ref<ImageTexture> albedo = load_texture_view(pbr.base_color_texture, true, "baseColor");
            if (albedo.is_valid())
            {
                material->set_texture_albedo(albedo);
            }

            if (check_texture_view(pbr.metallic_roughness_texture, "metallicRoughness"))
            {
                // glTF 把 roughness(G) 与 metallic(B) 打包在一张图里，引擎 shader 分别采样 .r，
                // 所以必须在 CPU 侧拆成两张单通道贴图。
                const std::pair<Ref<ImageTexture>, Ref<ImageTexture>> split =
                    split_metallic_roughness(pbr.metallic_roughness_texture.texture);
                if (split.first.is_valid())
                {
                    material->set_texture_roughness(split.first);
                }
                if (split.second.is_valid())
                {
                    material->set_texture_metallic(split.second);
                }
            }
        }

        if (source->has_pbr_specular_glossiness)
        {
            std::printf("[WARN] glTF material uses KHR_materials_pbrSpecularGlossiness, ignored\n");
        }
        if (source->unlit)
        {
            std::printf("[WARN] glTF material \"%s\" is unlit, rendered as PBR\n",
                        source->name != nullptr ? source->name : "(unnamed)");
        }
        if (source->alpha_mode != cgltf_alpha_mode_opaque)
        {
            std::printf("[WARN] glTF material \"%s\" uses alphaMode=%d, transparency is not supported\n",
                        source->name != nullptr ? source->name : "(unnamed)",
                        static_cast<int>(source->alpha_mode));
        }

        const Ref<ImageTexture> normal = load_texture_view(source->normal_texture, false, "normal");
        if (normal.is_valid())
        {
            material->set_texture_normal(normal);
            material->set_normal_scale(source->normal_texture.scale);
        }

        const Ref<ImageTexture> occlusion = load_texture_view(source->occlusion_texture, false, "occlusion");
        if (occlusion.is_valid())
        {
            material->set_texture_ao(occlusion);
            material->set_ao_strength(source->occlusion_texture.scale);
        }

        // emissiveFactor 是线性值；shader 暂未使用 emission，先按约定存下来。
        material->set_emission(color_from_linear(source->emissive_factor));
        if (source->emissive_texture.texture != nullptr)
        {
            std::printf("[WARN] glTF material \"%s\" uses emissiveTexture, not supported yet\n",
                        source->name != nullptr ? source->name : "(unnamed)");
        }

        return Ref<StandardMaterial>(material);
    }

    /// @brief 校验 texture_view 是否可映射到引擎：只支持 UV0、无 KHR_texture_transform。
    bool check_texture_view(const cgltf_texture_view &view, const char *usage)
    {
        if (view.texture == nullptr)
        {
            return false;
        }
        if (view.texcoord != 0)
        {
            std::printf("[WARN] glTF %s texture uses TEXCOORD_%d, only UV0 is supported, skipped\n",
                        usage, static_cast<int>(view.texcoord));
            return false;
        }
        if (view.has_transform)
        {
            std::printf("[WARN] glTF %s texture uses KHR_texture_transform, skipped\n", usage);
            return false;
        }
        return true;
    }

    Ref<ImageTexture> load_texture_view(const cgltf_texture_view &view, bool srgb, const char *usage)
    {
        if (!check_texture_view(view, usage))
        {
            return Ref<ImageTexture>(nullptr);
        }
        return load_texture(view.texture, srgb);
    }

    Ref<ImageTexture> load_texture(const cgltf_texture *texture, bool srgb)
    {
        if (texture == nullptr || texture->image == nullptr)
        {
            if (texture != nullptr && texture->has_basisu)
            {
                std::printf("[WARN] glTF texture uses KTX2/basisu, not supported\n");
            }
            return Ref<ImageTexture>(nullptr);
        }

        const size_t texture_index = static_cast<size_t>(texture - data->textures);
        const size_t key = texture_index * 2 + (srgb ? 1 : 0);
        auto cached = texture_cache.find(key);
        if (cached != texture_cache.end())
        {
            return cached->second;
        }

        std::vector<uint8_t> bytes;
        std::string debug_name;
        if (!read_image_bytes(texture->image, bytes, debug_name))
        {
            texture_cache.emplace(key, Ref<ImageTexture>(nullptr));
            return Ref<ImageTexture>(nullptr);
        }

        Ref<ImageTexture> image = ImageTextureLoader::load_from_memory(
            bytes.data(), bytes.size(), srgb, debug_name.c_str());
        if (image.is_null())
        {
            texture_cache.emplace(key, Ref<ImageTexture>(nullptr));
            return Ref<ImageTexture>(nullptr);
        }

        apply_sampler(image.ptr(), texture->sampler);
        texture_cache.emplace(key, image);
        return image;
    }

    std::pair<Ref<ImageTexture>, Ref<ImageTexture>> split_metallic_roughness(const cgltf_texture *texture)
    {
        const size_t texture_index = static_cast<size_t>(texture - data->textures);
        auto cached = metallic_roughness_cache.find(texture_index);
        if (cached != metallic_roughness_cache.end())
        {
            return cached->second;
        }

        const std::pair<Ref<ImageTexture>, Ref<ImageTexture>> empty;
        const Ref<ImageTexture> source = load_texture(texture, false);
        if (source.is_null())
        {
            metallic_roughness_cache.emplace(texture_index, empty);
            return empty;
        }

        const uint32_t width = source->get_width();
        const uint32_t height = source->get_height();
        const uint8_t channels = source->get_channels();
        const std::vector<uint8_t> &pixels = source->get_pixels();
        if (width == 0 || height == 0 || channels < 3 ||
            pixels.size() < static_cast<size_t>(width) * height * channels)
        {
            std::printf("[WARN] glTF metallicRoughness texture has unusable pixel data, skipped\n");
            metallic_roughness_cache.emplace(texture_index, empty);
            return empty;
        }

        const size_t pixel_count = static_cast<size_t>(width) * height;
        std::vector<uint8_t> roughness_pixels(pixel_count);
        std::vector<uint8_t> metallic_pixels(pixel_count);
        for (size_t i = 0; i < pixel_count; ++i)
        {
            roughness_pixels[i] = pixels[i * channels + 1];
            metallic_pixels[i] = pixels[i * channels + 2];
        }

        auto make_single_channel = [&](const std::vector<uint8_t> &source_pixels) {
            ImageTexture *channel = memnew(ImageTexture);
            channel->set_width(width);
            channel->set_height(height);
            channel->set_channels(1);
            channel->set_pixels(source_pixels);
            channel->set_srgb(false);
            channel->set_repeat(source->is_repeat());
            channel->set_linear_filter(source->is_linear_filter());
            channel->set_gen_mipmap(source->is_gen_mipmap());
            return Ref<ImageTexture>(channel);
        };

        std::pair<Ref<ImageTexture>, Ref<ImageTexture>> result = {
            make_single_channel(roughness_pixels),
            make_single_channel(metallic_pixels),
        };
        metallic_roughness_cache.emplace(texture_index, result);
        return result;
    }

    bool read_image_bytes(const cgltf_image *image, std::vector<uint8_t> &bytes, std::string &debug_name)
    {
        if (image == nullptr)
        {
            return false;
        }

        if (image->buffer_view != nullptr)
        {
            const uint8_t *data = cgltf_buffer_view_data(image->buffer_view);
            if (data == nullptr)
            {
                std::printf("[WARN] glTF embedded image buffer view has no data\n");
                return false;
            }
            bytes.assign(data, data + image->buffer_view->size);
            debug_name = image->name != nullptr ? image->name : "<glb-embedded>";
            return true;
        }

        if (image->uri == nullptr)
        {
            std::printf("[WARN] glTF image has neither uri nor buffer view\n");
            return false;
        }

        const std::string uri = image->uri;
        if (uri.rfind("data:", 0) == 0)
        {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos || comma < 7 ||
                uri.compare(comma - 7, 7, ";base64") != 0)
            {
                std::printf("[WARN] unsupported glTF image data URI (only base64 is supported)\n");
                return false;
            }
            if (!base64_decode(uri.substr(comma + 1), bytes))
            {
                std::printf("[WARN] failed to decode glTF image data URI\n");
                return false;
            }
            debug_name = image->name != nullptr ? image->name : "<data-uri>";
            return true;
        }

        const std::string path = Path::join(Path::get_base_dir(url), Path::decode_uri(uri));
        if (!FileAccess::read_file_bytes(path, bytes))
        {
            std::printf("[WARN] failed to read glTF image: %s\n", path.c_str());
            return false;
        }
        debug_name = path;
        return true;
    }

    void apply_sampler(ImageTexture *texture, const cgltf_sampler *sampler)
    {
        if (texture == nullptr)
        {
            return;
        }

        // glTF 默认 sampler：wrap=REPEAT、mag=LINEAR、min=LINEAR（不带 mipmap）。
        bool repeat = true;
        bool linear_filter = true;
        bool generate_mipmap = false;

        if (sampler != nullptr)
        {
            repeat = sampler->wrap_s != cgltf_wrap_mode_clamp_to_edge &&
                     sampler->wrap_t != cgltf_wrap_mode_clamp_to_edge;

            if ((sampler->wrap_s == cgltf_wrap_mode_mirrored_repeat ||
                 sampler->wrap_t == cgltf_wrap_mode_mirrored_repeat) &&
                !warned_mirrored_wrap)
            {
                warned_mirrored_wrap = true;
                std::printf("[WARN] glTF sampler uses MIRRORED_REPEAT, approximated with REPEAT\n");
            }

            linear_filter = sampler->mag_filter != cgltf_filter_type_nearest;

            switch (sampler->min_filter)
            {
            case cgltf_filter_type_nearest_mipmap_nearest:
            case cgltf_filter_type_linear_mipmap_nearest:
            case cgltf_filter_type_nearest_mipmap_linear:
            case cgltf_filter_type_linear_mipmap_linear:
                generate_mipmap = true;
                break;
            default:
                generate_mipmap = false;
                break;
            }
        }

        texture->set_repeat(repeat);
        texture->set_linear_filter(linear_filter);
        texture->set_gen_mipmap(generate_mipmap);
    }
};

Ref<Resource> GLTFLoader::load(const std::string url)
{
    std::vector<uint8_t> buffer;
    if (!FileAccess::read_file_bytes(url, buffer))
    {
        std::printf("[ERROR] gltf loader can't open file: %s\n", url.c_str());
        return Ref<Resource>(nullptr);
    }

    cgltf_options options = {};
    options.type = cgltf_file_type_invalid;
    // 外部 .bin 的读取回调必须走 FileAccess/Path，不能直接 fopen，
    // 否则 res:// 与百分号编码会失效。
    options.file.read = &gltf_file_read;
    options.file.release = &gltf_file_release;

    cgltf_data *out_data = nullptr;
    const cgltf_result parse_result = cgltf_parse(&options, buffer.data(), buffer.size(), &out_data);
    if (parse_result != cgltf_result_success)
    {
        std::printf("[ERROR] gltf loader parse error, code=%d: %s\n",
                    static_cast<int>(parse_result), url.c_str());
        return Ref<Resource>(nullptr);
    }

    // GLB 的内嵌 BIN 由 cgltf 处理；外部 .bin 通过 file callback 读取。
    // 传入绝对路径，cgltf 用它的目录与 buffer.uri 拼接。
    const std::string gltf_path = Path::replace_url(url);
    const cgltf_result buffer_result = cgltf_load_buffers(&options, out_data, gltf_path.c_str());
    if (buffer_result != cgltf_result_success)
    {
        std::printf("[ERROR] gltf loader failed to load buffers, code=%d: %s\n",
                    static_cast<int>(buffer_result), url.c_str());
        cgltf_free(out_data);
        return Ref<Resource>(nullptr);
    }

    GltfImporter importer(out_data, url);
    Ref<Resource> scene = importer.run();
    cgltf_free(out_data);
    return scene;
}
