#include "rendering_server.h"
#include "rendering_uniforms.h"

namespace
{
    SamplerHandle create_standard_material_sampler()
    {
        auto rendering_device = RenderingDeviceRefactor::get_singleton();
        SamplerDesc sampler_desc{
            .address_u = AddressMode::Repeat,
            .address_v = AddressMode::Repeat,
            .address_w = AddressMode::Repeat,
            .mag_filter = FilterMode::Linear,
            .min_filter = FilterMode::Linear,
            .mipmap_filter = FilterMode::Linear,
        };
        return rendering_device->create_sampler(sampler_desc);
    }

    void validate_material_data(const MaterialData *material_data)
    {
        if (material_data == nullptr)
        {
            throw std::invalid_argument("invalid material RID");
        }
        if (!material_data->bind_group.is_valid())
        {
            throw std::invalid_argument("invalid material BindGroup");
        }
        if (!material_data->uniform_buffer.is_valid())
        {
            throw std::invalid_argument("invalid material uniform buffer");
        }
    }
};

RID RenderingServer::create_material()
{
    return storage.materials.create(MaterialData());
}

RID RenderingServer::create_material_with_texture(const std::vector<RID> &texture_slots)
{
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    // bind group规则，材质固定在group(1)
    // binding(0) 为uniform buffer
    // binding(1) PBR纹理共用sampler
    // binding(2~6) 纹理槽
    BindGroupDesc desc;
    MaterialData material_data;

    GPUBufferDesc uniform_buffer_desc;
    uniform_buffer_desc.size = sizeof(MaterialUniforms);
    uniform_buffer_desc.usage = GPUBufferUsage::UNIFORM | GPUBufferUsage::COPY_DST;
    material_data.uniform_buffer = rendering_device->create_buffer(uniform_buffer_desc);

    // binding(0) uniform参数
    BindGroupEntry uniform_entry;
    uniform_entry.binding = 0;
    uniform_entry.resource = BindingResource{
        .type = BindingType::UniformBuffer,
        .buffer = material_data.uniform_buffer,
    };
    desc.entries.emplace_back(uniform_entry);

    // binding(1) texture sampler
    material_data.sampler = create_standard_material_sampler();
    BindGroupEntry sampler_entry{
        .binding = 1,
        .resource = BindingResource{
            .type = BindingType::Sampler,
            .sampler = material_data.sampler,
        }};
    desc.entries.emplace_back(sampler_entry);

    // binding(2~7) texture
    uint8_t slot = -1;
    material_data.texture_views.resize(6);
    for (const RID texture : texture_slots)
    {
        slot++;
        const auto texture_data = storage.textures.get_data(texture);
        if (texture_data == nullptr || !texture_data->handle.is_valid())
            continue;
        // 创建view
        const auto texture_view = rendering_device->create_texture_view(texture_data->handle, TextureViewDesc{});
        material_data.texture_views[slot] = texture_view;

        BindGroupEntry texture_entry{
            .binding = 2 + slot,
            .resource = BindingResource{
                .type = BindingType::Texture2D,
                .texture_view = texture_view,
            },
        };
        desc.entries.emplace_back(texture_entry);
    }

    return storage.materials.create(MaterialData{});
}

void RenderingServer::set_material_albedo(RID mat, const Color &color)
{
    auto material_data = storage.materials.get_data(mat);
    validate_material_data(material_data);
    material_data->albedo = color;
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    real_t norm[4];
    color.normalize_rgba(norm);
    rendering_device->queue_write_buffer(material_data->uniform_buffer, 0, norm, sizeof(norm));
}

void RenderingServer::set_material_roughness(RID mat, float roughness)
{
    auto material_data = storage.materials.get_data(mat);
    validate_material_data(material_data);
    material_data->roughness = roughness;
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    float data[1] = {roughness};
    rendering_device->queue_write_buffer(material_data->uniform_buffer, 32, data, sizeof(float));
}

void RenderingServer::set_material_metallic(RID mat, float metallic)
{
    auto material_data = storage.materials.get_data(mat);
    validate_material_data(material_data);
    material_data->metallic = metallic;
    auto rendering_device = RenderingDeviceRefactor::get_singleton();
    float data[1] = {metallic};
    rendering_device->queue_write_buffer(material_data->uniform_buffer, 16, data, sizeof(float));
}

void RenderingServer::set_material_scales(RID mat, const Vector2 &uv_scale, float parallax_scale, bool parallax_flip)
{
    // TODO
}

void RenderingServer::set_material_normal_scale(RID mat, float normal_scale)
{
    // TODO
}

void RenderingServer::set_material_ao_strength(RID mat, float ao_strength)
{
    // TODO
}
