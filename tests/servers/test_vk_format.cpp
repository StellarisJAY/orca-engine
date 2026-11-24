#include "doctest/doctest.h"
#include "vulkan/vk_format.h"

#include <string>

TEST_CASE("VK format: Format to vk::Format and back")
{
    const Format formats[] = {
        Format::R8Unorm,
        Format::RG8Unorm,
        Format::RGBA8Unorm,
        Format::RGBA8UnormSrgb,
        Format::BGRA8Unorm,
        Format::BGRA8UnormSrgb,
        Format::R16Float,
        Format::RGBA16Float,
        Format::Depth32Float,
    };

    for (Format format : formats)
    {
        vk::Format vk_format = vk::Format::eUndefined;
        REQUIRE(vk_format_info(format, vk_format));

        Format round_trip = Format::Undefined;
        REQUIRE(vk_format_from_vulkan(vk_format, round_trip));
        CHECK(round_trip == format);
    }

    vk::Format vk_format = vk::Format::eR8G8B8A8Srgb;
    CHECK_FALSE(vk_format_info(Format::Undefined, vk_format));

    Format format = Format::Undefined;
    CHECK_FALSE(vk_format_from_vulkan(vk::Format::eUndefined, format));
    CHECK_FALSE(vk_format_from_vulkan(vk::Format::eR32Sfloat, format));
}

TEST_CASE("VK format: texture usage flags")
{
    const uint8_t all_usage = TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT |
                              TextureUsage::COPY_DST | TextureUsage::COPY_SRC;

    const vk::ImageUsageFlags color_usage = vk_image_usage_from_flags(all_usage, Format::RGBA8Unorm);
    CHECK(bool(color_usage & vk::ImageUsageFlagBits::eSampled));
    CHECK(bool(color_usage & vk::ImageUsageFlagBits::eColorAttachment));
    CHECK(bool(color_usage & vk::ImageUsageFlagBits::eTransferDst));
    CHECK(bool(color_usage & vk::ImageUsageFlagBits::eTransferSrc));
    CHECK_FALSE(bool(color_usage & vk::ImageUsageFlagBits::eDepthStencilAttachment));

    const vk::ImageUsageFlags depth_usage = vk_image_usage_from_flags(all_usage, Format::Depth32Float);
    CHECK(bool(depth_usage & vk::ImageUsageFlagBits::eSampled));
    CHECK(bool(depth_usage & vk::ImageUsageFlagBits::eDepthStencilAttachment));
    CHECK_FALSE(bool(depth_usage & vk::ImageUsageFlagBits::eColorAttachment));

    // 只有 COPY_DST 位时不应把其它用途位一起打开（旧的运算符优先级 bug）。
    const vk::ImageUsageFlags copy_dst_only =
        vk_image_usage_from_flags(TextureUsage::COPY_DST, Format::RGBA8Unorm);
    CHECK(bool(copy_dst_only & vk::ImageUsageFlagBits::eTransferDst));
    CHECK_FALSE(bool(copy_dst_only & vk::ImageUsageFlagBits::eSampled));
    CHECK_FALSE(bool(copy_dst_only & vk::ImageUsageFlagBits::eColorAttachment));
    CHECK_FALSE(bool(copy_dst_only & vk::ImageUsageFlagBits::eTransferSrc));

    const vk::ImageUsageFlags storage =
        vk_image_usage_from_flags(TextureUsage::STORAGE, Format::RGBA8Unorm);
    CHECK(bool(storage & vk::ImageUsageFlagBits::eStorage));
}

TEST_CASE("VK format: buffer usage flags")
{
    // 引擎实际用到的三种形态：顶点/索引/uniform 各自叠加 COPY_DST 以便上传。
    const vk::BufferUsageFlags vertex =
        vk_buffer_usage_from_flags(GPUBufferUsage::VERTEX | GPUBufferUsage::COPY_DST);
    CHECK(bool(vertex & vk::BufferUsageFlagBits::eVertexBuffer));
    CHECK(bool(vertex & vk::BufferUsageFlagBits::eTransferDst));
    CHECK_FALSE(bool(vertex & vk::BufferUsageFlagBits::eIndexBuffer));
    CHECK_FALSE(bool(vertex & vk::BufferUsageFlagBits::eUniformBuffer));

    const vk::BufferUsageFlags index = vk_buffer_usage_from_flags(GPUBufferUsage::INDEX);
    CHECK(bool(index & vk::BufferUsageFlagBits::eIndexBuffer));
    CHECK_FALSE(bool(index & vk::BufferUsageFlagBits::eTransferDst));

    const vk::BufferUsageFlags uniform = vk_buffer_usage_from_flags(GPUBufferUsage::UNIFORM);
    CHECK(bool(uniform & vk::BufferUsageFlagBits::eUniformBuffer));

    const vk::BufferUsageFlags copy_src = vk_buffer_usage_from_flags(GPUBufferUsage::COPY_SRC);
    CHECK(bool(copy_src & vk::BufferUsageFlagBits::eTransferSrc));
    CHECK_FALSE(bool(copy_src & vk::BufferUsageFlagBits::eTransferDst));

    // STORAGE 当前在共享校验里会被拒绝，但映射表先保留完整，供 compute 落地时使用。
    const vk::BufferUsageFlags storage_buffer = vk_buffer_usage_from_flags(GPUBufferUsage::STORAGE);
    CHECK(bool(storage_buffer & vk::BufferUsageFlagBits::eStorageBuffer));

    const vk::BufferUsageFlags empty = vk_buffer_usage_from_flags(0);
    CHECK_FALSE(bool(empty & vk::BufferUsageFlagBits::eVertexBuffer));
    CHECK_FALSE(bool(empty & vk::BufferUsageFlagBits::eTransferDst));
}

TEST_CASE("VK format: sampler compare op mapping")
{
    // 阴影 PCF 的 compare sampler 用这条映射；必须逐项对上 VkCompareOp。
    CHECK(vk_compare_op_from_function(CompareFunction::Never) == vk::CompareOp::eNever);
    CHECK(vk_compare_op_from_function(CompareFunction::Less) == vk::CompareOp::eLess);
    CHECK(vk_compare_op_from_function(CompareFunction::Equal) == vk::CompareOp::eEqual);
    CHECK(vk_compare_op_from_function(CompareFunction::LessEqual) == vk::CompareOp::eLessOrEqual);
    CHECK(vk_compare_op_from_function(CompareFunction::Greater) == vk::CompareOp::eGreater);
    CHECK(vk_compare_op_from_function(CompareFunction::NotEqual) == vk::CompareOp::eNotEqual);
    CHECK(vk_compare_op_from_function(CompareFunction::GreaterEqual) == vk::CompareOp::eGreaterOrEqual);
    CHECK(vk_compare_op_from_function(CompareFunction::Always) == vk::CompareOp::eAlways);

    // 默认 SamplerDesc 与 WebGPU 一致：compare 关闭时仍带 LessEqual
    CHECK(SamplerDesc{}.compare_op == CompareFunction::LessEqual);
}

TEST_CASE("VK format: vertex input mapping")
{
    // 管线 vertex input attribute 的格式映射；每一项都必须是合法的 VkFormat。
    vk::Format format = vk::Format::eUndefined;
    REQUIRE(vk_vertex_format_info(VertexFormat::Float32, format));
    CHECK(format == vk::Format::eR32Sfloat);
    REQUIRE(vk_vertex_format_info(VertexFormat::Float32x2, format));
    CHECK(format == vk::Format::eR32G32Sfloat);
    REQUIRE(vk_vertex_format_info(VertexFormat::Float32x3, format));
    CHECK(format == vk::Format::eR32G32B32Sfloat);
    REQUIRE(vk_vertex_format_info(VertexFormat::Float32x4, format));
    CHECK(format == vk::Format::eR32G32B32A32Sfloat);
    REQUIRE(vk_vertex_format_info(VertexFormat::Uint32, format));
    CHECK(format == vk::Format::eR32Uint);
    REQUIRE(vk_vertex_format_info(VertexFormat::Uint8x4Normalized, format));
    CHECK(format == vk::Format::eR8G8B8A8Unorm);

    // 非法枚举值不能被静默映射成某个格式。
    CHECK_FALSE(vk_vertex_format_info(static_cast<VertexFormat>(999), format));
}

TEST_CASE("VK format: blend state mapping")
{
    CHECK(vk_blend_factor_from(BlendFactor::Zero) == vk::BlendFactor::eZero);
    CHECK(vk_blend_factor_from(BlendFactor::One) == vk::BlendFactor::eOne);
    CHECK(vk_blend_factor_from(BlendFactor::SrcColor) == vk::BlendFactor::eSrcColor);
    CHECK(vk_blend_factor_from(BlendFactor::OneMinusSrcColor) == vk::BlendFactor::eOneMinusSrcColor);
    CHECK(vk_blend_factor_from(BlendFactor::SrcAlpha) == vk::BlendFactor::eSrcAlpha);
    CHECK(vk_blend_factor_from(BlendFactor::OneMinusSrcAlpha) == vk::BlendFactor::eOneMinusSrcAlpha);
    CHECK(vk_blend_factor_from(BlendFactor::DstColor) == vk::BlendFactor::eDstColor);
    CHECK(vk_blend_factor_from(BlendFactor::OneMinusDstColor) == vk::BlendFactor::eOneMinusDstColor);
    CHECK(vk_blend_factor_from(BlendFactor::DstAlpha) == vk::BlendFactor::eDstAlpha);
    CHECK(vk_blend_factor_from(BlendFactor::OneMinusDstAlpha) == vk::BlendFactor::eOneMinusDstAlpha);

    CHECK(vk_blend_op_from(BlendOperation::Add) == vk::BlendOp::eAdd);
    CHECK(vk_blend_op_from(BlendOperation::Subtract) == vk::BlendOp::eSubtract);
    CHECK(vk_blend_op_from(BlendOperation::ReverseSubtract) == vk::BlendOp::eReverseSubtract);
    CHECK(vk_blend_op_from(BlendOperation::Min) == vk::BlendOp::eMin);
    CHECK(vk_blend_op_from(BlendOperation::Max) == vk::BlendOp::eMax);
}

TEST_CASE("VK format: color write mask mapping")
{
    const vk::ColorComponentFlags all = vk_color_write_mask_from(ColorWriteMask::ALL);
    CHECK(bool(all & vk::ColorComponentFlagBits::eR));
    CHECK(bool(all & vk::ColorComponentFlagBits::eG));
    CHECK(bool(all & vk::ColorComponentFlagBits::eB));
    CHECK(bool(all & vk::ColorComponentFlagBits::eA));

    // 只写 RGB（alpha 保持目标值）是常见的透明混合配置。
    const vk::ColorComponentFlags rgb =
        vk_color_write_mask_from(ColorWriteMask::R | ColorWriteMask::G | ColorWriteMask::B);
    CHECK(bool(rgb & vk::ColorComponentFlagBits::eR));
    CHECK(bool(rgb & vk::ColorComponentFlagBits::eG));
    CHECK(bool(rgb & vk::ColorComponentFlagBits::eB));
    CHECK_FALSE(bool(rgb & vk::ColorComponentFlagBits::eA));

    CHECK_FALSE(bool(vk_color_write_mask_from(0)));
}

TEST_CASE("VK format: sample count mapping")
{
    CHECK(vk_sample_count_from_bits(1) == vk::SampleCountFlagBits::e1);
    CHECK(vk_sample_count_from_bits(2) == vk::SampleCountFlagBits::e2);
    CHECK(vk_sample_count_from_bits(4) == vk::SampleCountFlagBits::e4);
    CHECK(vk_sample_count_from_bits(8) == vk::SampleCountFlagBits::e8);
    CHECK(vk_sample_count_from_bits(16) == vk::SampleCountFlagBits::e16);
    CHECK(vk_sample_count_from_bits(32) == vk::SampleCountFlagBits::e32);
    CHECK(vk_sample_count_from_bits(64) == vk::SampleCountFlagBits::e64);

    // 非法数量回退到 1（共享校验已在入口拒绝非 1 值）。
    CHECK(vk_sample_count_from_bits(3) == vk::SampleCountFlagBits::e1);
    CHECK(vk_sample_count_from_bits(0) == vk::SampleCountFlagBits::e1);
}
