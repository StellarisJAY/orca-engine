#include "vk_format.h"

#include "rhi_validation.h"

bool vk_format_info(Format format, vk::Format &out)
{
    switch (format)
    {
    case Format::R8Unorm: out = vk::Format::eR8Unorm; return true;
    case Format::RG8Unorm: out = vk::Format::eR8G8Unorm; return true;
    case Format::RGBA8Unorm: out = vk::Format::eR8G8B8A8Unorm; return true;
    case Format::RGBA8UnormSrgb: out = vk::Format::eR8G8B8A8Srgb; return true;
    case Format::BGRA8Unorm: out = vk::Format::eB8G8R8A8Unorm; return true;
    case Format::BGRA8UnormSrgb: out = vk::Format::eB8G8R8A8Srgb; return true;
    case Format::R16Float: out = vk::Format::eR16Sfloat; return true;
    case Format::RG16Float: out = vk::Format::eR16G16Sfloat; return true;
    case Format::RGBA16Float: out = vk::Format::eR16G16B16A16Sfloat; return true;
    case Format::Depth32Float: out = vk::Format::eD32Sfloat; return true;
    case Format::Undefined:
    default:
        return false;
    }
}

bool vk_format_from_vulkan(vk::Format format, Format &out)
{
    switch (format)
    {
    case vk::Format::eR8Unorm: out = Format::R8Unorm; return true;
    case vk::Format::eR8G8Unorm: out = Format::RG8Unorm; return true;
    case vk::Format::eR8G8B8A8Unorm: out = Format::RGBA8Unorm; return true;
    case vk::Format::eR8G8B8A8Srgb: out = Format::RGBA8UnormSrgb; return true;
    case vk::Format::eB8G8R8A8Unorm: out = Format::BGRA8Unorm; return true;
    case vk::Format::eB8G8R8A8Srgb: out = Format::BGRA8UnormSrgb; return true;
    case vk::Format::eR16Sfloat: out = Format::R16Float; return true;
    case vk::Format::eR16G16B16A16Sfloat: out = Format::RGBA16Float; return true;
    case vk::Format::eD32Sfloat: out = Format::Depth32Float; return true;
    default:
        return false;
    }
}

vk::ImageUsageFlags vk_image_usage_from_flags(uint8_t usage, Format format)
{
    vk::ImageUsageFlags flags;
    if ((usage & TextureUsage::TEXTURE_BINDING) != 0)
        flags |= vk::ImageUsageFlagBits::eSampled;
    if ((usage & TextureUsage::RENDER_ATTACHMENT) != 0)
    {
        flags |= rhi_format_is_depth(format) ? vk::ImageUsageFlagBits::eDepthStencilAttachment
                                             : vk::ImageUsageFlagBits::eColorAttachment;
    }
    if ((usage & TextureUsage::COPY_DST) != 0)
        flags |= vk::ImageUsageFlagBits::eTransferDst;
    if ((usage & TextureUsage::COPY_SRC) != 0)
        flags |= vk::ImageUsageFlagBits::eTransferSrc;
    if ((usage & TextureUsage::STORAGE) != 0)
        flags |= vk::ImageUsageFlagBits::eStorage;
    return flags;
}

vk::BufferUsageFlags vk_buffer_usage_from_flags(uint8_t usage)
{
    vk::BufferUsageFlags flags;
    if ((usage & GPUBufferUsage::VERTEX) != 0)
        flags |= vk::BufferUsageFlagBits::eVertexBuffer;
    if ((usage & GPUBufferUsage::INDEX) != 0)
        flags |= vk::BufferUsageFlagBits::eIndexBuffer;
    if ((usage & GPUBufferUsage::UNIFORM) != 0)
        flags |= vk::BufferUsageFlagBits::eUniformBuffer;
    if ((usage & GPUBufferUsage::COPY_DST) != 0)
        flags |= vk::BufferUsageFlagBits::eTransferDst;
    if ((usage & GPUBufferUsage::COPY_SRC) != 0)
        flags |= vk::BufferUsageFlagBits::eTransferSrc;
    // STORAGE 在接口里标注为预留，共享校验当前会拒绝；这里先保留映射，
    // compute / 存储缓冲真正实现时不需要再改这张表。
    if ((usage & GPUBufferUsage::STORAGE) != 0)
        flags |= vk::BufferUsageFlagBits::eStorageBuffer;
    return flags;
}

vk::CompareOp vk_compare_op_from_function(CompareFunction function)
{
    switch (function)
    {
    case CompareFunction::Never: return vk::CompareOp::eNever;
    case CompareFunction::Less: return vk::CompareOp::eLess;
    case CompareFunction::Equal: return vk::CompareOp::eEqual;
    case CompareFunction::LessEqual: return vk::CompareOp::eLessOrEqual;
    case CompareFunction::Greater: return vk::CompareOp::eGreater;
    case CompareFunction::NotEqual: return vk::CompareOp::eNotEqual;
    case CompareFunction::GreaterEqual: return vk::CompareOp::eGreaterOrEqual;
    case CompareFunction::Always: return vk::CompareOp::eAlways;
    default: return vk::CompareOp::eLessOrEqual;
    }
}

bool vk_vertex_format_info(VertexFormat format, vk::Format &out)
{
    switch (format)
    {
    case VertexFormat::Float32: out = vk::Format::eR32Sfloat; return true;
    case VertexFormat::Float32x2: out = vk::Format::eR32G32Sfloat; return true;
    case VertexFormat::Float32x3: out = vk::Format::eR32G32B32Sfloat; return true;
    case VertexFormat::Float32x4: out = vk::Format::eR32G32B32A32Sfloat; return true;
    case VertexFormat::Uint32: out = vk::Format::eR32Uint; return true;
    case VertexFormat::Uint8x4Normalized: out = vk::Format::eR8G8B8A8Unorm; return true;
    default: return false;
    }
}

vk::BlendFactor vk_blend_factor_from(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero: return vk::BlendFactor::eZero;
    case BlendFactor::One: return vk::BlendFactor::eOne;
    case BlendFactor::SrcColor: return vk::BlendFactor::eSrcColor;
    case BlendFactor::OneMinusSrcColor: return vk::BlendFactor::eOneMinusSrcColor;
    case BlendFactor::SrcAlpha: return vk::BlendFactor::eSrcAlpha;
    case BlendFactor::OneMinusSrcAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
    case BlendFactor::DstColor: return vk::BlendFactor::eDstColor;
    case BlendFactor::OneMinusDstColor: return vk::BlendFactor::eOneMinusDstColor;
    case BlendFactor::DstAlpha: return vk::BlendFactor::eDstAlpha;
    case BlendFactor::OneMinusDstAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
    default: return vk::BlendFactor::eOne;
    }
}

vk::BlendOp vk_blend_op_from(BlendOperation operation)
{
    switch (operation)
    {
    case BlendOperation::Add: return vk::BlendOp::eAdd;
    case BlendOperation::Subtract: return vk::BlendOp::eSubtract;
    case BlendOperation::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
    case BlendOperation::Min: return vk::BlendOp::eMin;
    case BlendOperation::Max: return vk::BlendOp::eMax;
    default: return vk::BlendOp::eAdd;
    }
}

vk::ColorComponentFlags vk_color_write_mask_from(uint8_t mask)
{
    vk::ColorComponentFlags flags;
    if ((mask & ColorWriteMask::R) != 0)
        flags |= vk::ColorComponentFlagBits::eR;
    if ((mask & ColorWriteMask::G) != 0)
        flags |= vk::ColorComponentFlagBits::eG;
    if ((mask & ColorWriteMask::B) != 0)
        flags |= vk::ColorComponentFlagBits::eB;
    if ((mask & ColorWriteMask::A) != 0)
        flags |= vk::ColorComponentFlagBits::eA;
    return flags;
}

vk::SampleCountFlagBits vk_sample_count_from_bits(uint32_t samples)
{
    switch (samples)
    {
    case 1: return vk::SampleCountFlagBits::e1;
    case 2: return vk::SampleCountFlagBits::e2;
    case 4: return vk::SampleCountFlagBits::e4;
    case 8: return vk::SampleCountFlagBits::e8;
    case 16: return vk::SampleCountFlagBits::e16;
    case 32: return vk::SampleCountFlagBits::e32;
    case 64: return vk::SampleCountFlagBits::e64;
    default: return vk::SampleCountFlagBits::e1;
    }
}
