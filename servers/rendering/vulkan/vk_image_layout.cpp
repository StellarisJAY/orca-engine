#include "vk_image_layout.h"

#include "rhi_validation.h"

namespace
{
    /// @brief 一个布局对应的 src/dst stage 与 access mask。
    ///
    /// 同一个布局同时被用作 barrier 的 oldLayout 与 newLayout，因此这里给出的是
    /// "以该布局为起点时的读/写范围"与"以该布局为终点时的读/写范围"：
    ///   - src：让上一阶段的写入对后续阶段可见；
    ///   - dst：让后续阶段看到/写入该布局的内容。
    /// 保守选择较宽的 stage（如 eAllGraphics），避免漏掉 vertex shader 采样。
    void layout_stage_access(vk::ImageLayout layout, vk::PipelineStageFlags &stage, vk::AccessFlags &access)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined:
            stage = vk::PipelineStageFlagBits::eTopOfPipe;
            access = {};
            break;
        case vk::ImageLayout::eTransferSrcOptimal:
            stage = vk::PipelineStageFlagBits::eTransfer;
            access = vk::AccessFlagBits::eTransferRead;
            break;
        case vk::ImageLayout::eTransferDstOptimal:
            stage = vk::PipelineStageFlagBits::eTransfer;
            access = vk::AccessFlagBits::eTransferWrite;
            break;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            stage = vk::PipelineStageFlagBits::eAllGraphics;
            access = vk::AccessFlagBits::eShaderRead;
            break;
        case vk::ImageLayout::eColorAttachmentOptimal:
            stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            access = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
            break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            stage = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
            access = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            break;
        case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
            stage = vk::PipelineStageFlagBits::eAllGraphics;
            access = vk::AccessFlagBits::eShaderRead;
            break;
        case vk::ImageLayout::eGeneral:
        default:
            stage = vk::PipelineStageFlagBits::eAllCommands;
            access = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
            break;
        }
    }
} // namespace

vk::ImageAspectFlags vk_image_aspect(Format format)
{
    return rhi_format_is_depth(format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
}

vk::ImageLayout vk_image_resting_layout(Format format, uint8_t usage)
{
    if (rhi_format_is_depth(format))
    {
        if ((usage & TextureUsage::TEXTURE_BINDING) != 0)
            return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        return vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }

    if ((usage & TextureUsage::TEXTURE_BINDING) != 0)
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    if ((usage & TextureUsage::COPY_SRC) != 0)
        return vk::ImageLayout::eTransferSrcOptimal;
    if ((usage & TextureUsage::RENDER_ATTACHMENT) != 0)
        return vk::ImageLayout::eColorAttachmentOptimal;
    return vk::ImageLayout::eGeneral;
}

VkImageLayoutTransition vk_image_layout_transition(vk::ImageLayout old_layout, vk::ImageLayout new_layout)
{
    VkImageLayoutTransition transition;
    layout_stage_access(old_layout, transition.src_stage, transition.src_access);
    layout_stage_access(new_layout, transition.dst_stage, transition.dst_access);
    return transition;
}

vk::Extent2D vk_mip_extent(uint32_t width, uint32_t height, uint32_t mip_level)
{
    const uint32_t shift = mip_level < 31 ? mip_level : 31;
    const uint32_t mip_width = width >> shift;
    const uint32_t mip_height = height >> shift;
    return vk::Extent2D{mip_width == 0 ? 1u : mip_width, mip_height == 0 ? 1u : mip_height};
}

void vk_cmd_image_layout_barrier(vk::CommandBuffer command_buffer, vk::Image image,
                                 vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                 vk::ImageAspectFlags aspect, uint32_t base_mip, uint32_t mip_count,
                                 uint32_t base_layer, uint32_t layer_count)
{
    if (old_layout == new_layout)
        return;

    const VkImageLayoutTransition transition = vk_image_layout_transition(old_layout, new_layout);

    vk::ImageMemoryBarrier barrier;
    barrier.setOldLayout(old_layout);
    barrier.setNewLayout(new_layout);
    barrier.setImage(image);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setSubresourceRange(
        vk::ImageSubresourceRange{aspect, base_mip, mip_count, base_layer, layer_count});
    barrier.setSrcAccessMask(transition.src_access);
    barrier.setDstAccessMask(transition.dst_access);

    command_buffer.pipelineBarrier(transition.src_stage, transition.dst_stage, vk::DependencyFlags{}, {}, {},
                                   barrier);
}
