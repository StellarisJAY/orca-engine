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
