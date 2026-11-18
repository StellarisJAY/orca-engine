#include "rendering_device_refactor.h"
#include "rendering_device.h"
#include "os/memory.h"
#include "window.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main()
{
    // 输出可能被重定向到文件；关闭缓冲，避免进程被提前终止时丢失日志。
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    RenderingDeviceRefactor::create_singleton(RenderingDriverType::Vulkan);
    auto device = RenderingDeviceRefactor::get_singleton();
    if (!device)
    {
        std::printf("SKIP: Vulkan rendering device is not available in this build\n");
        return 0;
    }

    WindowDesc d;
    d.fullscreen = false;
    d.height = 600;
    d.width = 800;
    d.visible = true;
    d.driver = RenderingDriverType::Vulkan;

    // 创建窗口
    auto window = Window::create();
    if (!window->init(d))
        return 1;
    std::printf("[TEST] Window initialized.\n");
    RenderingWindowInfo window_info = window->get_rendering_window_info();

    if (!device->init(window_info))
    {
        std::printf("[TEST] Vulkan init failed.\n");
        window->shutdown();
        return 1;
    }
    if (!device->init_device())
    {
        std::printf("[TEST] Vulkan init_device failed.\n");
        window->shutdown();
        return 1;
    }
    if (!device->init_swapchain())
    {
        std::printf("[TEST] Vulkan init_swapchain failed.\n");
        window->shutdown();
        return 1;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    device->get_swapchain_size(width, height);
    std::printf("[TEST] Vulkan initialized: swapchain format=%d, size=%ux%u\n",
                static_cast<int>(device->get_swapchain_format()), width, height);

    // V0：命令池 + command buffer 的创建/结束/释放链路必须自洽（不涉及绘制）。
    {
        CommandEncoderHandle encoder = device->create_command_encoder();
        if (!encoder.is_valid())
        {
            std::printf("[TEST] create_command_encoder failed.\n");
            return 1;
        }
        CommandBufferHandle command_buffer = device->encoder_finish(encoder);
        if (!command_buffer.is_valid())
        {
            std::printf("[TEST] encoder_finish failed.\n");
            return 1;
        }
        device->destroy_command_buffer(command_buffer);
        std::printf("[TEST] command encoder round-trip OK.\n");
    }

    // V1：采样器映射 + destroy 链路。
    {
        SamplerHandle sampler = device->create_sampler(SamplerDesc{});
        if (!sampler.is_valid())
        {
            std::printf("[TEST] create_sampler failed.\n");
            return 1;
        }
        device->destroy_sampler(sampler);
        std::printf("[TEST] sampler create/destroy OK.\n");
    }

    // V2：create_texture 必须完成 VkImage + 独占显存分配/绑定，且颜色/深度/cubemap
    // 三种形态都能创建视图并销毁（内容上传与 mipmap 生成仍不在本用例范围）。
    {
        TextureDescRefactor color_desc;
        color_desc.label = "smoke-color";
        color_desc.format = Format::RGBA8UnormSrgb;
        color_desc.width = 64;
        color_desc.height = 64;
        color_desc.mip_levels = 1;
        color_desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::COPY_DST | TextureUsage::COPY_SRC;
        TextureHandle color = device->create_texture(color_desc);
        if (!color.is_valid())
        {
            std::printf("[TEST] create_texture(color) failed.\n");
            return 1;
        }

        TextureViewDesc color_view_desc;
        color_view_desc.dim = TextureViewDimension::Texture2D;
        color_view_desc.aspect = TextureAspect::Color;
        TextureViewHandle color_view = device->create_texture_view(color, color_view_desc);
        if (!color_view.is_valid())
        {
            std::printf("[TEST] create_texture_view(color) failed.\n");
            return 1;
        }
        device->destroy_texture_view(color_view);
        device->destroy_texture(color);

        TextureDescRefactor depth_desc;
        depth_desc.label = "smoke-depth";
        depth_desc.format = Format::Depth32Float;
        depth_desc.width = 80;
        depth_desc.height = 60;
        depth_desc.mip_levels = 1;
        depth_desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT;
        TextureHandle depth = device->create_texture(depth_desc);
        if (!depth.is_valid())
        {
            std::printf("[TEST] create_texture(depth) failed.\n");
            return 1;
        }

        TextureViewDesc depth_view_desc;
        depth_view_desc.aspect = TextureAspect::DepthOnly;
        TextureViewHandle depth_view = device->create_texture_view(depth, depth_view_desc);
        if (!depth_view.is_valid())
        {
            std::printf("[TEST] create_texture_view(depth) failed.\n");
            return 1;
        }
        device->destroy_texture_view(depth_view);
        device->destroy_texture(depth);

        TextureDescRefactor cube_desc;
        cube_desc.label = "smoke-cube";
        cube_desc.dim = TextureDimension::Cube;
        cube_desc.format = Format::R8Unorm;
        cube_desc.width = 16;
        cube_desc.height = 16;
        cube_desc.mip_levels = 5;
        cube_desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::COPY_DST;
        TextureHandle cube = device->create_texture(cube_desc);
        if (!cube.is_valid())
        {
            std::printf("[TEST] create_texture(cube) failed.\n");
            return 1;
        }

        TextureViewDesc cube_view_desc;
        cube_view_desc.dim = TextureViewDimension::Cube;
        cube_view_desc.aspect = TextureAspect::Color;
        TextureViewHandle cube_view = device->create_texture_view(cube, cube_view_desc);
        if (!cube_view.is_valid())
        {
            std::printf("[TEST] create_texture_view(cube) failed.\n");
            return 1;
        }
        device->destroy_texture_view(cube_view);
        device->destroy_texture(cube);

        std::printf("[TEST] texture create/bind/destroy OK (color + depth + cubemap).\n");
    }

    // V3：queue_write_texture（staging + layout transition）与 generate_mip_map。
    {
        // 1) 2D 纹理：先整图紧密上传，再用带行距（bytes_per_row）的部分区域上传，
        //    后者覆盖"CPU 侧按行重排成紧密打包"的路径。
        TextureDescRefactor upload_desc;
        upload_desc.label = "smoke-upload";
        upload_desc.format = Format::RGBA8UnormSrgb;
        upload_desc.width = 32;
        upload_desc.height = 32;
        upload_desc.mip_levels = 1;
        upload_desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::COPY_DST | TextureUsage::COPY_SRC;
        TextureHandle upload = device->create_texture(upload_desc);
        if (!upload.is_valid())
        {
            std::printf("[TEST] create_texture(upload) failed.\n");
            return 1;
        }

        std::vector<uint8_t> pixels(32u * 32u * 4u);
        for (uint32_t y = 0; y < 32; ++y)
        {
            for (uint32_t x = 0; x < 32; ++x)
            {
                const size_t offset = (static_cast<size_t>(y) * 32 + x) * 4;
                pixels[offset + 0] = static_cast<uint8_t>(x * 8);
                pixels[offset + 1] = static_cast<uint8_t>(y * 8);
                pixels[offset + 2] = static_cast<uint8_t>((x + y) * 4);
                pixels[offset + 3] = 255;
            }
        }

        TextureWriteDesc full_write;
        full_write.data = pixels.data();
        full_write.data_size = pixels.size();
        device->queue_write_texture(upload, full_write);

        const uint32_t patch_width = 8;
        const uint32_t patch_height = 6;
        const uint32_t patch_row_bytes = patch_width * 4 + 8; // 每行 8 字节填充
        std::vector<uint8_t> patch(static_cast<size_t>(patch_height - 1) * patch_row_bytes + patch_width * 4, 0);
        for (uint32_t y = 0; y < patch_height; ++y)
        {
            for (uint32_t x = 0; x < patch_width; ++x)
            {
                const size_t offset = static_cast<size_t>(y) * patch_row_bytes + x * 4;
                patch[offset + 0] = 200;
                patch[offset + 1] = static_cast<uint8_t>(x * 16);
                patch[offset + 2] = static_cast<uint8_t>(y * 16);
                patch[offset + 3] = 255;
            }
        }

        TextureWriteDesc patch_write;
        patch_write.origin_x = 12;
        patch_write.origin_y = 9;
        patch_write.width = patch_width;
        patch_write.height = patch_height;
        patch_write.bytes_per_row = patch_row_bytes;
        patch_write.data = patch.data();
        patch_write.data_size = patch.size();
        device->queue_write_texture(upload, patch_write);

        // 非法上传请求必须被入口拒绝（只打印 [ERROR]，不能崩溃）。
        TextureWriteDesc bad_write;
        bad_write.mip_level = 1; // 该纹理只有 1 层
        bad_write.data = pixels.data();
        bad_write.data_size = pixels.size();
        device->queue_write_texture(upload, bad_write);

        TextureWriteDesc null_write;
        device->queue_write_texture(upload, null_write);

        std::printf("[TEST] texture upload OK (full + padded partial region).\n");
        device->destroy_texture(upload);

        // 2) cubemap：逐面上传 level 0，再生成 5 级 mip 链（vkCmdBlitImage）。
        TextureDescRefactor cube_desc;
        cube_desc.label = "smoke-cube-mips";
        cube_desc.dim = TextureDimension::Cube;
        cube_desc.format = Format::R8Unorm;
        cube_desc.width = 16;
        cube_desc.height = 16;
        cube_desc.mip_levels = 5;
        cube_desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::COPY_DST | TextureUsage::COPY_SRC;
        TextureHandle cube = device->create_texture(cube_desc);
        if (!cube.is_valid())
        {
            std::printf("[TEST] create_texture(cube-mips) failed.\n");
            return 1;
        }

        std::vector<uint8_t> face(16u * 16u);
        for (uint32_t layer = 0; layer < 6; ++layer)
        {
            for (uint32_t y = 0; y < 16; ++y)
            {
                for (uint32_t x = 0; x < 16; ++x)
                    face[static_cast<size_t>(y) * 16 + x] = static_cast<uint8_t>((x + y + layer) * 5);
            }
            TextureWriteDesc face_write;
            face_write.array_layer = layer;
            face_write.data = face.data();
            face_write.data_size = face.size();
            device->queue_write_texture(cube, face_write);
        }
        device->generate_mip_map(cube);
        std::printf("[TEST] cubemap face upload + mipmap generation OK.\n");
        device->destroy_texture(cube);

        // 3) usage 校验：未带 COPY_DST 的纹理不允许上传。深度纹理正是这种形态
        //    （TEXTURE_BINDING | RENDER_ATTACHMENT），上传应在入口被拒绝。
        TextureDescRefactor no_copy_desc;
        no_copy_desc.label = "smoke-no-copy";
        no_copy_desc.format = Format::Depth32Float;
        no_copy_desc.width = 16;
        no_copy_desc.height = 16;
        no_copy_desc.mip_levels = 1;
        no_copy_desc.usage = TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT;
        TextureHandle no_copy = device->create_texture(no_copy_desc);
        if (!no_copy.is_valid())
        {
            std::printf("[TEST] create_texture(no-copy) failed.\n");
            return 1;
        }
        std::vector<uint8_t> depth_pixels(16u * 16u * 4u, 0);
        TextureWriteDesc no_copy_write;
        no_copy_write.data = depth_pixels.data();
        no_copy_write.data_size = depth_pixels.size();
        device->queue_write_texture(no_copy, no_copy_write); // 期望打印 ERROR 后返回
        device->generate_mip_map(no_copy);                   // 1 层 → no-op
        device->destroy_texture(no_copy);
        std::printf("[TEST] upload usage validation handled.\n");
    }

    // V1：非法描述必须在 create 入口被拒绝，不能进入 driver。
    {
        TextureDescRefactor bad_texture;
        bad_texture.format = Format::Undefined;
        bad_texture.width = 16;
        bad_texture.height = 16;
        if (device->create_texture(bad_texture).is_valid())
        {
            std::printf("[TEST] create_texture accepted an invalid description.\n");
            return 1;
        }

        SamplerDesc bad_sampler;
        bad_sampler.lod_min_clamp = -1.0f;
        if (device->create_sampler(bad_sampler).is_valid())
        {
            std::printf("[TEST] create_sampler accepted an invalid description.\n");
            return 1;
        }

        RenderPipelineDesc bad_pipeline; // 没有 pipeline layout / shader module
        if (device->create_render_pipeline(bad_pipeline).is_valid())
        {
            std::printf("[TEST] create_render_pipeline accepted an invalid description.\n");
            return 1;
        }
        std::printf("[TEST] invalid descriptions rejected.\n");
    }

    device->queue_wait_idle();

    // 供 CI/脚本做"只初始化"验证；未设置时保持原来的交互窗口循环。
    if (std::getenv("ORCA_VULKAN_INIT_TEST_EXIT_AFTER_INIT") != nullptr)
    {
        device->shutdown();
        window->shutdown();
        RenderingDeviceRefactor::destroy_singleton();
        std::printf("[TEST] ORCA_VULKAN_INIT_TEST_EXIT_AFTER_INIT is set, exiting early.\n");
        return 0;
    }

    while (!window->get_should_close())
    {
        window->poll_event();
        window->swap_buffers();
    }

    device->shutdown();
    window->shutdown();
    RenderingDeviceRefactor::destroy_singleton();
    return 0;
}
