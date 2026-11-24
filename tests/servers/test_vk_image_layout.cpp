#include "doctest/doctest.h"
#include "vulkan/vk_image_layout.h"

// vk_image_layout.* 是纯映射逻辑（不创建 image、不提交命令），因此可以脱离
// 窗口/GPU 做单元测试。Vulkan 后端的 queue_write_texture / generate_mip_map
// 依赖这里的布局/同步映射，修改映射表时同步维护本用例。
TEST_CASE("Vulkan image layout: aspect")
{
    CHECK(vk_image_aspect(Format::RGBA8Unorm) == vk::ImageAspectFlagBits::eColor);
    CHECK(vk_image_aspect(Format::RGBA8UnormSrgb) == vk::ImageAspectFlagBits::eColor);
    CHECK(vk_image_aspect(Format::Depth32Float) == vk::ImageAspectFlagBits::eDepth);
}

TEST_CASE("Vulkan image layout: resting layout")
{
    // 可采样纹理：回到 shader read；深度纹理用 depth read-only，不能是颜色布局。
    CHECK(vk_image_resting_layout(Format::RGBA8Unorm, TextureUsage::TEXTURE_BINDING) ==
          vk::ImageLayout::eShaderReadOnlyOptimal);
    CHECK(vk_image_resting_layout(Format::RGBA8Unorm,
                                  TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT) ==
          vk::ImageLayout::eShaderReadOnlyOptimal);
    CHECK(vk_image_resting_layout(Format::RGBA8Unorm, TextureUsage::COPY_SRC) ==
          vk::ImageLayout::eTransferSrcOptimal);
    CHECK(vk_image_resting_layout(Format::RGBA8Unorm, TextureUsage::RENDER_ATTACHMENT) ==
          vk::ImageLayout::eColorAttachmentOptimal);
    CHECK(vk_image_resting_layout(Format::RGBA8Unorm, TextureUsage::COPY_DST) == vk::ImageLayout::eGeneral);

    CHECK(vk_image_resting_layout(Format::Depth32Float, TextureUsage::TEXTURE_BINDING) ==
          vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    CHECK(vk_image_resting_layout(Format::Depth32Float, TextureUsage::RENDER_ATTACHMENT) ==
          vk::ImageLayout::eDepthStencilAttachmentOptimal);
}

TEST_CASE("Vulkan image layout: transition masks")
{
    // eUndefined 起点：src 在 top-of-pipe、无 access；上传目标需要 TRANSFER 写。
    const VkImageLayoutTransition to_transfer_dst =
        vk_image_layout_transition(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    CHECK(to_transfer_dst.src_stage == vk::PipelineStageFlagBits::eTopOfPipe);
    CHECK(to_transfer_dst.src_access == vk::AccessFlags{});
    CHECK(to_transfer_dst.dst_stage == vk::PipelineStageFlagBits::eTransfer);
    CHECK(to_transfer_dst.dst_access == vk::AccessFlagBits::eTransferWrite);

    // 上传结束：TRANSFER_DST → SHADER_READ，采样侧必须是 shader read。
    const VkImageLayoutTransition to_shader_read =
        vk_image_layout_transition(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
    CHECK(to_shader_read.src_stage == vk::PipelineStageFlagBits::eTransfer);
    CHECK(to_shader_read.src_access == vk::AccessFlagBits::eTransferWrite);
    CHECK(to_shader_read.dst_stage == vk::PipelineStageFlagBits::eAllGraphics);
    CHECK(to_shader_read.dst_access == vk::AccessFlagBits::eShaderRead);

    // 采样 → 再次上传：src 侧必须等 shader 读完。
    const VkImageLayoutTransition shader_read_to_transfer =
        vk_image_layout_transition(vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal);
    CHECK(shader_read_to_transfer.src_stage == vk::PipelineStageFlagBits::eAllGraphics);
    CHECK(shader_read_to_transfer.src_access == vk::AccessFlagBits::eShaderRead);

    // 深度附件布局的 src/dst 是 early/late fragment tests + depth/stencil 读写。
    const VkImageLayoutTransition depth_attachment = vk_image_layout_transition(
        vk::ImageLayout::eDepthStencilReadOnlyOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    CHECK(depth_attachment.src_access == vk::AccessFlagBits::eShaderRead);
    CHECK(depth_attachment.dst_stage ==
          (vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests));
}

TEST_CASE("Vulkan image layout: mip extent")
{
    CHECK(vk_mip_extent(64, 64, 0) == vk::Extent2D(64, 64));
    CHECK(vk_mip_extent(64, 64, 3) == vk::Extent2D(8, 8));
    CHECK(vk_mip_extent(80, 60, 1) == vk::Extent2D(40, 30));
    // 不足 1 的维度被钳到 1（非 2 的幂尺寸下 mip 链仍是合法的）
    CHECK(vk_mip_extent(80, 60, 4) == vk::Extent2D(5, 3));
    CHECK(vk_mip_extent(1, 1, 0) == vk::Extent2D(1, 1));
    CHECK(vk_mip_extent(1, 1, 10) == vk::Extent2D(1, 1));
    CHECK(vk_mip_extent(0, 0, 0) == vk::Extent2D(1, 1));
}
