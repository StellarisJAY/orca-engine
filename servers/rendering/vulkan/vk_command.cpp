#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "os/memory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace
{
    vk::AttachmentLoadOp get_vk_load_op(LoadOp orca_load_op)
    {
        switch (orca_load_op)
        {
        case LoadOp::Load:
            return vk::AttachmentLoadOp::eLoad;
        case LoadOp::Clear:
            return vk::AttachmentLoadOp::eClear;
        case LoadOp::DontCare:
            return vk::AttachmentLoadOp::eDontCare;
        default:
            return vk::AttachmentLoadOp::eLoad;
        }
    }

    vk::AttachmentStoreOp get_vk_store_op(StoreOp orca_store_op)
    {
        switch (orca_store_op)
        {
        case StoreOp::Discard:
            return vk::AttachmentStoreOp::eDontCare;
        case StoreOp::Store:
            return vk::AttachmentStoreOp::eStore;
        default:
            return vk::AttachmentStoreOp::eStore;
        }
    }

    void print_vk_error(const char *what, const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan %s failed: %s\n", what, error.what());
    }

    /// @brief 从显式附件视图解析出的 render pass 创建信息。
    struct ResolvedPassAttachment
    {
        VkTextureHandleData *texture = nullptr;
        VkTextureViewHandleData *view = nullptr;
        vk::Format format = vk::Format::eUndefined;
        vk::Extent2D extent{0, 0};
        /// @brief 父纹理当前的实际布局（渲染通道开始时由驱动 transition 到附件布局）
        vk::ImageLayout initial_layout = vk::ImageLayout::eUndefined;
        /// @brief 本后端固定的附件结束布局
        vk::ImageLayout final_layout = vk::ImageLayout::eUndefined;
    };

    /// @brief 读取显式附件视图的格式/尺寸/布局并校验视图形态。
    ///
    /// Vulkan 的 layout 是显式状态，`initialLayout` 必须取父纹理记录的实际布局
    /// （由上传/上一个 pass 维护），不能假设为 Undefined；`finalLayout` 固定为附件布局，
    /// 由 `pass_end` 写回父纹理。视图必须是单层 2D 视图（framebuffer 的 layerCount 为 1）。
    bool resolve_pass_attachment(const TextureViewHandle &view_handle, bool is_depth,
                                 ResolvedPassAttachment &resolved, std::string &error)
    {
        if (!view_handle.is_valid() || view_handle.data == nullptr)
        {
            error = "附件视图句柄无效";
            return false;
        }

        const auto view_data = static_cast<VkTextureViewHandleData *>(view_handle.data);
        if (view_data->dim != TextureViewDimension::Texture2D || view_data->layer_count != 1)
        {
            error = "附件视图必须是单层 2D 视图";
            return false;
        }
        if (is_depth)
        {
            if (view_data->aspect != TextureAspect::DepthOnly)
            {
                error = "深度附件视图必须使用 DepthOnly aspect";
                return false;
            }
        }
        else if (view_data->aspect != TextureAspect::Color)
        {
            error = "颜色附件视图必须使用 Color aspect";
            return false;
        }
        if (view_data->texture == nullptr)
        {
            error = "附件视图缺少父纹理记录";
            return false;
        }

        vk::Format format = vk::Format::eUndefined;
        if (!vk_format_info(view_data->format, format))
        {
            error = "附件视图格式无法映射到 Vulkan 格式";
            return false;
        }
        if (view_data->width == 0 || view_data->height == 0)
        {
            error = "附件视图尺寸为 0";
            return false;
        }

        resolved.texture = view_data->texture;
        resolved.view = view_data;
        resolved.format = format;
        resolved.extent = vk::Extent2D{view_data->width, view_data->height};
        resolved.initial_layout = view_data->texture->current_layout;
        resolved.final_layout = is_depth ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                                         : vk::ImageLayout::eColorAttachmentOptimal;
        return true;
    }

    /// @brief 计算 render area：`desc.width/height == 0` 时取附件（framebuffer）尺寸。
    ///
    /// 接口里的 width/height 是"渲染区域"，不是 framebuffer 尺寸：framebuffer 必须与
    /// 附件尺寸一致，render area 可以更小，但不能超出 framebuffer。
    bool resolve_render_area(const char *label, const RenderPassDesc &desc, vk::Extent2D framebuffer_extent,
                             vk::Rect2D &out_area)
    {
        const uint32_t width = desc.width == 0 ? framebuffer_extent.width : desc.width;
        const uint32_t height = desc.height == 0 ? framebuffer_extent.height : desc.height;
        if (width == 0 || height == 0 || width > framebuffer_extent.width || height > framebuffer_extent.height)
        {
            std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): render area %ux%u 超出附件尺寸 %ux%u\n",
                        label, width, height, framebuffer_extent.width, framebuffer_extent.height);
            return false;
        }

        out_area.setOffset(vk::Offset2D(0, 0));
        out_area.setExtent(vk::Extent2D(width, height));
        return true;
    }

    // -------------------------------------------------------------------------
    // V6-2：pass 的 viewport / scissor 与参数校验
    // -------------------------------------------------------------------------

    /// @brief 把 viewport 的外接整数矩形裁剪到 render area，得到 scissor 矩形（G-04）。
    ///
    /// 接口只有 `pass_set_viewport`，没有独立的 scissor 命令；而管线把 scissor 设为
    /// dynamic，所以本后端在设置 viewport 时必须同时录制 `vkCmdSetScissor`。
    /// Vulkan 要求 scissor 完全落在 render area 内（偏移非负、右下角不越界），因此
    /// 这里对 viewport 取 floor/ceil 后与 render area 求交；完全在区域外时返回零尺寸
    /// 矩形（绘制被裁剪为空）。viewport 本身可以超出 render area（Vulkan 允许），
    /// 不在这里报错。
    vk::Rect2D make_scissor_from_viewport(const vk::Viewport &viewport, const vk::Rect2D &render_area)
    {
        const int64_t area_left = static_cast<int64_t>(render_area.offset.x);
        const int64_t area_top = static_cast<int64_t>(render_area.offset.y);
        const int64_t area_right = area_left + static_cast<int64_t>(render_area.extent.width);
        const int64_t area_bottom = area_top + static_cast<int64_t>(render_area.extent.height);
        const int64_t left = static_cast<int64_t>(std::floor(static_cast<double>(viewport.x)));
        const int64_t top = static_cast<int64_t>(std::floor(static_cast<double>(viewport.y)));
        const int64_t right = static_cast<int64_t>(
            std::ceil(static_cast<double>(viewport.x) + static_cast<double>(viewport.width)));
        const int64_t bottom = static_cast<int64_t>(
            std::ceil(static_cast<double>(viewport.y) + static_cast<double>(viewport.height)));

        const int64_t clipped_left = std::max<int64_t>(left, area_left);
        const int64_t clipped_top = std::max<int64_t>(top, area_top);
        const int64_t clipped_right = std::min<int64_t>(right, area_right);
        const int64_t clipped_bottom = std::min<int64_t>(bottom, area_bottom);
        if (clipped_left >= clipped_right || clipped_top >= clipped_bottom)
        {
            // 零尺寸矩形也要落在 render area 内；取 render area 的左上角 + 0 尺寸。
            return vk::Rect2D{vk::Offset2D{render_area.offset.x, render_area.offset.y},
                              vk::Extent2D{0, 0}};
        }
        return vk::Rect2D{
            vk::Offset2D{static_cast<int32_t>(clipped_left), static_cast<int32_t>(clipped_top)},
            vk::Extent2D{static_cast<uint32_t>(clipped_right - clipped_left),
                         static_cast<uint32_t>(clipped_bottom - clipped_top)}};
    }

    /// @brief 同时录制 viewport 与派生 scissor（接口没有独立 scissor 命令，G-04）。
    void record_viewport_and_scissor(const vk::raii::CommandBuffer &command_buffer,
                                     const vk::Rect2D &render_area, const vk::Viewport &viewport)
    {
        command_buffer.setViewport(0, viewport);
        command_buffer.setScissor(0, make_scissor_from_viewport(viewport, render_area));
    }

    /// @brief render pass 开始时设置覆盖整个 render area 的默认 viewport/scissor。
    ///
    /// 这样即使调用方不调用 `pass_set_viewport`，draw 也不会读到未定义的动态状态；
    /// 需要局部渲染时再用 `pass_set_viewport` 覆盖。
    void record_default_viewport_and_scissor(const vk::raii::CommandBuffer &command_buffer,
                                             const vk::Rect2D &render_area)
    {
        const vk::Viewport viewport(static_cast<float>(render_area.offset.x),
                                    static_cast<float>(render_area.offset.y),
                                    static_cast<float>(render_area.extent.width),
                                    static_cast<float>(render_area.extent.height), 0.0f, 1.0f);
        record_viewport_and_scissor(command_buffer, render_area, viewport);
    }

    /// @brief 校验 `pass_set_viewport` 的入参（Vulkan 的坐标/尺寸/深度合法区间）。
    bool validate_viewport(const vk::PhysicalDeviceLimits &limits, float x, float y, float width,
                           float height, float min_depth, float max_depth)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
            !std::isfinite(min_depth) || !std::isfinite(max_depth))
        {
            std::printf("[ERROR] Vulkan pass_set_viewport: viewport 参数必须是有限值\n");
            return false;
        }
        if (width <= 0.0f || height <= 0.0f)
        {
            std::printf("[ERROR] Vulkan pass_set_viewport: viewport 尺寸必须大于 0（得到 %.3fx%.3f）\n",
                        static_cast<double>(width), static_cast<double>(height));
            return false;
        }
        if (min_depth < 0.0f || min_depth > 1.0f || max_depth < 0.0f || max_depth > 1.0f ||
            min_depth > max_depth)
        {
            std::printf("[ERROR] Vulkan pass_set_viewport: 深度范围非法（[%.3f, %.3f]，要求 0 <= min <= max <= 1）\n",
                        static_cast<double>(min_depth), static_cast<double>(max_depth));
            return false;
        }
        // Vulkan 的 viewport 宽高不能超过 maxViewportDimensions，原点必须落在
        // viewportBoundsRange（该范围通常包含负值，允许 viewport 部分移出屏幕）。
        if (width > static_cast<float>(limits.maxViewportDimensions[0]) ||
            height > static_cast<float>(limits.maxViewportDimensions[1]))
        {
            std::printf("[ERROR] Vulkan pass_set_viewport: viewport 尺寸 %.0fx%.0f 超过 "
                        "maxViewportDimensions %ux%u\n",
                        static_cast<double>(width), static_cast<double>(height),
                        limits.maxViewportDimensions[0], limits.maxViewportDimensions[1]);
            return false;
        }
        if (x < limits.viewportBoundsRange[0] || x > limits.viewportBoundsRange[1] ||
            y < limits.viewportBoundsRange[0] || y > limits.viewportBoundsRange[1])
        {
            std::printf("[ERROR] Vulkan pass_set_viewport: viewport 原点 (%.0f, %.0f) 超出 "
                        "viewportBoundsRange [%.0f, %.0f]\n",
                        static_cast<double>(x), static_cast<double>(y),
                        static_cast<double>(limits.viewportBoundsRange[0]),
                        static_cast<double>(limits.viewportBoundsRange[1]));
            return false;
        }
        return true;
    }

    /// @brief IndexFormat → Vulkan 索引类型 + 单个元素字节数；未知枚举返回 false。
    bool get_vk_index_info(IndexFormat format, vk::IndexType &index_type, uint64_t &element_size)
    {
        switch (format)
        {
        case IndexFormat::Uint16:
            index_type = vk::IndexType::eUint16;
            element_size = 2;
            return true;
        case IndexFormat::Uint32:
            index_type = vk::IndexType::eUint32;
            element_size = 4;
            return true;
        default:
            return false;
        }
    }

    /// @brief 校验 pass 句柄可用于录制，并返回它的私有数据。
    ///
    /// 统一的入口检查：句柄有效、encoder 未 finish（`encoder_finish` 之后不能再录制）、
    /// pass 未 `pass_end`。失败时打印 `[ERROR]` 并返回 nullptr，调用方直接返回即可。
    /// 这样所有 pass 级命令（绑定/draw）都不会在 vkCmdEndRenderPass 之后继续录制，
    /// 也不会在 encoder 失效后访问已释放的命令缓冲。
    VkRenderPassHandleData *resolve_recording_pass(const char *what, RenderPassHandle pass)
    {
        if (!pass.is_valid() || pass.data == nullptr)
        {
            std::printf("[ERROR] Vulkan %s: invalid render pass handle\n", what);
            return nullptr;
        }
        auto *render_pass_data = static_cast<VkRenderPassHandleData *>(pass.data);
        if (render_pass_data->command_encoder_data == nullptr)
        {
            std::printf("[ERROR] Vulkan %s: encoder 已 finish，不能再录制\n", what);
            return nullptr;
        }
        if (!render_pass_data->active)
        {
            std::printf("[ERROR] Vulkan %s: render pass 已经 pass_end，不能再录制\n", what);
            return nullptr;
        }
        return render_pass_data;
    }
} // namespace

/// @brief 创建指令编码器（命令池 + 已 begin 的主 command buffer）。
CommandEncoderHandle VkRenderingDevice::create_command_encoder()
{
    CommandEncoderHandle cmd_encoder;
    VkCommandEncoderHandleData *command_encoder_data = nullptr;

    try
    {
        const auto [graphics_family, _] = findQueueFamilies(physical_device);
        if (!graphics_family.has_value())
        {
            std::printf("[ERROR] Vulkan create_command_encoder: no graphics queue family\n");
            return cmd_encoder;
        }

        vk::CommandPoolCreateInfo create_info;
        create_info.setQueueFamilyIndex(graphics_family.value());
        create_info.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

        command_encoder_data = memnew(VkCommandEncoderHandleData);
        command_encoder_data->command_pool = device.createCommandPool(create_info);

        vk::CommandBufferAllocateInfo alloc_info;
        alloc_info.setCommandPool(*command_encoder_data->command_pool);
        alloc_info.setCommandBufferCount(1);
        alloc_info.setLevel(vk::CommandBufferLevel::ePrimary);
        auto command_buffers = device.allocateCommandBuffers(alloc_info);
        command_encoder_data->command_buffer = std::move(command_buffers[0]);

        // 命令录制前必须进入 recording 状态
        command_encoder_data->command_buffer.begin(vk::CommandBufferBeginInfo{});

        cmd_encoder.handle = alloc_handle_id();
        cmd_encoder.data = command_encoder_data;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("create_command_encoder", error);
        memdelete(command_encoder_data);
        return {};
    }
    return cmd_encoder;
}

RenderPassHandle VkRenderingDevice::encoder_begin_render_pass(CommandEncoderHandle encoder,
                                                              const RenderPassDesc &desc)
{
    RenderPassHandle render_pass;
    if (!encoder.is_valid() || encoder.data == nullptr)
    {
        std::printf("[ERROR] Vulkan encoder_begin_render_pass: invalid encoder handle\n");
        return render_pass;
    }
    auto command_encoder_data = static_cast<VkCommandEncoderHandleData *>(encoder.data);
    const char *label = desc.label.empty() ? "render-pass" : desc.label.c_str();

    // 接口约定：无效的 color view（或无效的 depth view）表示默认帧缓冲。默认帧缓冲的
    // render pass/framebuffer 归交换链所有（V3-1），且不能与显式附件混用。
    bool uses_default_framebuffer = false;
    for (const auto &color_attachment : desc.color_attachments)
    {
        if (!color_attachment.view.is_valid())
            uses_default_framebuffer = true;
    }
    if (!desc.depth_attachments.empty() && !desc.depth_attachments[0].view.is_valid())
        uses_default_framebuffer = true;

    VkRenderPassHandleData *render_pass_data = nullptr;
    try
    {
        if (uses_default_framebuffer)
        {
            const bool color_shape_ok =
                desc.color_attachments.size() == 1 && !desc.color_attachments[0].view.is_valid();
            const bool depth_shape_ok =
                desc.depth_attachments.size() <= 1 &&
                (desc.depth_attachments.empty() || !desc.depth_attachments[0].view.is_valid());
            if (!color_shape_ok || !depth_shape_ok)
            {
                std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 默认帧缓冲不能与显式附件混用"
                            "（color 必须恰好 1 个无效 view，depth 至多 1 个无效 view）\n", label);
                return render_pass;
            }
            if (swapchain == nullptr || swapchain_render_pass == nullptr || swapchain_framebuffers.empty())
            {
                std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 交换链默认帧缓冲尚未创建\n", label);
                return render_pass;
            }
            // 默认帧缓冲必须使用已 acquire 的交换链图像。接口没有独立的 acquire 入口
            // （G-08），由本后端在第一次进入默认帧缓冲时惰性 acquire（V3-2），同一帧内
            // 后续的默认帧缓冲 pass 复用同一张图像。
            if (!acquire_next_swapchain_image())
            {
                std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 无法 acquire 交换链图像\n", label);
                return render_pass;
            }
            // 默认 render pass 的 load/store 是固有能力：color Clear+Store、depth Clear+DontCare。
            // load/store 不能像 clear value 那样在 begin 时覆盖；要支持 Load 必须按组合缓存
            // render pass + framebuffer（V3-3）。默认深度也是 Clear，请求 Load 同样无法表达。
            if (desc.color_attachments[0].load_op == LoadOp::Load ||
                (!desc.depth_attachments.empty() && desc.depth_attachments[0].load_op == LoadOp::Load))
            {
                std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 默认帧缓冲固定 Clear，"
                            "Load 变体尚未缓存（V3-3）\n", label);
                return render_pass;
            }

            vk::Rect2D render_area;
            if (!resolve_render_area(label, desc, swapchain_image_extent, render_area))
                return render_pass;

            const auto &color_attachment = desc.color_attachments[0];
            const auto &color = color_attachment.clear_color;
            const float depth_clear = desc.depth_attachments.empty() ? 1.0f
                                                                     : desc.depth_attachments[0].clear_depth;
            // 默认 render pass 固定两个附件：0 = 交换链 color，1 = 内部深度。
            std::array<vk::ClearValue, 2> clear_values = {
                vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{color[0], color[1], color[2], color[3]}}},
                vk::ClearValue{vk::ClearDepthStencilValue{depth_clear, 0}},
            };

            render_pass_data = memnew(VkRenderPassHandleData);
            render_pass_data->command_encoder_data = command_encoder_data;
            render_pass_data->render_area = render_area;

            vk::RenderPassBeginInfo begin_info;
            begin_info.setRenderArea(render_area);
            begin_info.setRenderPass(*swapchain_render_pass);
            begin_info.setFramebuffer(*swapchain_framebuffers[current_swapchain_image]);
            begin_info.setClearValues(clear_values);
            command_encoder_data->command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);
            // 默认动态状态：覆盖整个 render area 的 viewport + scissor（G-04）。
            record_default_viewport_and_scissor(command_encoder_data->command_buffer, render_area);
            std::printf("[INFO] Vulkan began default framebuffer pass(%s): image %u, area %ux%u\n", label,
                        current_swapchain_image, render_area.extent.width, render_area.extent.height);
        }
        else
        {
            // ---------------- 显式附件路径 ----------------
            if (desc.color_attachments.empty() && desc.depth_attachments.empty())
            {
                std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 至少需要一个颜色或深度附件\n",
                            label);
                return render_pass;
            }
            if (desc.depth_attachments.size() > 1)
            {
                std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 目前只支持 1 个深度附件\n", label);
                return render_pass;
            }

            std::vector<ResolvedPassAttachment> resolved_colors;
            resolved_colors.reserve(desc.color_attachments.size());
            ResolvedPassAttachment resolved_depth;
            const bool has_depth = !desc.depth_attachments.empty();

            // framebuffer 尺寸由附件推导（所有附件必须同尺寸）；render area 可以更小。
            uint32_t framebuffer_width = 0;
            uint32_t framebuffer_height = 0;
            auto accept_extent = [&](const ResolvedPassAttachment &attachment) -> bool {
                if (framebuffer_width == 0)
                {
                    framebuffer_width = attachment.extent.width;
                    framebuffer_height = attachment.extent.height;
                    return true;
                }
                if (attachment.extent.width != framebuffer_width ||
                    attachment.extent.height != framebuffer_height)
                {
                    std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 附件尺寸不一致（%ux%u vs %ux%u）\n",
                                label, attachment.extent.width, attachment.extent.height, framebuffer_width,
                                framebuffer_height);
                    return false;
                }
                return true;
            };

            for (size_t i = 0; i < desc.color_attachments.size(); ++i)
            {
                ResolvedPassAttachment resolved;
                std::string error;
                if (!resolve_pass_attachment(desc.color_attachments[i].view, false, resolved, error))
                {
                    std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 颜色附件 #%u: %s\n", label,
                                static_cast<uint32_t>(i), error.c_str());
                    return render_pass;
                }
                if (!accept_extent(resolved))
                    return render_pass;
                resolved_colors.push_back(resolved);
            }
            if (has_depth)
            {
                std::string error;
                if (!resolve_pass_attachment(desc.depth_attachments[0].view, true, resolved_depth, error))
                {
                    std::printf("[ERROR] Vulkan encoder_begin_render_pass(%s): 深度附件: %s\n", label,
                                error.c_str());
                    return render_pass;
                }
                if (!accept_extent(resolved_depth))
                    return render_pass;
            }

            vk::Rect2D render_area;
            if (!resolve_render_area(label, desc, vk::Extent2D{framebuffer_width, framebuffer_height},
                                     render_area))
                return render_pass;

            // 完整附件描述：format/samples/load/store/initial+final layout 缺一不可。
            std::vector<vk::AttachmentDescription> vk_attachments;
            std::vector<vk::ImageView> vk_image_views;
            std::vector<vk::ClearValue> clear_values;
            std::vector<vk::AttachmentReference> color_references;
            vk_attachments.reserve(desc.color_attachments.size() + (has_depth ? 1u : 0u));
            vk_image_views.reserve(desc.color_attachments.size() + (has_depth ? 1u : 0u));
            clear_values.reserve(desc.color_attachments.size() + (has_depth ? 1u : 0u));
            color_references.reserve(desc.color_attachments.size());

            for (size_t i = 0; i < resolved_colors.size(); ++i)
            {
                const auto &attachment = resolved_colors[i];
                const auto &color_attachment = desc.color_attachments[i];

                vk::AttachmentDescription vk_attachment;
                vk_attachment.setFormat(attachment.format);
                vk_attachment.setSamples(vk::SampleCountFlagBits::e1);
                vk_attachment.setLoadOp(get_vk_load_op(color_attachment.load_op));
                vk_attachment.setStoreOp(get_vk_store_op(color_attachment.store_op));
                vk_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
                vk_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
                vk_attachment.setInitialLayout(attachment.initial_layout);
                vk_attachment.setFinalLayout(attachment.final_layout);
                vk_attachments.emplace_back(vk_attachment);

                vk_image_views.emplace_back(*attachment.view->image_view);

                const auto &color = color_attachment.clear_color;
                clear_values.emplace_back(vk::ClearValue{vk::ClearColorValue{
                    std::array<float, 4>{color[0], color[1], color[2], color[3]}}});

                color_references.emplace_back(
                    vk::AttachmentReference{static_cast<uint32_t>(i), vk::ImageLayout::eColorAttachmentOptimal});

                if (color_attachment.load_op == LoadOp::Load &&
                    attachment.initial_layout == vk::ImageLayout::eUndefined)
                {
                    std::printf("[WARN] Vulkan encoder_begin_render_pass(%s): 颜色附件(%s) 初始布局为 Undefined，"
                                "Load 读到的内容未定义\n", label, attachment.texture->label.c_str());
                }
            }

            vk::AttachmentReference depth_reference;
            if (has_depth)
            {
                const auto &attachment = resolved_depth;
                const auto &depth_attachment = desc.depth_attachments[0];

                vk::AttachmentDescription vk_attachment;
                vk_attachment.setFormat(attachment.format);
                vk_attachment.setSamples(vk::SampleCountFlagBits::e1);
                vk_attachment.setLoadOp(get_vk_load_op(depth_attachment.load_op));
                vk_attachment.setStoreOp(get_vk_store_op(depth_attachment.store_op));
                vk_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
                vk_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
                vk_attachment.setInitialLayout(attachment.initial_layout);
                vk_attachment.setFinalLayout(attachment.final_layout);
                vk_attachments.emplace_back(vk_attachment);

                vk_image_views.emplace_back(*attachment.view->image_view);
                clear_values.emplace_back(
                    vk::ClearValue{vk::ClearDepthStencilValue{depth_attachment.clear_depth, 0}});
                depth_reference = vk::AttachmentReference{
                    static_cast<uint32_t>(resolved_colors.size()), vk::ImageLayout::eDepthStencilAttachmentOptimal};

                if (depth_attachment.load_op == LoadOp::Load &&
                    attachment.initial_layout == vk::ImageLayout::eUndefined)
                {
                    std::printf("[WARN] Vulkan encoder_begin_render_pass(%s): 深度附件(%s) 初始布局为 Undefined，"
                                "Load 读到的内容未定义\n", label, attachment.texture->label.c_str());
                }
            }

            vk::SubpassDescription subpass;
            subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
            if (!color_references.empty())
                subpass.setColorAttachments(color_references);
            if (has_depth)
                subpass.setPDepthStencilAttachment(&depth_reference);

            // 所有 render pass（默认帧缓冲 / 显式附件 / 管线兼容 pass）必须使用同一组
            // dependency：Vulkan 的 render pass 兼容性要求它们除 initial/final layout、
            // load/store 与 attachment reference 的 layout 之外完全一致（G-09）。
            std::array<vk::SubpassDependency, 2> dependencies =
                vk_render_pass_util::make_render_pass_dependencies();

            vk::RenderPassCreateInfo create_info;
            create_info.setAttachments(vk_attachments);
            create_info.setSubpasses(subpass);
            create_info.setDependencies(dependencies);

            render_pass_data = memnew(VkRenderPassHandleData);
            render_pass_data->render_pass = device.createRenderPass(create_info);
            render_pass_data->command_encoder_data = command_encoder_data;
            render_pass_data->render_area = render_area;
            for (const auto &attachment : resolved_colors)
                render_pass_data->layout_updates.push_back({attachment.texture, attachment.final_layout});
            if (has_depth)
                render_pass_data->layout_updates.push_back({resolved_depth.texture, resolved_depth.final_layout});

            vk::FramebufferCreateInfo fb_info;
            fb_info.setRenderPass(*render_pass_data->render_pass);
            fb_info.setAttachments(vk_image_views);
            fb_info.setWidth(framebuffer_width);
            fb_info.setHeight(framebuffer_height);
            fb_info.setLayers(1);
            render_pass_data->framebuffer = device.createFramebuffer(fb_info);

            vk::RenderPassBeginInfo begin_info;
            begin_info.setRenderArea(render_area);
            begin_info.setRenderPass(*render_pass_data->render_pass);
            begin_info.setFramebuffer(*render_pass_data->framebuffer);
            begin_info.setClearValues(clear_values);
            command_encoder_data->command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);
            // 默认动态状态：覆盖整个 render area 的 viewport + scissor（G-04）。
            record_default_viewport_and_scissor(command_encoder_data->command_buffer, render_area);
            std::printf("[INFO] Vulkan began render pass(%s): %u color + %s depth, framebuffer %ux%u, area %ux%u\n",
                        label, static_cast<uint32_t>(resolved_colors.size()), has_depth ? "1" : "0",
                        framebuffer_width, framebuffer_height, render_area.extent.width,
                        render_area.extent.height);
        }

        // 归 encoder 所有：encoder_finish 时移交 command buffer，destroy_command_buffer 释放。
        command_encoder_data->render_passes.push_back(render_pass_data);

        render_pass.handle = alloc_handle_id();
        render_pass.data = render_pass_data;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("encoder_begin_render_pass", error);
        memdelete(render_pass_data);
        return {};
    }
    return render_pass;
}

/// @brief 绑定渲染管线。
void VkRenderingDevice::pass_set_pipeline(RenderPassHandle pass, RenderPipelineHandle pipeline)
{
    const auto render_pass_data = resolve_recording_pass("pass_set_pipeline", pass);
    if (render_pass_data == nullptr)
        return;
    if (!pipeline.is_valid() || pipeline.data == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_set_pipeline: invalid render pipeline handle\n");
        return;
    }
    const auto render_pipeline_data = static_cast<VkGraphicsPipelineHandleData *>(pipeline.data);
    // 记录当前管线：后续的 bind group / vertex buffer / index buffer / draw 校验依赖它的
    // pipeline layout 与 vertex input 布局。
    render_pass_data->pipeline = render_pipeline_data;
    render_pass_data->command_encoder_data->command_buffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics, *render_pipeline_data->pipeline);
}

/// @brief 绑定绑定组（VkDescriptorSet + 动态偏移）。
///
/// 校验：
///   - 调用前必须已经 `pass_set_pipeline`（vkCmdBindDescriptorSets 需要 pipeline layout）；
///   - group_index 必须在 pipeline layout 的 set 范围内，且 bind group 的布局必须与
///     该 group 的布局完全一致；
///   - dynamic offsets 的数量必须等于布局里 `has_dynamic_offset` 的项数（按 binding
///     编号升序消费），每个偏移按 minUniformBufferOffsetAlignment 对齐，并且叠加
///     descriptor 静态 offset 后区间必须完整落在缓冲区内。
void VkRenderingDevice::pass_set_bind_group(RenderPassHandle pass, uint32_t group_index,
                                            BindGroupHandle bind_group, const uint32_t *dynamic_offsets,
                                            uint32_t dynamic_offset_count)
{
    const auto render_pass_data = resolve_recording_pass("pass_set_bind_group", pass);
    if (render_pass_data == nullptr)
        return;
    if (!bind_group.is_valid() || bind_group.data == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_set_bind_group: invalid bind group handle\n");
        return;
    }
    // Vulkan 的 vkCmdBindDescriptorSets 需要 pipeline layout，而这个接口不提供显式
    // layout 参数，只能从当前绑定的管线取得。
    if (render_pass_data->pipeline == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_set_bind_group: 必须先 pass_set_pipeline（需要 pipeline layout）\n");
        return;
    }
    const auto pipeline_layout_data = render_pass_data->pipeline->pipeline_layout;
    if (pipeline_layout_data == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_set_bind_group: 当前管线没有 pipeline layout\n");
        return;
    }
    if (group_index >= pipeline_layout_data->bind_group_layouts.size())
    {
        std::printf("[ERROR] Vulkan pass_set_bind_group: group index %u 超出 pipeline layout(%s) 的 %zu 个 set\n",
                    group_index, pipeline_layout_data->label.c_str(),
                    pipeline_layout_data->bind_group_layouts.size());
        return;
    }

    const auto bind_group_data = static_cast<VkBindGroupHandleData *>(bind_group.data);
    const VkBindGroupLayoutHandleData *expected_layout =
        pipeline_layout_data->bind_group_layouts[group_index];
    // Vulkan 要求 set 与 pipeline layout 中对应 set layout 兼容；本后端创建的布局
    // 是一一对应的对象，因此直接比较指针即可。
    if (bind_group_data->layout != expected_layout)
    {
        std::printf("[ERROR] Vulkan pass_set_bind_group(group %u): bind group(%s) 的布局与 pipeline layout 不一致\n",
                    group_index, bind_group_data->label.c_str());
        return;
    }

    // 动态偏移对应的 binding 按编号升序消费；数量必须与布局完全一致。
    std::vector<uint32_t> dynamic_bindings;
    for (const VkBindGroupLayoutEntryData &entry : expected_layout->entries)
    {
        if (entry.has_dynamic_offset)
            dynamic_bindings.push_back(entry.binding);
    }
    std::sort(dynamic_bindings.begin(), dynamic_bindings.end());
    if (dynamic_offset_count != dynamic_bindings.size())
    {
        std::printf("[ERROR] Vulkan pass_set_bind_group(group %u): 动态偏移数量 %u 与布局要求的 %zu 不一致\n",
                    group_index, dynamic_offset_count, dynamic_bindings.size());
        return;
    }
    if (dynamic_offset_count > 0 && dynamic_offsets == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_set_bind_group(group %u): dynamic_offsets 为空但布局要求动态偏移\n",
                    group_index);
        return;
    }

    const uint64_t alignment = physical_device.getProperties().limits.minUniformBufferOffsetAlignment;
    std::vector<uint32_t> offsets(dynamic_offset_count);
    for (size_t i = 0; i < dynamic_bindings.size(); ++i)
    {
        const uint32_t binding_number = dynamic_bindings[i];
        const uint32_t offset = dynamic_offsets[i];
        if (alignment > 1 && (offset % alignment) != 0)
        {
            std::printf("[ERROR] Vulkan pass_set_bind_group(group %u, binding %u): 动态偏移 %u 未按 "
                        "minUniformBufferOffsetAlignment %llu 对齐\n",
                        group_index, binding_number, offset, static_cast<unsigned long long>(alignment));
            return;
        }

        const VkBindGroupBindingData *binding = nullptr;
        for (const VkBindGroupBindingData &candidate : bind_group_data->bindings)
        {
            if (candidate.binding == binding_number)
            {
                binding = &candidate;
                break;
            }
        }
        if (binding == nullptr || binding->buffer == nullptr)
        {
            std::printf("[ERROR] Vulkan pass_set_bind_group(group %u, binding %u): 动态绑定缺少缓冲区\n",
                        group_index, binding_number);
            return;
        }
        // 动态偏移叠加在 descriptor 的静态 offset 上；最终区间必须完整落在缓冲区内。
        const uint64_t base = binding->offset + offset;
        if (base > binding->buffer->size || binding->size > binding->buffer->size - base)
        {
            std::printf("[ERROR] Vulkan pass_set_bind_group(group %u, binding %u): 动态偏移 %u 使区间 "
                        "[%llu, %llu) 超出缓冲区(%s) 的 %llu 字节\n",
                        group_index, binding_number, offset, static_cast<unsigned long long>(base),
                        static_cast<unsigned long long>(base + binding->size),
                        binding->buffer->label.c_str(),
                        static_cast<unsigned long long>(binding->buffer->size));
            return;
        }
        offsets[i] = offset;
    }

    // 单个 set：firstSet = group_index，后续 set 保持不变（与 WebGPU 的 per-group 绑定一致）。
    const std::vector<vk::DescriptorSet> descriptor_sets = {*bind_group_data->descriptor_set};
    render_pass_data->command_encoder_data->command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipeline_layout_data->pipeline_layout, group_index,
        descriptor_sets, offsets);
}

/// @brief 绑定某个 slot 的顶点缓冲区。
void VkRenderingDevice::pass_set_vertex_buffer(RenderPassHandle pass, uint32_t slot, GPUBufferHandle buffer,
                                               uint64_t offset)
{
    const auto render_pass_data = resolve_recording_pass("pass_set_vertex_buffer", pass);
    if (render_pass_data == nullptr)
        return;
    if (!buffer.is_valid() || buffer.data == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_set_vertex_buffer: invalid buffer handle\n");
        return;
    }
    const auto buffer_data = static_cast<VkBufferHandleData *>(buffer.data);
    if ((buffer_data->usage & GPUBufferUsage::VERTEX) == 0)
    {
        std::printf("[ERROR] Vulkan pass_set_vertex_buffer(slot %u): 缓冲区(%s) 没有 VERTEX 用途\n", slot,
                    buffer_data->label.c_str());
        return;
    }
    if (offset > buffer_data->size)
    {
        std::printf("[ERROR] Vulkan pass_set_vertex_buffer(slot %u): offset %llu 越过缓冲区(%s) 的 %llu 字节\n",
                    slot, static_cast<unsigned long long>(offset), buffer_data->label.c_str(),
                    static_cast<unsigned long long>(buffer_data->size));
        return;
    }
    if (render_pass_data->pipeline != nullptr)
    {
        if (slot >= render_pass_data->pipeline->vertex_buffer_count)
        {
            std::printf("[ERROR] Vulkan pass_set_vertex_buffer: slot %u 超出管线(%s) 的 %u 个顶点缓冲区\n",
                        slot, render_pass_data->pipeline->label.c_str(),
                        render_pass_data->pipeline->vertex_buffer_count);
            return;
        }
    }
    else
    {
        // 还没绑定管线时只能按设备上限校验 slot；真正的布局校验在 draw 前由管线决定。
        const uint32_t max_bindings = physical_device.getProperties().limits.maxVertexInputBindings;
        if (slot >= max_bindings)
        {
            std::printf("[ERROR] Vulkan pass_set_vertex_buffer: slot %u 超出 maxVertexInputBindings %u\n", slot,
                        max_bindings);
            return;
        }
    }

    const std::vector<vk::Buffer> buffers = {*buffer_data->buffer};
    const std::vector<vk::DeviceSize> offsets = {static_cast<vk::DeviceSize>(offset)};
    render_pass_data->command_encoder_data->command_buffer.bindVertexBuffers(slot, buffers, offsets);
}

/// @brief 绑定索引缓冲区。
void VkRenderingDevice::pass_set_index_buffer(RenderPassHandle pass, GPUBufferHandle buffer, uint64_t offset,
                                              IndexFormat format)
{
    const auto render_pass_data = resolve_recording_pass("pass_set_index_buffer", pass);
    if (render_pass_data == nullptr)
        return;
    if (!buffer.is_valid() || buffer.data == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_set_index_buffer: invalid buffer handle\n");
        return;
    }
    const auto buffer_data = static_cast<VkBufferHandleData *>(buffer.data);
    if ((buffer_data->usage & GPUBufferUsage::INDEX) == 0)
    {
        std::printf("[ERROR] Vulkan pass_set_index_buffer: 缓冲区(%s) 没有 INDEX 用途\n",
                    buffer_data->label.c_str());
        return;
    }
    vk::IndexType index_type = vk::IndexType::eUint32;
    uint64_t element_size = 0;
    if (!get_vk_index_info(format, index_type, element_size))
    {
        std::printf("[ERROR] Vulkan pass_set_index_buffer: 未知 IndexFormat\n");
        return;
    }
    // Vulkan 要求索引缓冲的起始地址（缓冲区基址 + offset）按元素大小对齐；基址由
    // 驱动保证对齐，因此这里要求 offset 是 2/4 的整数倍。
    if (offset % element_size != 0)
    {
        std::printf("[ERROR] Vulkan pass_set_index_buffer: offset %llu 不是索引元素大小 %llu 的整数倍\n",
                    static_cast<unsigned long long>(offset), static_cast<unsigned long long>(element_size));
        return;
    }
    if (offset > buffer_data->size)
    {
        std::printf("[ERROR] Vulkan pass_set_index_buffer: offset %llu 越过缓冲区(%s) 的 %llu 字节\n",
                    static_cast<unsigned long long>(offset), buffer_data->label.c_str(),
                    static_cast<unsigned long long>(buffer_data->size));
        return;
    }
    // 记录索引缓冲区状态供 pass_draw_indexed 校验索引区间。索引缓冲状态与管线无关
    // （Vulkan 的顶点/索引绑定是独立动态状态），切换管线不需要清空。
    render_pass_data->index_buffer = buffer_data;
    render_pass_data->index_buffer_offset = offset;
    render_pass_data->index_element_size = element_size;
    render_pass_data->command_encoder_data->command_buffer.bindIndexBuffer(
        *buffer_data->buffer, static_cast<vk::DeviceSize>(offset), index_type);
}

/// @brief 设置 viewport，并同步派生/裁剪 scissor（接口没有独立 scissor 命令，G-04）。
void VkRenderingDevice::pass_set_viewport(RenderPassHandle pass, float x, float y, float width, float height,
                                          float min_depth, float max_depth)
{
    const auto render_pass_data = resolve_recording_pass("pass_set_viewport", pass);
    if (render_pass_data == nullptr)
        return;
    if (!validate_viewport(physical_device.getProperties().limits, x, y, width, height, min_depth, max_depth))
        return;
    const vk::Viewport viewport(x, y, width, height, min_depth, max_depth);
    record_viewport_and_scissor(render_pass_data->command_encoder_data->command_buffer,
                                render_pass_data->render_area, viewport);
}

/// @brief 索引绘制（V6-2b）。
///
/// Vulkan 要求 `vkCmdDrawIndexed` 之前已经绑定索引缓冲区，因此这里校验：
///   - pass 仍在录制（`resolve_recording_pass`）、已经 `pass_set_pipeline`；
///   - 已经 `pass_set_index_buffer`；
///   - `offset + (first_index + index_count) * element_size` 不越过索引缓冲区。
/// index_count / instance_count 为 0 时不产生图元，直接返回（合法空操作）。
void VkRenderingDevice::pass_draw_indexed(RenderPassHandle pass, uint32_t index_count, uint32_t instance_count,
                                          uint32_t first_index, int32_t base_vertex)
{
    const auto render_pass_data = resolve_recording_pass("pass_draw_indexed", pass);
    if (render_pass_data == nullptr)
        return;
    if (render_pass_data->pipeline == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_draw_indexed: 必须先 pass_set_pipeline\n");
        return;
    }
    if (render_pass_data->index_buffer == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_draw_indexed: 必须先 pass_set_index_buffer\n");
        return;
    }
    if (index_count == 0 || instance_count == 0)
        return;

    const uint64_t index_end = static_cast<uint64_t>(first_index) + static_cast<uint64_t>(index_count);
    const uint64_t byte_end =
        render_pass_data->index_buffer_offset + index_end * render_pass_data->index_element_size;
    if (byte_end > render_pass_data->index_buffer->size)
    {
        std::printf("[ERROR] Vulkan pass_draw_indexed: 索引区间 [%llu, %llu) 超出索引缓冲区(%s) 的 %llu 字节"
                    "（first_index %u, index_count %u, 元素大小 %llu）\n",
                    static_cast<unsigned long long>(render_pass_data->index_buffer_offset),
                    static_cast<unsigned long long>(byte_end),
                    render_pass_data->index_buffer->label.c_str(),
                    static_cast<unsigned long long>(render_pass_data->index_buffer->size), first_index,
                    index_count, static_cast<unsigned long long>(render_pass_data->index_element_size));
        return;
    }

    render_pass_data->command_encoder_data->command_buffer.drawIndexed(
        index_count, instance_count, first_index, base_vertex, 0);
}

/// @brief 非索引绘制（V6-2b）。
///
/// 只要求 pass 仍在录制且已经 `pass_set_pipeline`：顶点数据来源由管线声明的顶点
/// 缓冲区决定，完全用 `gl_VertexIndex` 的 shader 合法地不需要顶点缓冲区，因此这里
/// 不强制要求 `pass_set_vertex_buffer`。vertex_count / instance_count 为 0 时直接返回。
void VkRenderingDevice::pass_draw(RenderPassHandle pass, uint32_t vertex_count, uint32_t instance_count,
                                  uint32_t first_vertex)
{
    const auto render_pass_data = resolve_recording_pass("pass_draw", pass);
    if (render_pass_data == nullptr)
        return;
    if (render_pass_data->pipeline == nullptr)
    {
        std::printf("[ERROR] Vulkan pass_draw: 必须先 pass_set_pipeline\n");
        return;
    }
    if (vertex_count == 0 || instance_count == 0)
        return;
    render_pass_data->command_encoder_data->command_buffer.draw(vertex_count, instance_count, first_vertex, 0);
}

/// @brief 结束渲染通道。
void VkRenderingDevice::pass_end(RenderPassHandle pass)
{
    const auto render_pass_data = resolve_recording_pass("pass_end", pass);
    if (render_pass_data == nullptr)
        return;
    render_pass_data->command_encoder_data->command_buffer.endRenderPass();
    // pass 结束后不再接受任何绑定/draw 命令（否则 vkCmdEndRenderPass 之后录制的命令
    // 在 submit 时会被 validation 拒绝）。
    render_pass_data->active = false;

    // 布局状态按录制顺序推进：后续 pass/上传会把它当作 oldLayout。这里假设录制最终会被
    // 提交并按顺序执行（queue_submit 会真正提交）；录制后被丢弃会让布局簿记失真。
    for (const auto &update : render_pass_data->layout_updates)
    {
        if (update.texture != nullptr)
            update.texture->current_layout = update.final_layout;
    }
    render_pass_data->layout_updates.clear();
}

/// @brief 结束录制：把命令池与 command buffer 一起转移给 CommandBufferHandleData。
CommandBufferHandle VkRenderingDevice::encoder_finish(CommandEncoderHandle encoder)
{
    CommandBufferHandle cmd_buffer;
    if (!encoder.is_valid() || encoder.data == nullptr)
    {
        std::printf("[ERROR] Vulkan encoder_finish: invalid encoder handle\n");
        return cmd_buffer;
    }
    auto cmd_encoder_data = static_cast<VkCommandEncoderHandleData *>(encoder.data);

    try
    {
        cmd_encoder_data->command_buffer.end();
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("encoder_finish", error);
        // 录制失败时 command buffer 未产生，它本应持有的 pass 内部对象在这里回收。
        for (auto *render_pass_data : cmd_encoder_data->render_passes)
            memdelete(render_pass_data);
        memdelete(cmd_encoder_data);
        return {};
    }

    auto cmd_buffer_data = memnew(VkCommandBufferHandleData);
    cmd_buffer_data->command_pool = std::move(cmd_encoder_data->command_pool);
    cmd_buffer_data->command_buffer = std::move(cmd_encoder_data->command_buffer);
    // render pass/framebuffer 的所有权随录制结果移交给 command buffer：
    // destroy_command_buffer（提交执行完之后）统一释放，避免每个 pass 新建即泄漏。
    cmd_buffer_data->render_passes = std::move(cmd_encoder_data->render_passes);
    for (auto *render_pass_data : cmd_buffer_data->render_passes)
        render_pass_data->command_encoder_data = nullptr;
    memdelete(cmd_encoder_data);

    cmd_buffer.handle = alloc_handle_id();
    cmd_buffer.data = cmd_buffer_data;
    return cmd_buffer;
}

void VkRenderingDevice::destroy_command_buffer(CommandBufferHandle cmd)
{
    if (!cmd.is_valid() || cmd.data == nullptr)
        return;
    auto cmd_buffer_data = static_cast<VkCommandBufferHandleData *>(cmd.data);

    // 先销毁 command buffer/pool（解除对 render pass/framebuffer 的引用），再释放录制期
    // 创建的对象。调用约定：必须在命令缓冲提交执行完之后调用。
    const std::vector<VkRenderPassHandleData *> render_passes = std::move(cmd_buffer_data->render_passes);
    memdelete(cmd_buffer_data);
    for (auto *render_pass_data : render_passes)
        memdelete(render_pass_data);
}

/// @brief 一次性提交：临时命令池 + command buffer + fence，等待 GPU 执行完再返回。
///
/// 用于 queue_write_texture / generate_mip_map。每次调用都新建命令池和 fence，
/// 避免与帧录制的 encoder 共享状态；提交进同一个图形队列，因此执行顺序天然是
/// "先提交的上传 → 后提交的帧"，满足接口"写入先于本次提交的绘制可见"的约定。
/// 代价是每次调用有一次 submit + fence 等待，纹理上传只发生一次，当前可接受；
/// V6 做帧 fence / 延迟销毁时可以把这条路径并入按帧复用的上传命令池。
bool VkRenderingDevice::submit_immediate_commands(
    const char *what, const std::function<void(const vk::raii::CommandBuffer &command_buffer)> &record)
{
    try
    {
        const auto [graphics_family, _] = findQueueFamilies(physical_device);
        if (!graphics_family.has_value())
        {
            std::printf("[ERROR] Vulkan %s: no graphics queue family\n", what);
            return false;
        }

        vk::CommandPoolCreateInfo pool_create_info;
        pool_create_info.setQueueFamilyIndex(graphics_family.value());
        // 一次性命令缓冲，适合用 transient 池（驱动可以按此优化内存）。
        pool_create_info.setFlags(vk::CommandPoolCreateFlagBits::eTransient);
        vk::raii::CommandPool command_pool(device, pool_create_info);

        vk::CommandBufferAllocateInfo alloc_info;
        alloc_info.setCommandPool(*command_pool);
        alloc_info.setCommandBufferCount(1);
        alloc_info.setLevel(vk::CommandBufferLevel::ePrimary);
        auto command_buffers = device.allocateCommandBuffers(alloc_info);
        if (command_buffers.empty())
        {
            std::printf("[ERROR] Vulkan %s: failed to allocate a command buffer\n", what);
            return false;
        }

        vk::CommandBufferBeginInfo begin_info;
        begin_info.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        command_buffers[0].begin(begin_info);
        record(command_buffers[0]);
        command_buffers[0].end();

        // setCommandBuffers 只接受 ArrayProxyNoTemporaries，必须传入 lvalue 句柄，
        // 且该句柄要活到 submit 调用结束。
        vk::CommandBuffer raw_command_buffer = *command_buffers[0];
        vk::SubmitInfo submit_info;
        submit_info.setCommandBuffers(raw_command_buffer);
        vk::raii::Fence fence(device, vk::FenceCreateInfo{});
        (*graphics_queue).submit(submit_info, *fence);
        // D-38：wait 的返回值必须处理（超时意味着 GPU 没有在预期时间内完成），
        // 不能直接忽略 nodiscard 结果。
        const vk::Result wait_result = device.waitForFences(*fence, VK_TRUE, UINT64_MAX);
        if (wait_result != vk::Result::eSuccess)
        {
            std::printf("[ERROR] Vulkan %s: waitForFences 返回 %d\n", what, static_cast<int>(wait_result));
            return false;
        }
        return true;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error(what, error);
        return false;
    }
}

/// @brief 提交录制的命令缓冲（V6-3：帧 fence + acquire/present semaphore 链）。
///
/// 帧同步由后端内部维护（接口没有显式帧对象，G-08）：
///   1. 本帧第一次进入默认帧缓冲的 `encoder_begin_render_pass` 会 acquire 交换链图像，
///      同时等待并 reset 本帧槽位的 in_flight fence（见 `frame_fence_reset`）；
///   2. `queue_submit` 等待 image_available（本帧 acquire 过时）、以 in_flight 作为
///      完成 fence 提交命令、signal render_finished；
///   3. `present` 等 render_finished 后调用 vkQueuePresentKHR，并推进 frame 槽位。
///
/// 当前一次 present 之间只接受一次 queue_submit：多个命令缓冲需要额外的 semaphore
/// 链（最后一个提交才 signal render_finished），留到 V6-4/G-10（多帧在飞与延迟销毁）。
void VkRenderingDevice::queue_submit(CommandBufferHandle cmd)
{
    if (!cmd.is_valid() || cmd.data == nullptr)
    {
        std::printf("[ERROR] Vulkan queue_submit: invalid command buffer handle\n");
        return;
    }
    if (device == nullptr || graphics_queue == nullptr)
    {
        std::printf("[ERROR] Vulkan queue_submit: device/queue 尚未初始化\n");
        return;
    }
    if (frame_submitted)
    {
        std::printf("[ERROR] Vulkan queue_submit: 本帧已经提交过命令缓冲；一次 present 之间只允许一次 "
                    "queue_submit（多提交见 V6-4）\n");
        return;
    }

    auto cmd_buffer_data = static_cast<VkCommandBufferHandleData *>(cmd.data);
    if (cmd_buffer_data->submitted)
    {
        std::printf("[ERROR] Vulkan queue_submit: 同一个 command buffer 不能重复提交\n");
        return;
    }

    FrameSync *sync = nullptr;
    if (!frame_syncs.empty() && current_frame < frame_syncs.size())
        sync = &frame_syncs[current_frame];

    const bool uses_swapchain_image = current_swapchain_image != UINT32_MAX;
    if (uses_swapchain_image && sync == nullptr)
    {
        std::printf("[ERROR] Vulkan queue_submit: 默认帧缓冲需要帧同步对象，但 init_swapchain 尚未创建\n");
        return;
    }

    try
    {
        // 默认帧缓冲的 acquire 已经等待/重置过 fence；纯 offscreen 帧没有 acquire，
        // 在这里补齐等待，保证同一槽位的 fence 下一轮复用时上一轮提交已完成。
        if (!frame_fence_reset && sync != nullptr)
        {
            const vk::Result wait_result = device.waitForFences(*sync->in_flight, VK_TRUE, UINT64_MAX);
            if (wait_result != vk::Result::eSuccess)
            {
                std::printf("[ERROR] Vulkan queue_submit: waitForFences 返回 %d\n",
                            static_cast<int>(wait_result));
                return;
            }
            device.resetFences(*sync->in_flight);
            frame_fence_reset = true;
        }

        std::vector<vk::Semaphore> wait_semaphores;
        std::vector<vk::PipelineStageFlags> wait_stages;
        std::vector<vk::Semaphore> signal_semaphores;
        if (uses_swapchain_image)
        {
            // 等待 acquire 完成后再写交换链图像（颜色附件输出阶段）。
            wait_semaphores.push_back(*sync->image_available);
            wait_stages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
            // 渲染完成后 signal，present 等待它，保证呈现的不是半成品。
            signal_semaphores.push_back(*sync->render_finished);
        }

        const vk::CommandBuffer raw_command_buffer = *cmd_buffer_data->command_buffer;
        vk::SubmitInfo submit_info;
        submit_info.setWaitSemaphores(wait_semaphores);
        submit_info.setWaitDstStageMask(wait_stages);
        submit_info.setCommandBuffers(raw_command_buffer);
        submit_info.setSignalSemaphores(signal_semaphores);

        // setCommandBuffers 只接受 ArrayProxyNoTemporaries，必须传入 lvalue 句柄。
        if (sync != nullptr)
            (*graphics_queue).submit(submit_info, *sync->in_flight);
        else
            (*graphics_queue).submit(submit_info); // 交换链尚未初始化（纯 offscreen 早期用法）

        cmd_buffer_data->submitted = true;
        frame_submitted = true;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("queue_submit", error);
    }
}
