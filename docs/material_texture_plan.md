# 材质纹理支持实现方案

> 目标：在现有 metallic / roughness / albedo 标量材质基础上，增加颜色（albedo）、法线（normal）、粗糙度（roughness）、金属度（metallic）、AO 五类纹理。
>
> 约束：纹理必须走 `ResourceLoader` / `ResourceCache` 的资源加载链路；`orca-resource` 仍然不能链接 `orca-rendering`，资源只保存 CPU 像素与可回填的 RID。

---

## 1. 现状分析

### 1.1 已经具备的骨架

- `Resource` / `ResourceCache` / `ResourceLoader` / `ResourceFormatLoader` 已经存在。
- `Mesh` 已经采用 “CPU 数据 + `rid_dirty` + 由渲染服务器回填 RID” 的模式，可直接作为 `ImageTexture` 的参考。
- `ObjMeshLoader` 已注册，`MeshInstance3D` 在进入场景树时完成 mesh / material 的一次性上传。
- `RenderingServer` 已有 `MaterialData`、`RIDStorage`，`RIDType::Texture` 也已预留。
- `RenderingDevice` 已有 mesh / shader / uniform 抽象，OpenGL 后端可继续扩展。

### 1.2 当前缺口

| 位置 | 现状 | 需要补的内容 |
|---|---|---|
| `Resource` | 只有 `get_url()`，没有 `set_url()` | loader 加载完成后写入 url，供缓存使用 |
| `ResourceCache` | 未被 `ResourceLoader` 使用；`cache.at(url)` 找不到会抛异常 | `load()` 先查缓存；`get_resource()` 使用 `find` 并返回空 Ref |
| `ResourceLoader` | 只注册 ObjMeshLoader；扩展名直接取自 `Path::get_extension()` | 注册 ImageTextureLoader；统一扩展名大小写和点号 |
| `ResourceFormatLoader` | `load(const std::string url)` 与派生类 `const std::string &` 不一致 | 统一签名，避免 `ImageTextureLoader` 没有真正 override |
| `ImageTexture` | 空壳，只有 RID | 保存宽高、像素、sRGB、mipmap、采样参数，并支持 dirty/回填 RID |
| `ImageTextureLoader` | 返回空 Ref，扩展名是 `"png"`（无点），未注册 | 用 stb_image 解码，返回 `ImageTexture`，注册 `.png` / `.jpg` 等 |
| `StandardMaterial` | 纹理槽只有 `RID`，没有 albedo 纹理；没有 setter/getter | 改为 `Ref<ImageTexture>` 槽位 + 对应 setter/getter |
| `MaterialData` | 只有标量 | 增加 5 个纹理 RID |
| `RenderingStorage` | 没有 `TextureData` 和 textures storage | 增加纹理 RIDStorage |
| `RenderingServer` | 没有纹理 API，也没有把材质纹理传给 draw | 增加 create/upload/set_material_texture，并在 draw 中绑定 |
| `RenderingDevice` | 没有纹理接口 | 增加 2D 纹理创建 / 销毁 / 绑定 |
| shader | 只采样顶点颜色参数，没有 sampler | 采样 5 张纹理，并实现法线贴图 TBN |
| `MeshInstance3D` | 只上传 mesh 和材质标量 | 上传纹理并绑定到材质 |
| CMake / tests | 没有 image loader、stb、纹理测试 | 补源文件、第三方库、fixture 和单测 |

---

## 2. 总体数据流

```
.material / 代码
      │
      ▼
ResourceLoader::load("res://textures/xxx.png")
      │  查 ResourceCache
      ▼
ImageTextureLoader ── stb_image ──> ImageTexture（CPU: width/height/RGBA8/srgb）
      │
      ▼
MeshInstance3D 进入场景树
      │  1. 确保 mesh 已上传
      │  2. 确保 material 已上传
      │  3. 确保每张 ImageTexture 已上传
      ▼
RenderingServer::set_material_texture(mat, slot, texture_rid)
      │
      ▼
MaterialData 保存 5 个纹理 RID
      │
      ▼
draw(): bind texture units 0..4 -> shader 中 sampler2D 采样
```

关键点：**`ImageTexture` 只保存 CPU 数据；`RenderingServer` 负责创建 GPU 纹理并回填 `ImageTexture::rid`。**

---

## 3. 分阶段实现

### 阶段 0：修通资源加载与缓存（必须先做）

当前 `ResourceLoader::load()` 每次都会新建资源，缓存实际没有生效；而 `ImageTexture` 的 url 又必须能写回，否则析构时无法从缓存移除。

建议修改：

```cpp
class Resource : public RefCounted
{
    std::string url;
public:
    void set_url(const std::string &p_url) { url = p_url; }
    const std::string &get_url() const { return url; }
    ...
};
```

```cpp
Ref<Resource> ResourceLoader::load(const std::string &url)
{
    if (auto cache = ResourceCache::get_singleton())
    {
        if (Ref<Resource> cached = cache->get_resource(url); cached.is_valid())
            return cached;
    }

    std::string ext = to_lower(Path::get_extension(url));
    auto it = loaders.find(ext);
    if (it == loaders.end() || it->second == nullptr)
        return Ref<Resource>(nullptr);

    Ref<Resource> resource = it->second->load(url);
    if (resource.is_valid())
    {
        resource->set_url(url);
        if (auto cache = ResourceCache::get_singleton())
            cache->save_resource(resource.ptr());
    }
    return resource;
}
```

同时：

- `ResourceCache::get_resource()` 用 `find`，不存在时返回空 `Ref`。
- `ResourceFormatLoader::load` 统一为 `virtual Ref<Resource> load(const std::string &url) = 0;`。
- 扩展名统一带点、转小写，例如 `.obj`、`.png`。
- `ImageTextureLoader::format_extension()` 加 `override`。

> 这一步完成后，同一路径加载两次会返回同一个资源实例，纹理也能享受缓存。

---

### 阶段 1：`ImageTexture` 资源

`resource/texture/image_texture.h/.cpp`

```cpp
class ImageTexture : public Resource
{
public:
    bool create_from_rgba8(
        int width,
        int height,
        std::vector<uint8_t> pixels,
        bool srgb,
        bool generate_mipmaps = true,
        bool repeat = true,
        bool linear_filter = true);

    int get_width() const;
    int get_height() const;
    bool is_srgb() const;
    bool has_mipmaps() const;
    bool is_repeat() const;
    bool is_linear_filter() const;

    const std::vector<uint8_t> &get_pixels() const;

    // Resource
    RID get_rid() const override { return rid; }
    Ref<Resource> duplicate() const override;

    // 渲染服务器上传完成后回填
    bool is_rid_dirty() const { return rid_dirty; }
    void set_rid(RID p_rid) { rid = p_rid; rid_dirty = false; }
    void mark_rid_dirty() { rid_dirty = true; }

private:
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // 固定 RGBA8，每像素 4 字节
    bool srgb = false;
    bool generate_mipmaps = true;
    bool repeat = true;
    bool linear_filter = true;

    RID rid = 0;
    bool rid_dirty = true;
};
```

设计选择：

- 统一解码成 **RGBA8**，简单且与 OpenGL / Vulkan 上传路径一致；后续再考虑 RGB8、R8、压缩格式。
- `srgb` 由 loader / 材质槽决定：
  - albedo、emissive 纹理：sRGB；
  - normal、roughness、metallic、AO：线性。
- `duplicate()` 深拷贝像素，RID 置 0 并标记 dirty。
- 资源模块不引用 GL / Vulkan 类型。

---

### 阶段 2：`ImageTextureLoader` + stb_image

当前 `thirdparty/` 没有图像解码库。建议引入单头文件 `stb_image.h`：

```
thirdparty/stb/stb_image.h
thirdparty/stb/stb_image.cpp   // #define STB_IMAGE_IMPLEMENTATION
```

CMake：

```cmake
add_library(orca-stb STATIC stb/stb_image.cpp)
target_include_directories(orca-stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
```

`resource/loaders/image_texture_loader.cpp`：

```cpp
Ref<Resource> ImageTextureLoader::load(const std::string &url)
{
    const std::string path = Path::replace_url(url);

    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(1); // 对齐 OBJ/OpenGL 的 UV 原点
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (data == nullptr)
    {
        std::printf("[ERROR] failed to load image: %s (%s)\n", url.c_str(), stbi_failure_reason());
        return Ref<Resource>(nullptr);
    }

    std::vector<uint8_t> pixels(data, data + width * height * 4);
    stbi_image_free(data);

    ImageTexture *texture = memnew(ImageTexture);
    texture->create_from_rgba8(width, height, std::move(pixels), /*srgb=*/true);
    return Ref<Resource>(texture);
}
```

注意事项：

- `stbi_set_flip_vertically_on_load(1)` 与现有 OBJ 的 UV 约定保持一致，避免贴图上下颠倒。若后续统一使用 DirectX 风格，可改为在 shader 里翻转 V。
- 扩展名支持：`.png`、`.jpg`、`.jpeg`，可选 `.tga`、`.bmp`。
- 如果要支持“同一图像分别按 sRGB / 线性使用”，可以在 `StandardMaterial::set_texture()` 时显式设置 `ImageTexture::set_srgb()`，或者在材质文件里声明。前期约定按槽位处理即可。
- 若不想在 loader 里直接使用 `stbi_load` 读绝对路径，可以给 `FileAccess` 增加 `get_buffer()`，用 `stbi_load_from_memory` 解码，保持资源读取抽象；这是更干净的长期方案。

注册 loader：

```cpp
void ResourceLoader::create_singleton()
{
    singleton = memnew(ResourceLoader);
    singleton->register_loader(memnew(ObjMeshLoader));
    singleton->register_loader(memnew(ImageTextureLoader));
}
```

---

### 阶段 2.1：`ResourceLoader` 支持“多扩展名 → 同一个 loader”

当前 `ResourceFormatLoader` 只能返回一个扩展名，`ResourceLoader` 也是
`extension -> loader` 的一对一映射。图像 loader 需要同时处理 `.png`、`.jpg`、
`.jpeg`、`.bmp`、`.tga` 等格式，因此要把“一个 loader 对应一个扩展名”改成
“一个 loader 声明一组扩展名”。

#### 1. 抽象基类改为扩展名列表

```cpp
class ResourceFormatLoader
{
public:
    virtual ~ResourceFormatLoader() = default;

    virtual Ref<Resource> load(const std::string &url) = 0;

    /// 返回不带点、小写的扩展名，例如 {"png", "jpg", "jpeg"}。
    virtual std::vector<std::string> get_recognized_extensions() const = 0;
};
```

`ObjMeshLoader`：

```cpp
std::vector<std::string> get_recognized_extensions() const override
{
    return { "obj" };
}
```

`ImageTextureLoader`：

```cpp
std::vector<std::string> get_recognized_extensions() const override
{
    // 只列出当前解码路径真正支持的格式。
    // stb_image 会根据文件内容自动识别格式，因此一个 loader 就够了。
    return { "png", "jpg", "jpeg", "bmp", "tga" };
}

Ref<Resource> load(const std::string &url) override
{
    // stbi_load 从文件头识别实际格式，不需要按扩展名 switch。
}
```

#### 2. `ResourceLoader` 的注册表

```cpp
class ResourceLoader
{
public:
    bool register_loader(ResourceFormatLoader *loader);
    Ref<Resource> load(const std::string &url);
    bool has_loader_for(const std::string &url) const;
    std::vector<std::string> get_supported_extensions() const;

private:
    static std::string normalize_extension(const std::string &extension);

    // 同一个 loader 会被多个扩展名引用，因此不能按扩展名逐个 delete。
    std::vector<ResourceFormatLoader *> owned_loaders;
    std::unordered_map<std::string, ResourceFormatLoader *> extension_to_loader;
};
```

注册时展开扩展名列表：

```cpp
bool ResourceLoader::register_loader(ResourceFormatLoader *loader)
{
    if (loader == nullptr)
        return false;

    bool all_registered = true;
    for (const std::string &raw_extension : loader->get_recognized_extensions())
    {
        const std::string extension = normalize_extension(raw_extension);
        if (extension.empty())
        {
            all_registered = false;
            continue;
        }

        auto [it, inserted] = extension_to_loader.emplace(extension, loader);
        if (!inserted)
        {
            // 同一扩展名被重复注册：明确策略，不要静默覆盖。
            // 这里选择保留先注册的 loader，并打印警告。
            std::printf("[WARN] extension .%s is already registered\n", extension.c_str());
            all_registered = false;
        }
    }

    // 即使部分扩展名冲突，loader 本身仍由 ResourceLoader 持有，避免泄漏。
    owned_loaders.push_back(loader);
    return all_registered;
}
```

> 注意：现有析构函数是遍历 `loaders` map 并 `memdelete`。改成多扩展名后，同一个
> loader 指针会在 map 中出现多次，继续按 map 析构会 **重复释放**。必须改成
> `owned_loaders` 只保存一次，并在析构时遍历它释放。

扩展名统一规范化：

```cpp
std::string ResourceLoader::normalize_extension(const std::string &extension)
{
    std::string ext = extension;
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());

    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}
```

#### 3. `load()` 统一走缓存 + 扩展名规范化

```cpp
Ref<Resource> ResourceLoader::load(const std::string &url)
{
    if (auto cache = ResourceCache::get_singleton())
    {
        if (Ref<Resource> cached = cache->get_resource(url); cached.is_valid())
            return cached;
    }

    const std::string extension = normalize_extension(Path::get_extension(url));
    if (extension.empty())
    {
        std::printf("[ERROR] resource has no extension: %s\n", url.c_str());
        return Ref<Resource>(nullptr);
    }

    auto it = extension_to_loader.find(extension);
    if (it == extension_to_loader.end() || it->second == nullptr)
    {
        std::printf("[ERROR] no resource loader for extension .%s: %s\n",
                    extension.c_str(), url.c_str());
        return Ref<Resource>(nullptr);
    }

    Ref<Resource> resource = it->second->load(url);
    if (resource.is_valid())
    {
        resource->set_url(url);
        if (auto cache = ResourceCache::get_singleton())
            cache->save_resource(resource.ptr());
    }
    return resource;
}
```

#### 4. 为什么图像格式不需要多个 loader

`ResourceLoader` 的职责是“根据扩展名找到能处理该文件的 loader”，不是“每种格式
对应一个 loader”。图像文件虽然扩展名不同，但都交给同一个 `ImageTextureLoader`，
由 stb_image 根据文件头识别 PNG/JPEG/BMP/TGA，再统一解码成 RGBA8 像素。

需要区分的是两类“格式”：

- **文件容器格式**：PNG、JPG、BMP、TGA。它们都可以解码成 RGBA8，走同一个
  `ImageTextureLoader`，只需要在 `get_recognized_extensions()` 里多列几项。
- **像素格式**：RGBA8、R8、RGB16F、HDR 等。如果以后支持 `.hdr` / `.exr`，
  它们需要 `stbi_loadf` 或专用 loader，并且 `ImageTexture` 要增加像素格式字段，
  这时才需要新的资源类型/加载分支，而不是简单多注册一个扩展名。

#### 5. 可选的进一步扩展

如果以后出现“同一扩展名需要按内容选择 loader”或“同一文件可以加载成不同类型”的
需求，可以在当前扩展名映射之上再加一层：

```cpp
virtual bool handles_type(const std::string &resource_type) const;
Ref<Resource> load(const std::string &url, const std::string &type_hint = "");
```

例如 `.tex` 既可以加载成普通 `ImageTexture`，也可以加载成带 mip 链的压缩纹理。
但当前 mini 引擎不需要这一层，保持“扩展名 → loader”映射即可，避免过度设计。

### 阶段 3：`StandardMaterial` 增加纹理槽

`resource/material/material.h`

```cpp
enum class MaterialTextureSlot : uint8_t
{
    Albedo = 0,
    Normal,
    Roughness,
    Metallic,
    AO,
    Max
};

class StandardMaterial : public Resource
{
public:
    void set_texture(MaterialTextureSlot slot, const Ref<ImageTexture> &texture);
    Ref<ImageTexture> get_texture(MaterialTextureSlot slot) const;

    void set_albedo(const Color &color);
    void set_metallic(float metallic);
    void set_roughness(float roughness);
    void set_normal_scale(float scale);
    void set_ao_strength(float strength);
    ...

private:
    Color albedo = Color(255, 255, 255);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normal_scale = 1.0f;
    float ao_strength = 1.0f;

    Ref<ImageTexture> textures[static_cast<int>(MaterialTextureSlot::Max)];

    RID rid = 0;       // 渲染服务器侧材质 RID
    bool rid_dirty = true;
};
```

要点：

- 删除/替换现有的 `RID normal_texture` 等字段。资源侧保存 `Ref<ImageTexture>`，GPU 纹理 RID 保存在 `ImageTexture` 自己身上。
- 补齐 `duplicate()`，复制标量和纹理 `Ref`。
- `set_texture()` 只修改资源数据并标记 dirty；真正上传由场景/渲染同步阶段完成。
- 修正现有 `get_roughenss()` 拼写，保留兼容别名可选。
- 纹理槽语义：
  - Albedo：RGB 乘到 base color，A 可选用于透明度；
  - Normal：切线空间法线，OpenGL 约定 +Y；
  - Roughness：取 G 通道（兼容 ORM / glTF metal-rough）；
  - Metallic：取 B 通道（兼容 ORM）；
  - AO：取 R 通道（兼容 ORM）。

> 推荐直接支持 ORM 打包图：把同一张 ORM 纹理同时设给 Roughness / Metallic / AO，shader 分别取 G/B/R。这样既支持独立贴图，也支持常见的打包贴图。

---

### 阶段 4：`RenderingDevice` 增加纹理接口

`servers/rendering/rendering_device.h`

```cpp
struct TextureDesc
{
    uint32_t width = 0;
    uint32_t height = 0;
    bool srgb = false;
    bool generate_mipmaps = true;
    bool repeat = true;
    bool linear_filter = true;
};

virtual uint32_t create_texture_2d(const TextureDesc &desc, const void *rgba8_pixels) = 0;
virtual void destroy_texture(uint32_t texture) = 0;
virtual void bind_texture(uint32_t unit, uint32_t texture) = 0;
```

OpenGL 实现要点：

```cpp
GLuint texture = 0;
glGenTextures(1, &texture);
glBindTexture(GL_TEXTURE_2D, texture);

glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
const GLint internal_format = desc.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
glTexImage2D(GL_TEXTURE_2D, 0, internal_format,
             desc.width, desc.height, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, rgba8_pixels);

if (desc.generate_mipmaps)
    glGenerateMipmap(GL_TEXTURE_2D);

glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                desc.generate_mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
```

绑定：

```cpp
void GLRenderingDevice::bind_texture(uint32_t unit, uint32_t texture)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}
```

Vulkan 后端当前整体是空实现，保持同样的 stub 即可，不阻塞 OpenGL 主线。

---

### 阶段 5：`RenderingStorage` / `RenderingServer`

#### 5.1 TextureData

```cpp
struct TextureData
{
    uint32_t texture = 0; // device 侧纹理句柄
    int width = 0;
    int height = 0;
    bool has_data = false;
};

class RenderingStorage
{
public:
    ...
    RIDStorage<TextureData> textures{RIDType::Texture};
};
```

#### 5.2 纹理 API

```cpp
RID create_texture();
void set_texture_image(
    RID texture,
    int width,
    int height,
    const std::vector<uint8_t> &rgba8_pixels,
    bool srgb,
    bool generate_mipmaps,
    bool repeat,
    bool linear_filter);
```

`set_texture_image()` 内部：

1. 取 `TextureData*`；
2. 如果已有 GPU 纹理，先 `destroy_texture()`；
3. 调 `device->create_texture_2d(desc, pixels.data())`；
4. 保存句柄、尺寸、`has_data = true`。

`free(RID)` 增加：

```cpp
case RIDType::Texture:
    if (TextureData *data = storage.textures.get_data(rid))
        if (data->texture != 0)
            RenderingDevice::get_singleton()->destroy_texture(data->texture);
    storage.textures.free(rid);
    break;
```

#### 5.3 MaterialData

```cpp
struct MaterialData
{
    Color albedo = Color(255, 255, 255);
    float metallic = 0;
    float roughness = 0.5f;
    float specular = 1.0f;
    float normal_scale = 1.0f;
    float ao_strength = 1.0f;
    float emission = 0.0f;

    RID albedo_texture = 0;
    RID normal_texture = 0;
    RID roughness_texture = 0;
    RID metallic_texture = 0;
    RID ao_texture = 0;
};
```

```cpp
void set_material_texture(RID material, MaterialTextureSlot slot, RID texture);
```

`MaterialTextureSlot` 目前定义在 resource 层。为了避免 server 反向依赖 resource，可以在 `rendering_storage.h` 定义一份同样的枚举，或者在 core 层定义共享枚举；小引擎阶段建议在 `rendering_storage.h` 里定义 `enum class MaterialTextureSlot`，由 scene 层做一次转换。

#### 5.4 默认纹理

在 `RenderingServer::init()` 中创建：

- `default_white_texture`：RGBA8 (255,255,255,255)，用于 albedo / roughness / metallic / AO 的缺省值；
- `default_normal_texture`：RGBA8 (128,128,255,255)，用于法线。

这样 shader 不需要 `if (has_texture)` 分支，缺省材质也能保持原行为。

在 `draw()` 中绑定：

```cpp
const uint32_t albedo_tex = material->albedo_texture
    ? storage.textures.get_data(material->albedo_texture)->texture
    : storage.textures.get_data(default_white_texture)->texture;
...
device->bind_texture(0, albedo_tex);
device->bind_texture(1, normal_tex);
device->bind_texture(2, roughness_tex);
device->bind_texture(3, metallic_tex);
device->bind_texture(4, ao_tex);
```

sampler uniform 只需在 `bind_shader_program()` 之后设置一次：

```cpp
set_uniform_int(program, "u_albedo_texture", 0);
set_uniform_int(program, "u_normal_texture", 1);
set_uniform_int(program, "u_roughness_texture", 2);
set_uniform_int(program, "u_metallic_texture", 3);
set_uniform_int(program, "u_ao_texture", 4);
```

---

### 阶段 6：场景层上传纹理

`MeshInstance3D::_notification(NOTIFICATION_ENTER_TREE)` 目前已经负责 mesh / material 的上传。建议抽出三个 helper，避免代码继续膨胀：

```cpp
void ensure_mesh_uploaded(RenderingServer *server, const Ref<Mesh> &mesh);
void ensure_texture_uploaded(RenderingServer *server, const Ref<ImageTexture> &texture);
void ensure_material_uploaded(RenderingServer *server, const Ref<StandardMaterial> &material);
```

`ensure_texture_uploaded()`：

```cpp
if (texture.is_null())
    return;

if (!texture->get_rid())
    texture->set_rid(server->create_texture());

if (texture->is_rid_dirty())
{
    server->set_texture_image(
        texture->get_rid(),
        texture->get_width(),
        texture->get_height(),
        texture->get_pixels(),
        texture->is_srgb(),
        texture->has_mipmaps(),
        texture->is_repeat(),
        texture->is_linear_filter());
    texture->set_rid(texture->get_rid()); // 清除 dirty
}
```

`ensure_material_uploaded()`：

```cpp
if (material.is_null() || material->get_rid())
    return;

material->set_rid(server->create_material());
server->set_material_albedo(...);
server->set_material_roughness(...);
server->set_material_metallic(...);
server->set_material_normal_scale(...);

for (每个纹理槽)
{
    Ref<ImageTexture> texture = material->get_texture(slot);
    ensure_texture_uploaded(server, texture);
    if (texture.is_valid())
        server->set_material_texture(material->get_rid(), slot, texture->get_rid());
}
```

注意：

- 同一张纹理被多个材质/MeshInstance 共享时，依靠 `ImageTexture::rid` 只上传一次。
- 资源热更新可以后续通过 `Resource::emit_changed()` + `mark_rid_dirty()` 触发重新上传；本期先做进入场景树时的一次性上传。
- 如果 mesh 有多个 surface，当前只处理 surface 0，纹理方案不需要改变这个限制，但后续应补多 surface。

---

### 阶段 7：shader

#### 7.1 顶点 shader

增加世界坐标输出：

```glsl
out vec3 v_world_position;
...
v_world_position = surface_world_position.xyz;
```

#### 7.2 片元 shader

```glsl
uniform sampler2D u_albedo_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_roughness_texture;
uniform sampler2D u_metallic_texture;
uniform sampler2D u_ao_texture;

uniform float u_normal_scale;
uniform float u_ao_strength;
uniform vec2 u_uv_scale;

in vec3 v_world_position;
```

采样：

```glsl
vec2 uv = v_uv * u_uv_scale;

vec4 base_color = u_albedo * texture(u_albedo_texture, uv);
vec3 albedo = base_color.rgb;

float metallic = clamp(u_metallic * texture(u_metallic_texture, uv).b, 0.0, 1.0);
float roughness = clamp(u_roughness * texture(u_roughness_texture, uv).g, 0.04, 1.0);
float ao = mix(1.0, texture(u_ao_texture, uv).r, u_ao_strength);
```

法线贴图：由于 `Mesh::Surface` 目前没有切线属性，推荐先用屏幕空间导数构造 TBN：

```glsl
vec3 get_normal()
{
    vec3 n = normalize(v_normal);
    vec3 dp1 = dFdx(v_world_position);
    vec3 dp2 = dFdy(v_world_position);
    vec2 duv1 = dFdx(v_uv);
    vec2 duv2 = dFdy(v_uv);

    vec3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 B = normalize(cross(n, T));
    T = normalize(cross(B, n));

    vec3 tangent_normal = texture(u_normal_texture, uv).xyz * 2.0 - 1.0;
    tangent_normal.xy *= u_normal_scale;
    return normalize(T * tangent_normal.x + B * tangent_normal.y + n * tangent_normal.z);
}
```

后续如果性能或质量不够，再在 OBJ loader / mesh surface 中生成切线并作为顶点属性上传。

AO 建议只乘在 ambient 项：

```glsl
vec3 ambient = albedo * u_ambient_color.rgb * u_ambient_strength * ao;
```

如果希望更明显地看到 AO，也可以临时乘到整个 `Lo + ambient`，但物理上推荐只影响环境光。

---

### 阶段 8：材质文件加载（推荐，形成完整资源链路）

为了真正体现“用 Resource 的加载方式”，建议实现一个极简文本材质 loader，注册 `.material`：

```ini
type=StandardMaterial
albedo=1.0,0.8,0.2,1.0
metallic=1.0
roughness=0.35
normal_scale=1.0
ao_strength=1.0

albedo_texture=res://textures/gold_albedo.png
normal_texture=res://textures/gold_normal.png
roughness_texture=res://textures/gold_orm.png
metallic_texture=res://textures/gold_orm.png
ao_texture=res://textures/gold_orm.png
```

`MaterialLoader` 逐行解析，遇到 `*_texture` 时递归调用 `ResourceLoader::get_singleton()->load(value)`，并 `cast<ImageTexture>()` 后写入材质槽。

这样应用代码只需要：

```cpp
Ref<StandardMaterial> material =
    ResourceLoader::get_singleton()->load("res://materials/gold.material").cast<StandardMaterial>();
MeshInstance3D *instance = memnew(MeshInstance3D(mesh, material));
```

这也是后续 `.obj` + `.mtl` 自动绑定纹理的入口。

---

## 4. 推荐落地顺序

1. **修通 ResourceLoader / ResourceCache / set_url**（否则纹理缓存和生命周期都不正确）。
2. **实现 ImageTexture + stb_image + ImageTextureLoader**，用单测确认能解码 PNG 并命中缓存。
3. **扩展 RenderingDevice 纹理 API + OpenGL 实现**。
4. **扩展 RenderingStorage / RenderingServer / MaterialData**，加入 TextureData、材质纹理 RID、默认纹理。
5. **在 MeshInstance3D 中上传纹理并绑定到材质**。
6. **修改 shader，接入 5 个 sampler 和法线贴图**。
7. **实现 `.material` loader（可选但推荐）**，并准备一组测试贴图。
8. **补 CMake、资源拷贝和测试**。

前 6 步完成后即可在现有 `test_application` 中把某个球的材质换成带 albedo/normal/ORM 的材质，验证完整链路。

---

## 5. 关键设计决策与注意事项

### 5.1 sRGB 与线性

- albedo 纹理使用 `GL_SRGB8_ALPHA8`，让 GPU 自动解码到线性空间。
- normal / roughness / metallic / AO 使用 `GL_RGBA8`，不能做 sRGB 解码。
- 同一张图如果同时用于 albedo 和线性槽位，需要明确约定或复制资源；前期建议按槽位区分图片用途。

### 5.2 纹理通道约定

推荐兼容 glTF / ORM：

| 槽位 | 采样通道 | 缺省值 |
|---|---|---|
| Albedo | RGB | 白色 |
| Normal | RGB | (0.5, 0.5, 1.0) |
| Roughness | G | 1.0 |
| Metallic | B | 1.0 |
| AO | R | 1.0 |

这样 `roughness_texture` / `metallic_texture` / `ao_texture` 可以指向同一张 ORM 图。材质的标量 `roughness` / `metallic` 作为乘数，与纹理采样值相乘。

### 5.3 UV 方向

- stb_image 解码时 `flip_vertically_on_load(1)`，与 OpenGL / OBJ 的 UV 原点保持一致。
- 如果后续发现法线贴图绿色通道方向不对，再单独处理 normal map 的 G 翻转（DirectX vs OpenGL 约定）。

### 5.4 纹理资源生命周期

- `ResourceCache` 是弱引用，不延长资源寿命；资源析构时自动从缓存移除。
- `ImageTexture` 的 RID 由渲染服务器持有。资源析构不会立刻释放 GPU 纹理，除非场景层或渲染服务器有显式释放策略；后续可增加 `Resource::emit_changed()` 或引用计数回调来释放。
- 与 `Mesh` 保持一致：资源析构空实现，RID 生命周期由渲染服务器管理，避免资源模块反向依赖渲染层。

### 5.5 不要破坏现有分层

- `orca-resource` 不新增对 `orca-rendering` 的链接。
- `ImageTexture` 只保存 CPU 数据和 `RID`，不包含 GL / Vulkan 类型。
- 场景层（`scene/3d/mesh_instance_3d.h`）负责把资源翻译成 RenderingServer 调用。

### 5.6 现有代码的小问题

- `ImageTextureLoader::format_extension()` 返回 `"png"`，而 `Path::get_extension()` 返回 `".png"`，当前永远匹配不到。
- `ImageTextureLoader::load` 的 `const std::string &` 与基类 `const std::string url` 签名不同，不是真正的 override。
- `ResourceCache::get_resource()` 使用 `cache.at()`，未命中会抛出异常。
- `StandardMaterial::duplicate()` 返回空 Ref。
- `StandardMaterial::get_roughenss()` 拼写错误。
- `RenderingServer::draw()` 中 `SceneData` / `EnvironmentData` / `CameraData` 没有判空就直接解引用，纹理接入时不要扩大这个风险。

---

## 6. 测试计划

### 单元测试（不依赖窗口/渲染后端）

- `ImageTextureLoader`
  - 加载一张 2x2 测试 PNG，检查宽高、像素、sRGB 标记。
  - 文件不存在时返回空 Ref。
  - 同一路径 `ResourceLoader::load()` 两次返回同一实例。
- `StandardMaterial`
  - 设置 5 个纹理槽后能在对应槽位取回。
  - `duplicate()` 后纹理 Ref 有效且 RID 为 0。
- `ResourceCache`
  - 未命中返回空 Ref；插入后能命中；资源析构后自动移除。
- 纹理数据
  - `ImageTexture::create_from_rgba8` 的尺寸/像素校验。
  - `set_rid` 后 `is_rid_dirty()` 为 false。

### 集成测试（有 OpenGL 上下文）

- 在 `test_application` 中把现有球体材质替换为带 albedo + normal + ORM 的材质。
- 检查：
  - 纹理能正常显示；
  - 法线方向正确；
  - roughness / metallic / AO 影响符合预期；
  - 同一张纹理被多个实例共享时只上传一次（可加日志或计数验证）。

### 资源准备

建议目录：

```
assets/
    textures/
        test_albedo.png
        test_normal.png
        test_orm.png
    materials/
        test.material
```

并在 CMake 中把 `assets/` 拷贝到 `build/resources/`，保证 `res://textures/test_albedo.png` 在运行时可用。

---

## 7. 最小可用版本（MVP）范围

如果只想最快看到效果，可以按以下最小范围实现：

1. `Resource::set_url` + `ResourceLoader` 缓存。
2. `ImageTexture` + `stb_image` + `ImageTextureLoader`（只支持 `.png`）。
3. `RenderingDevice::create_texture_2d / bind_texture`（OpenGL）。
4. `TextureData` + `RenderingServer::create_texture / set_texture_image`。
5. `MaterialData` 增加 5 个 RID，`StandardMaterial` 用 `Ref<ImageTexture>` 保存槽位。
6. `MeshInstance3D` 上传纹理。
7. shader 采样 5 张纹理 + 简单法线贴图。

暂不做：纹理压缩、KTX、HDR、纹理流式加载、热重载、多 surface、Vulkan 真实实现、材质文件格式。这样可以在不推翻现有架构的前提下，把五类纹理完整接进渲染链路。
