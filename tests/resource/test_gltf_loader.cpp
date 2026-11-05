#include "doctest/doctest.h"
#include "loaders/gltf_loader.h"
#include "mesh/mesh.h"
#include "material/material.h"
#include "scene/scene.h"
#include "texture/image_texture.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    // =====================================================================
    // 测试用的小工具：base64 编码、PNG 编码、二进制文件写入。
    // 目的：不依赖仓库外的样例资源（build/resources 不在版本控制中）。
    // =====================================================================
    std::string base64_encode(const std::vector<uint8_t> &data)
    {
        static const char *const alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string encoded;
        encoded.reserve((data.size() + 2) / 3 * 4);

        size_t i = 0;
        while (i + 2 < data.size())
        {
            const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                                    (static_cast<uint32_t>(data[i + 1]) << 8) |
                                    static_cast<uint32_t>(data[i + 2]);
            encoded.push_back(alphabet[(triple >> 18) & 0x3F]);
            encoded.push_back(alphabet[(triple >> 12) & 0x3F]);
            encoded.push_back(alphabet[(triple >> 6) & 0x3F]);
            encoded.push_back(alphabet[triple & 0x3F]);
            i += 3;
        }

        const size_t remaining = data.size() - i;
        if (remaining == 1)
        {
            const uint32_t value = static_cast<uint32_t>(data[i]) << 16;
            encoded.push_back(alphabet[(value >> 18) & 0x3F]);
            encoded.push_back(alphabet[(value >> 12) & 0x3F]);
            encoded.push_back('=');
            encoded.push_back('=');
        }
        else if (remaining == 2)
        {
            const uint32_t value = (static_cast<uint32_t>(data[i]) << 16) |
                                   (static_cast<uint32_t>(data[i + 1]) << 8);
            encoded.push_back(alphabet[(value >> 18) & 0x3F]);
            encoded.push_back(alphabet[(value >> 12) & 0x3F]);
            encoded.push_back(alphabet[(value >> 6) & 0x3F]);
            encoded.push_back('=');
        }

        return encoded;
    }

    uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
    {
        crc = ~crc;
        for (size_t i = 0; i < length; ++i)
        {
            crc ^= data[i];
            for (int bit = 0; bit < 8; ++bit)
            {
                const uint32_t mask = ~(crc & 1u) + 1u;
                crc = (crc >> 1) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }

    void append_be16(std::vector<uint8_t> &out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void append_be32(std::vector<uint8_t> &out, uint32_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void append_le16(std::vector<uint8_t> &out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void append_le32(std::vector<uint8_t> &out, uint32_t value)
    {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void append_png_chunk(std::vector<uint8_t> &png, const char *type,
                          const std::vector<uint8_t> &data)
    {
        append_be32(png, static_cast<uint32_t>(data.size()));
        const size_t type_begin = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());

        uint32_t crc = 0;
        crc = crc32_update(crc, png.data() + type_begin, 4 + data.size());
        append_be32(png, crc);
    }

    /// @brief 生成未压缩 8bit RGB PNG；用于内嵌图片、外部图片与 MR 通道拆分测试。
    std::vector<uint8_t> build_png_rgb(uint32_t width, uint32_t height,
                                       const std::vector<uint8_t> &rgb_pixels)
    {
        std::vector<uint8_t> png;
        if (rgb_pixels.size() != static_cast<size_t>(width) * height * 3)
        {
            return png;
        }

        const uint8_t signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        png.insert(png.end(), signature, signature + 8);

        std::vector<uint8_t> ihdr;
        append_be32(ihdr, width);
        append_be32(ihdr, height);
        ihdr.push_back(8); // bit depth
        ihdr.push_back(2); // color type: truecolor RGB
        ihdr.push_back(0); // compression
        ihdr.push_back(0); // filter
        ihdr.push_back(0); // interlace
        append_png_chunk(png, "IHDR", ihdr);

        // 每行前缀 filter byte 0，然后跟 RGB 数据。
        std::vector<uint8_t> raw;
        raw.reserve(static_cast<size_t>(height) * (1 + width * 3));
        for (uint32_t y = 0; y < height; ++y)
        {
            raw.push_back(0);
            const size_t begin = static_cast<size_t>(y) * width * 3;
            raw.insert(raw.end(), rgb_pixels.begin() + begin, rgb_pixels.begin() + begin + width * 3);
        }

        // zlib 流：使用 deflate 的 stored 块，测试数据无需真正的压缩。
        std::vector<uint8_t> zlib;
        zlib.push_back(0x78);
        zlib.push_back(0x01);
        size_t offset = 0;
        while (offset < raw.size())
        {
            const size_t block = std::min<size_t>(65535, raw.size() - offset);
            const bool final_block = offset + block >= raw.size();
            zlib.push_back(final_block ? 0x01 : 0x00);
            append_le16(zlib, static_cast<uint16_t>(block));
            append_le16(zlib, static_cast<uint16_t>(~static_cast<uint16_t>(block)));
            zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + block);
            offset += block;
        }

        uint32_t adler_a = 1;
        uint32_t adler_b = 0;
        for (uint8_t byte : raw)
        {
            adler_a = (adler_a + byte) % 65521;
            adler_b = (adler_b + adler_a) % 65521;
        }
        append_be32(zlib, (adler_b << 16) | adler_a);

        append_png_chunk(png, "IDAT", zlib);
        append_png_chunk(png, "IEND", std::vector<uint8_t>());
        return png;
    }

    void write_file(const std::string &path, const std::vector<uint8_t> &bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        if (!bytes.empty())
        {
            file.write(reinterpret_cast<const char *>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        }
    }

    void write_text_file(const std::string &path, const std::string &text)
    {
        write_file(path, std::vector<uint8_t>(text.begin(), text.end()));
    }

    void append_float(std::vector<uint8_t> &out, float value)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &value, sizeof(bytes));
        out.insert(out.end(), bytes, bytes + 4);
    }

    void replace_all(std::string &text, const std::string &from, const std::string &to)
    {
        size_t position = 0;
        while ((position = text.find(from, position)) != std::string::npos)
        {
            text.replace(position, from.size(), to);
            position += to.size();
        }
    }

    /// @brief 3 个顶点、带法线/UV/索引的三角形缓冲。
    /// 布局：positions(0-36) normals(36-72) uvs(72-96) indices u16(96-102)
    std::vector<uint8_t> build_triangle_buffer()
    {
        const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        const float normals[9] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
        const float uvs[6] = {0, 0, 1, 0, 0, 1};
        const uint16_t indices[3] = {0, 1, 2};

        std::vector<uint8_t> buffer;
        for (float value : positions) append_float(buffer, value);
        for (float value : normals) append_float(buffer, value);
        for (float value : uvs) append_float(buffer, value);
        append_le16(buffer, indices[0]);
        append_le16(buffer, indices[1]);
        append_le16(buffer, indices[2]);
        return buffer;
    }

    /// @brief 一个完整的 .gltf：单三角形 + 节点层级 + 带贴图的材质。
    std::string make_triangle_gltf(const std::string &buffer_uri, const std::string &image_uri)
    {
        std::string json = R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"name": "TestScene", "nodes": [0]}],
  "nodes": [
    {"name": "root", "translation": [1.0, 2.0, 3.0], "scale": [2.0, 2.0, 2.0], "mesh": 0, "children": [1]},
    {"name": "child", "matrix": [-1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 4.0, 5.0, 6.0, 1.0]}
  ],
  "meshes": [{
    "name": "triangle",
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
      "indices": 3,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "mat",
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.5, 0.25, 0.1, 1.0],
      "metallicFactor": 0.25,
      "roughnessFactor": 0.75,
      "baseColorTexture": {"index": 0},
      "metallicRoughnessTexture": {"index": 1}
    },
    "normalTexture": {"index": 2, "scale": 0.5},
    "occlusionTexture": {"index": 2, "strength": 0.8},
    "emissiveFactor": [0.1, 0.2, 0.3]
  }],
  "textures": [
    {"source": 0, "sampler": 0},
    {"source": 0, "sampler": 0},
    {"source": 0, "sampler": 0}
  ],
  "images": [{"uri": "__IMAGE_URI__"}],
  "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
  "buffers": [{"uri": "__BUFFER_URI__", "byteLength": 102}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ]
})JSON";

        replace_all(json, "__IMAGE_URI__", image_uri);
        replace_all(json, "__BUFFER_URI__", buffer_uri);
        return json;
    }

    std::vector<uint8_t> build_glb(const std::string &json_text, const std::vector<uint8_t> &bin)
    {
        std::vector<uint8_t> json(json_text.begin(), json_text.end());
        while (json.size() % 4 != 0) json.push_back(0x20);

        std::vector<uint8_t> binary = bin;
        while (binary.size() % 4 != 0) binary.push_back(0x00);

        const uint32_t total_length = static_cast<uint32_t>(
            12 + 8 + json.size() + 8 + binary.size());

        std::vector<uint8_t> glb;
        append_le32(glb, 0x46546C67); // "glTF"
        append_le32(glb, 2);
        append_le32(glb, total_length);

        append_le32(glb, static_cast<uint32_t>(json.size()));
        append_le32(glb, 0x4E4F534A); // "JSON"
        glb.insert(glb.end(), json.begin(), json.end());

        append_le32(glb, static_cast<uint32_t>(binary.size()));
        append_le32(glb, 0x004E4942); // "BIN\0"
        glb.insert(glb.end(), binary.begin(), binary.end());
        return glb;
    }
} // namespace

TEST_CASE("GLTFLoader: 解析最小 glTF、节点层级与 PBR 材质")
{
    const std::vector<uint8_t> png = build_png_rgb(1, 1, {10, 20, 30});
    REQUIRE_FALSE(png.empty());

    const std::string buffer_uri =
        "data:application/octet-stream;base64," + base64_encode(build_triangle_buffer());
    const std::string image_uri = "data:image/png;base64," + base64_encode(png);

    const std::string path = "orca_gltf_triangle_test.gltf";
    write_text_file(path, make_triangle_gltf(buffer_uri, image_uri));

    GLTFLoader loader;
    Ref<Resource> resource = loader.load(path);
    std::remove(path.c_str());
    REQUIRE(resource.is_valid());

    Ref<PackedScene> scene = resource.cast<PackedScene>();
    REQUIRE(scene.is_valid());
    CHECK(scene->validate());
    CHECK(scene->get_scene_name() == "TestScene");
    REQUIRE(scene->get_nodes().size() == 2);
    REQUIRE(scene->get_roots().size() == 1);
    CHECK(scene->get_roots()[0] == 0);

    // 节点层级与局部变换
    const PackedScene::Node &root = scene->get_nodes()[0];
    CHECK(root.name == "root");
    CHECK(root.parent_idx == -1);
    REQUIRE(root.children.size() == 1);
    CHECK(root.children[0] == 1);
    CHECK(root.kind == PackedScene::NodeKind::MeshInstance);
    CHECK(root.mesh.is_valid());
    CHECK(root.local_transform.origin.is_equal_approx(Vector3(1, 2, 3)));
    CHECK(root.local_transform.basis.get_scale().is_equal_approx(Vector3(2, 2, 2)));

    const PackedScene::Node &child = scene->get_nodes()[1];
    CHECK(child.name == "child");
    CHECK(child.parent_idx == 0);
    CHECK(child.kind == PackedScene::NodeKind::Node3D);
    CHECK(child.local_transform.origin.is_equal_approx(Vector3(4, 5, 6)));
    // matrix 写法的负缩放
    CHECK(child.local_transform.basis.determinant() < 0.0f);

    // Mesh / Surface
    const Ref<Mesh> &mesh = root.mesh;
    REQUIRE(mesh->get_surface_count() == 1);
    const Mesh::Surface &surface = mesh->get_surface(0);
    CHECK(surface.vertices.size() == 3);
    CHECK(surface.normals.size() == 3);
    CHECK(surface.uvs.size() == 3);
    CHECK(surface.indices.size() == 3);
    CHECK(surface.indices[2] == 2);
    // glTF UV 原点在左上角，导入时翻转 v
    CHECK(surface.uvs[0].is_equal_approx(Vector2(0, 1)));
    CHECK(surface.uvs[2].is_equal_approx(Vector2(0, 0)));

    // 材质标量
    REQUIRE(surface.material.is_valid());
    const Ref<StandardMaterial> &material = surface.material;
    CHECK(material->get_metallic() == doctest::Approx(0.25f));
    CHECK(material->get_roughenss() == doctest::Approx(0.75f));
    CHECK(material->get_normal_scale() == doctest::Approx(0.5f));
    CHECK(material->get_ao_strength() == doctest::Approx(0.8f));

    // baseColorFactor 是线性值，写入 Color 前要编码成 sRGB 字节
    real_t linear[3] = {0, 0, 0};
    material->get_albedo().linear(linear);
    CHECK(linear[0] == doctest::Approx(0.5f).epsilon(0.02f));
    CHECK(linear[1] == doctest::Approx(0.25f).epsilon(0.02f));
    CHECK(linear[2] == doctest::Approx(0.1f).epsilon(0.02f));

    // 贴图：baseColor 做 sRGB 解码，数据图不做
    const Ref<ImageTexture> albedo = material->get_texture_albedo();
    REQUIRE(albedo.is_valid());
    CHECK(albedo->is_srgb());
    CHECK(albedo->get_width() == 1);
    CHECK(albedo->get_height() == 1);

    const Ref<ImageTexture> normal = material->get_texture_normal();
    REQUIRE(normal.is_valid());
    CHECK_FALSE(normal->is_srgb());
    CHECK(normal->is_repeat());
    CHECK(normal->is_linear_filter());
    CHECK(normal->is_gen_mipmap()); // minFilter 9987 带 mipmap

    // AO 与 normal 引用同一个 glTF texture，应命中缓存拿到同一个实例
    CHECK(material->get_texture_ao().ptr() == normal.ptr());

    // metallicRoughness 打包图在 CPU 侧拆分：G -> roughness，B -> metallic
    const Ref<ImageTexture> roughness = material->get_texture_roughness();
    REQUIRE(roughness.is_valid());
    CHECK(roughness->get_channels() == 1);
    REQUIRE(roughness->get_pixels().size() == 1);
    CHECK(roughness->get_pixels()[0] == 20);

    const Ref<ImageTexture> metallic = material->get_texture_metallic();
    REQUIRE(metallic.is_valid());
    CHECK(metallic->get_channels() == 1);
    REQUIRE(metallic->get_pixels().size() == 1);
    CHECK(metallic->get_pixels()[0] == 30);
}

TEST_CASE("GLTFLoader: 多 primitive 映射多个 Surface 与材质")
{
    // 布局：pos0(0-36) idx0 u8(36-39) pad(39-40) pos1(40-88) idx1 u16(88-96) pos2(96-132)
    std::vector<uint8_t> buffer;
    const float positions_triangle[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    for (float value : positions_triangle) append_float(buffer, value);
    buffer.push_back(0);
    buffer.push_back(1);
    buffer.push_back(2);
    buffer.push_back(0); // padding，保持后面的 float 4 字节对齐

    const float positions_quad[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    for (float value : positions_quad) append_float(buffer, value);
    append_le16(buffer, 0);
    append_le16(buffer, 1);
    append_le16(buffer, 2);
    append_le16(buffer, 3);

    const float positions_second[9] = {2, 0, 0, 3, 0, 0, 2, 1, 0};
    for (float value : positions_second) append_float(buffer, value);

    const std::string buffer_uri =
        "data:application/octet-stream;base64," + base64_encode(buffer);

    std::string json = R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "multi", "mesh": 0}],
  "meshes": [{
    "name": "multi",
    "primitives": [
      {"mode": 4, "attributes": {"POSITION": 0}, "indices": 1, "material": 0},
      {"mode": 5, "attributes": {"POSITION": 2}, "indices": 3, "material": 1},
      {"mode": 4, "attributes": {"POSITION": 4}},
      {"mode": 0, "attributes": {"POSITION": 4}}
    ]
  }],
  "materials": [
    {"name": "red", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.0, 0.0, 1.0]}},
    {"name": "green", "pbrMetallicRoughness": {"baseColorFactor": [0.0, 1.0, 0.0, 1.0]}}
  ],
  "buffers": [{"uri": "__BUFFER_URI__", "byteLength": 132}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 3},
    {"buffer": 0, "byteOffset": 40, "byteLength": 48},
    {"buffer": 0, "byteOffset": 88, "byteLength": 8},
    {"buffer": 0, "byteOffset": 96, "byteLength": 36}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5121, "count": 3, "type": "SCALAR"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 3, "componentType": 5123, "count": 4, "type": "SCALAR"},
    {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC3"}
  ]
})JSON";
    replace_all(json, "__BUFFER_URI__", buffer_uri);

    const std::string path = "orca_gltf_multi_primitive_test.gltf";
    write_text_file(path, json);

    GLTFLoader loader;
    Ref<Resource> resource = loader.load(path);
    std::remove(path.c_str());
    REQUIRE(resource.is_valid());

    Ref<PackedScene> scene = resource.cast<PackedScene>();
    REQUIRE(scene.is_valid());
    REQUIRE(scene->get_nodes().size() == 1);
    const Ref<Mesh> &mesh = scene->get_nodes()[0].mesh;
    REQUIRE(mesh.is_valid());

    // POINTS primitive 被跳过
    REQUIRE(mesh->get_surface_count() == 3);
    CHECK(mesh->get_surface(0).indices.size() == 3);
    // TRIANGLE_STRIP 的 4 个索引 -> 2 个三角形
    CHECK(mesh->get_surface(1).indices.size() == 6);
    CHECK(mesh->get_surface(1).indices[0] == 0);
    CHECK(mesh->get_surface(1).indices[1] == 1);
    CHECK(mesh->get_surface(1).indices[2] == 2);
    // 无索引时自动生成 0..N-1
    CHECK(mesh->get_surface(2).indices.size() == 3);
    CHECK(mesh->get_surface(2).indices[2] == 2);

    // 多个 primitive 各自映射材质
    REQUIRE(mesh->get_surface(0).material.is_valid());
    REQUIRE(mesh->get_surface(1).material.is_valid());
    CHECK(mesh->get_surface(0).material.ptr() != mesh->get_surface(1).material.ptr());
    CHECK(mesh->get_surface(0).material->get_albedo() != mesh->get_surface(1).material->get_albedo());
    CHECK(mesh->get_surface(2).material.is_null());
}

TEST_CASE("GLTFLoader: 外部 .bin 与带百分号编码的图片路径")
{
    const std::vector<uint8_t> png = build_png_rgb(1, 1, {10, 20, 30});
    const std::vector<uint8_t> buffer = build_triangle_buffer();
    REQUIRE_FALSE(png.empty());

    const std::string buffer_path = "orca gltf external buffer.bin";
    const std::string image_path = "orca gltf external image.png";
    const std::string gltf_path = "orca_gltf_external_test.gltf";

    write_file(buffer_path, buffer);
    write_file(image_path, png);

    const std::string json = make_triangle_gltf("orca%20gltf%20external%20buffer.bin",
                                                "orca%20gltf%20external%20image.png");
    write_text_file(gltf_path, json);

    GLTFLoader loader;
    Ref<Resource> resource = loader.load(gltf_path);
    std::remove(gltf_path.c_str());
    std::remove(buffer_path.c_str());
    std::remove(image_path.c_str());

    REQUIRE(resource.is_valid());
    Ref<PackedScene> scene = resource.cast<PackedScene>();
    REQUIRE(scene.is_valid());
    REQUIRE(scene->get_nodes().size() == 2);
    const Ref<Mesh> &mesh = scene->get_nodes()[0].mesh;
    REQUIRE(mesh.is_valid());
    REQUIRE(mesh->get_surface_count() == 1);
    REQUIRE(mesh->get_surface(0).material.is_valid());
    REQUIRE(mesh->get_surface(0).material->get_texture_albedo().is_valid());
    CHECK(mesh->get_surface(0).material->get_texture_albedo()->get_width() == 1);
}

TEST_CASE("GLTFLoader: GLB 容器与内嵌图片")
{
    const std::vector<uint8_t> png = build_png_rgb(1, 1, {10, 20, 30});
    std::vector<uint8_t> bin = build_triangle_buffer();
    const size_t png_offset = bin.size();
    bin.insert(bin.end(), png.begin(), png.end());

    std::string json = R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"name": "GlbScene", "nodes": [0]}],
  "nodes": [{"name": "root", "mesh": 0}],
  "meshes": [{
    "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]
  }],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
  "textures": [{"source": 0, "sampler": 0}],
  "images": [{"bufferView": 4, "mimeType": "image/png"}],
  "samplers": [{"magFilter": 9729, "minFilter": 9729}],
  "buffers": [{"byteLength": __BIN_LENGTH__}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6},
    {"buffer": 0, "byteOffset": __PNG_OFFSET__, "byteLength": __PNG_LENGTH__}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ]
})JSON";

    replace_all(json, "__BIN_LENGTH__", std::to_string(bin.size()));
    replace_all(json, "__PNG_OFFSET__", std::to_string(png_offset));
    replace_all(json, "__PNG_LENGTH__", std::to_string(png.size()));

    const std::vector<uint8_t> glb = build_glb(json, bin);
    const std::string path = "orca_gltf_embedded_test.glb";
    write_file(path, glb);

    GLTFLoader loader;
    Ref<Resource> resource = loader.load(path);
    std::remove(path.c_str());
    REQUIRE(resource.is_valid());

    Ref<PackedScene> scene = resource.cast<PackedScene>();
    REQUIRE(scene.is_valid());
    CHECK(scene->get_scene_name() == "GlbScene");
    REQUIRE(scene->get_nodes().size() == 1);
    const Ref<Mesh> &mesh = scene->get_nodes()[0].mesh;
    REQUIRE(mesh.is_valid());
    REQUIRE(mesh->get_surface_count() == 1);

    const Ref<StandardMaterial> &material = mesh->get_surface(0).material;
    REQUIRE(material.is_valid());
    const Ref<ImageTexture> albedo = material->get_texture_albedo();
    REQUIRE(albedo.is_valid());
    CHECK(albedo->is_srgb());
    CHECK(albedo->get_width() == 1);
    // glTF 默认 sampler（无 mipmap）应关闭 mipmap 生成
    CHECK_FALSE(albedo->is_gen_mipmap());
}

TEST_CASE("GLTFLoader: 非法输入与不支持的特性")
{
    GLTFLoader loader;

    // 文件不存在
    CHECK(loader.load("orca_missing_gltf_for_test.gltf").is_null());

    // 非法 JSON
    {
        const std::string path = "orca_gltf_bad_json_test.gltf";
        write_text_file(path, "{ this is not json");
        CHECK(loader.load(path).is_null());
        std::remove(path.c_str());
    }

    // glTF 1.0
    {
        const std::string path = "orca_gltf_legacy_test.gltf";
        write_text_file(path, R"JSON({"asset": {"version": "1.0"}, "scenes": [{"nodes": []}]})JSON");
        CHECK(loader.load(path).is_null());
        std::remove(path.c_str());
    }

    // 没有 nodes
    {
        const std::string path = "orca_gltf_empty_test.gltf";
        write_text_file(path, R"JSON({"asset": {"version": "2.0"}})JSON");
        CHECK(loader.load(path).is_null());
        std::remove(path.c_str());
    }

    // 必需的压缩扩展无法处理
    {
        const std::string path = "orca_gltf_required_extension_test.gltf";
        write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_draco_mesh_compression"],
  "extensionsRequired": ["KHR_draco_mesh_compression"],
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "root"}]
})JSON");
        CHECK(loader.load(path).is_null());
        std::remove(path.c_str());
    }
}

TEST_CASE("ResourceLoader: 缓存 glTF 场景并按大写扩展名命中")
{
    const std::vector<uint8_t> png = build_png_rgb(1, 1, {10, 20, 30});
    const std::string buffer_uri =
        "data:application/octet-stream;base64," + base64_encode(build_triangle_buffer());
    const std::string image_uri = "data:image/png;base64," + base64_encode(png);

    const std::string path = "orca_gltf_cache_test.GLTF";
    write_text_file(path, make_triangle_gltf(buffer_uri, image_uri));

    ResourceCache::create_singleton();
    ResourceLoader::create_singleton();

    Ref<Resource> first = ResourceLoader::get_singleton()->load(path);
    Ref<Resource> second = ResourceLoader::get_singleton()->load(path);

    // 同一个 URL 命中缓存，返回同一实例
    REQUIRE(first.is_valid());
    CHECK(second.is_valid());
    CHECK(first.ptr() == second.ptr());
    CHECK(first.cast<PackedScene>().is_valid());

    ResourceLoader::destroy_singleton();
    ResourceCache::destroy_singleton();
    std::remove(path.c_str());
}
