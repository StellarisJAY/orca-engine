#include "rendering_device_refactor.h"
#include "rendering_device.h"
#include "shader_loader.h"
#include "os/memory.h"
#include "window.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
    d.resizable = true;
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

    // V3-1 / I-03：init_swapchain 现在同时创建 image view、每图像深度缓冲、
    // 默认 render pass + framebuffer 与 acquire/present 同步对象。重复调用会走
    // oldSwapchain 重建路径（释放旧依赖资源后重建），成功后尺寸必须保持不变。
    {
        if (!device->init_swapchain())
        {
            std::printf("[TEST] re-running init_swapchain failed.\n");
            return 1;
        }
        uint32_t new_width = 0;
        uint32_t new_height = 0;
        device->get_swapchain_size(new_width, new_height);
        if (new_width != width || new_height != height)
        {
            std::printf("[TEST] swapchain size changed after re-init: %ux%u -> %ux%u\n",
                        width, height, new_width, new_height);
            return 1;
        }
        std::printf("[TEST] swapchain re-init OK (image view/depth/framebuffer/sync recreated).\n");
    }

    // I-04：resize_swapchain 必须走 oldSwapchain 路径重建交换链与全部依赖资源。
    // 先在零尺寸（模拟窗口最小化）下确认只记录、不重建，再改真实窗口大小验证新 extent。
    {
        device->resize_swapchain(0, 0);
        uint32_t zero_width = 0;
        uint32_t zero_height = 0;
        device->get_swapchain_size(zero_width, zero_height);
        if (zero_width != width || zero_height != height)
        {
            std::printf("[TEST] zero-sized resize changed the swapchain: %ux%u\n", zero_width, zero_height);
            return 1;
        }
        std::printf("[TEST] zero-sized resize deferred OK.\n");

#if defined(_WIN32)
        // GLFW 在 Win32 下通过 RenderingWindowInfo::window 暴露 HWND；这里直接改客户区
        // 尺寸来模拟平台窗口 resize。SDL3 不填该字段，跳过这段真实 resize 检查
        // （交换链重建本身仍由上面的重复 init_swapchain 用例覆盖）。
        if (window_info.window != nullptr)
        {
            HWND hwnd = static_cast<HWND>(window_info.window);
            RECT window_rect {};
            RECT client_rect {};
            if (GetWindowRect(hwnd, &window_rect) && GetClientRect(hwnd, &client_rect))
            {
                const uint32_t target_width = 640;
                const uint32_t target_height = 480;
                const int frame_width =
                    (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
                const int frame_height =
                    (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);
                if (SetWindowPos(hwnd, nullptr, 0, 0, static_cast<int>(target_width) + frame_width,
                                 static_cast<int>(target_height) + frame_height,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
                {
                    window->poll_event();
                    int fb_width = 0;
                    int fb_height = 0;
                    window->get_frame_buffer_size(fb_width, fb_height);
                    if (fb_width == static_cast<int>(width) && fb_height == static_cast<int>(height))
                    {
                        std::printf("[WARN] window resize was not applied; skipping live resize check.\n");
                    }
                    else
                    {
                        device->resize_swapchain(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
                        uint32_t resized_width = 0;
                        uint32_t resized_height = 0;
                        device->get_swapchain_size(resized_width, resized_height);
                        if (resized_width != static_cast<uint32_t>(fb_width) ||
                            resized_height != static_cast<uint32_t>(fb_height))
                        {
                            std::printf("[TEST] resize_swapchain produced %ux%u, expected %dx%d\n", resized_width,
                                        resized_height, fb_width, fb_height);
                            return 1;
                        }
                        std::printf("[TEST] swapchain resize OK: %ux%u -> %ux%u.\n", width, height, resized_width,
                                    resized_height);
                    }
                }
                else
                {
                    std::printf("[WARN] SetWindowPos failed; skipping live resize check.\n");
                }
            }
        }
#endif
    }

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

    // V4：SPIR-V 工具链。shaders-vulkan/<name>/{vert,frag}.glsl 由 CMake 的
    // orca-shader-spirv 目标编译成 .spv；这里验证 create_shader_module 的
    // 源类型/入口点/stage 校验与销毁链路。
    {
        ShaderBinarySource triangle = ShaderLoader::load_spirv("triangle");
        ShaderBinarySource skybox = ShaderLoader::load_spirv("builtin-skybox");
        if (!triangle.is_valid() || !skybox.is_valid())
        {
            std::printf("[TEST] SPIR-V shaders are missing; run from the build directory.\n");
            return 1;
        }

        auto make_module_desc = [](const std::vector<uint8_t> &bytes, ShaderType type, const char *name) {
            ShaderModuleDesc desc;
            desc.name = name;
            desc.source.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            desc.source_kind = ShaderSourceKind::SPIRV;
            desc.entry_point = "main";
            desc.type = type;
            return desc;
        };

        const ShaderModuleDesc triangle_vertex = make_module_desc(triangle.vertex, ShaderType::VERTEX, "triangle.vert");
        const ShaderModuleDesc triangle_fragment =
            make_module_desc(triangle.fragment, ShaderType::FRAGMENT, "triangle.frag");
        const ShaderModuleDesc skybox_vertex =
            make_module_desc(skybox.vertex, ShaderType::VERTEX, "builtin-skybox.vert");
        const ShaderModuleDesc skybox_fragment =
            make_module_desc(skybox.fragment, ShaderType::FRAGMENT, "builtin-skybox.frag");

        ShaderHandle modules[4] = {
            device->create_shader_module(triangle_vertex),
            device->create_shader_module(triangle_fragment),
            device->create_shader_module(skybox_vertex),
            device->create_shader_module(skybox_fragment),
        };
        for (ShaderHandle module : modules)
        {
            if (!module.is_valid())
            {
                std::printf("[TEST] create_shader_module(SPIR-V) failed.\n");
                return 1;
            }
        }

        // 源类型/入口点/stage 不匹配必须在入口被拒绝（只打印 ERROR，不进入 driver）。
        ShaderModuleDesc glsl_desc = triangle_vertex;
        glsl_desc.name = "triangle.glsl-reject";
        glsl_desc.source = "#version 450\nvoid main() {}\n";
        glsl_desc.source_kind = ShaderSourceKind::GLSL_TEXT;
        if (device->create_shader_module(glsl_desc).is_valid())
        {
            std::printf("[TEST] create_shader_module accepted GLSL text for Vulkan.\n");
            return 1;
        }

        ShaderModuleDesc wrong_stage = triangle_fragment;
        wrong_stage.name = "triangle.wrong-stage";
        wrong_stage.type = ShaderType::VERTEX; // fragment SPIR-V 装进 vertex 槽
        if (device->create_shader_module(wrong_stage).is_valid())
        {
            std::printf("[TEST] create_shader_module accepted a stage mismatch.\n");
            return 1;
        }

        ShaderModuleDesc wrong_entry = triangle_vertex;
        wrong_entry.name = "triangle.wrong-entry";
        wrong_entry.entry_point = "not_an_entry_point";
        if (device->create_shader_module(wrong_entry).is_valid())
        {
            std::printf("[TEST] create_shader_module accepted a missing entry point.\n");
            return 1;
        }

        for (ShaderHandle module : modules)
            device->destroy_shader_module(module);
        std::printf("[TEST] SPIR-V shader modules OK (triangle + builtin-skybox).\n");
    }

    // V1：采样器映射（含 compare_op）+ destroy 链路。
    {
        SamplerHandle sampler = device->create_sampler(SamplerDesc{});
        if (!sampler.is_valid())
        {
            std::printf("[TEST] create_sampler failed.\n");
            return 1;
        }
        device->destroy_sampler(sampler);

        // 阴影 PCF 用的深度比较采样器：compareEnable + compareOp 必须都映射到位。
        SamplerDesc compare_desc;
        compare_desc.compare_enable = true;
        compare_desc.compare_op = CompareFunction::GreaterEqual;
        compare_desc.mag_filter = FilterMode::Nearest;
        compare_desc.min_filter = FilterMode::Nearest;
        compare_desc.address_u = AddressMode::ClampToEdge;
        compare_desc.address_v = AddressMode::ClampToEdge;
        SamplerHandle compare_sampler = device->create_sampler(compare_desc);
        if (!compare_sampler.is_valid())
        {
            std::printf("[TEST] create_sampler(compare) failed.\n");
            return 1;
        }
        device->destroy_sampler(compare_sampler);
        std::printf("[TEST] sampler create/destroy OK (default + compare).\n");
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

        // 完整 cubemap 视图（6 层）与 mip 子范围视图（base 1，count 2）
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

        TextureViewDesc cube_mip_view_desc;
        cube_mip_view_desc.dim = TextureViewDimension::Cube;
        cube_mip_view_desc.base_mip = 1;
        cube_mip_view_desc.mip_count = 2;
        TextureViewHandle cube_mip_view = device->create_texture_view(cube, cube_mip_view_desc);
        if (!cube_mip_view.is_valid())
        {
            std::printf("[TEST] create_texture_view(cube mip range) failed.\n");
            return 1;
        }
        device->destroy_texture_view(cube_mip_view);

        // 非法视图描述必须在入口被拒绝（只打印 [ERROR]，不能进入 driver）：
        // 1) cubemap 不能按 2D 查看；2) base_layer 不是 6 的倍数；
        // 3) layer_count 不是 6 的倍数；4) 深度纹理用 Color aspect；
        // 5) mip 范围越界；6) 无效父句柄。
        TextureViewDesc cube_as_2d;
        cube_as_2d.dim = TextureViewDimension::Texture2D;
        if (device->create_texture_view(cube, cube_as_2d).is_valid())
        {
            std::printf("[TEST] create_texture_view accepted a cube-as-2D view.\n");
            return 1;
        }

        TextureViewDesc misaligned_cube;
        misaligned_cube.dim = TextureViewDimension::Cube;
        misaligned_cube.base_layer = 3;
        if (device->create_texture_view(cube, misaligned_cube).is_valid())
        {
            std::printf("[TEST] create_texture_view accepted a misaligned cubemap base_layer.\n");
            return 1;
        }

        TextureViewDesc partial_cube;
        partial_cube.dim = TextureViewDimension::Cube;
        partial_cube.layer_count = 3;
        if (device->create_texture_view(cube, partial_cube).is_valid())
        {
            std::printf("[TEST] create_texture_view accepted a partial cubemap layer_count.\n");
            return 1;
        }

        TextureViewDesc mip_out_of_range;
        mip_out_of_range.dim = TextureViewDimension::Cube;
        mip_out_of_range.base_mip = 5; // 该纹理只有 5 层（0..4）
        if (device->create_texture_view(cube, mip_out_of_range).is_valid())
        {
            std::printf("[TEST] create_texture_view accepted an out-of-range mip.\n");
            return 1;
        }

        if (device->create_texture_view(TextureHandle{}, cube_view_desc).is_valid())
        {
            std::printf("[TEST] create_texture_view accepted an invalid texture handle.\n");
            return 1;
        }

        // 颜色纹理用 DepthOnly / 深度纹理用 Color 都必须被拒绝
        TextureViewDesc depth_as_color;
        depth_as_color.aspect = TextureAspect::Color;
        depth_desc.label = "smoke-depth-reject";
        TextureHandle depth_for_view = device->create_texture(depth_desc);
        if (!depth_for_view.is_valid())
        {
            std::printf("[TEST] create_texture(depth-view-reject) failed.\n");
            return 1;
        }
        if (device->create_texture_view(depth_for_view, depth_as_color).is_valid())
        {
            std::printf("[TEST] create_texture_view accepted a depth texture with Color aspect.\n");
            return 1;
        }
        device->destroy_texture(depth_for_view);

        device->destroy_texture(cube);

        std::printf("[TEST] texture view create/range validation OK (2D + depth + cubemap mip range).\n");
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

    // V2-1：create_buffer + queue_write_buffer（staging → vkCmdCopyBuffer）。
    {
        // 1) 顶点缓冲：整段写入。
        GPUBufferDesc vertex_desc;
        vertex_desc.label = "smoke-vertex";
        vertex_desc.size = 4 * 1024;
        vertex_desc.usage = GPUBufferUsage::VERTEX | GPUBufferUsage::COPY_DST;
        GPUBufferHandle vertex_buffer = device->create_buffer(vertex_desc);
        if (!vertex_buffer.is_valid())
        {
            std::printf("[TEST] create_buffer(vertex) failed.\n");
            return 1;
        }
        std::vector<float> vertices(256 * 3);
        for (size_t i = 0; i < vertices.size(); ++i)
            vertices[i] = static_cast<float>(i);
        device->queue_write_buffer(vertex_buffer, 0, vertices.data(), vertices.size() * sizeof(float));

        // 2) 索引缓冲：26 字节容量，分两次写入（第二次带偏移），覆盖 2 字节整数倍
        //    的拷贝大小（vkCmdCopyBuffer 不要求 4 字节对齐，这里刻意用 12 字节验证）。
        GPUBufferDesc index_desc;
        index_desc.label = "smoke-index";
        index_desc.size = 24;
        index_desc.usage = GPUBufferUsage::INDEX | GPUBufferUsage::COPY_DST;
        GPUBufferHandle index_buffer = device->create_buffer(index_desc);
        if (!index_buffer.is_valid())
        {
            std::printf("[TEST] create_buffer(index) failed.\n");
            return 1;
        }
        const uint16_t indices[6] = {0, 1, 2, 2, 3, 0};
        device->queue_write_buffer(index_buffer, 0, indices, sizeof(indices));
        device->queue_write_buffer(index_buffer, 12, indices, sizeof(indices));

        // 3) uniform 缓冲：64 字节（UBO 常见尺寸）。
        GPUBufferDesc uniform_desc;
        uniform_desc.label = "smoke-uniform";
        uniform_desc.size = 64;
        uniform_desc.usage = GPUBufferUsage::UNIFORM | GPUBufferUsage::COPY_DST;
        GPUBufferHandle uniform_buffer = device->create_buffer(uniform_desc);
        if (!uniform_buffer.is_valid())
        {
            std::printf("[TEST] create_buffer(uniform) failed.\n");
            return 1;
        }
        float ubo[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        device->queue_write_buffer(uniform_buffer, 0, ubo, sizeof(ubo));

        // 4) 非法写入必须在入口被拒绝：越界区间、空指针、未带 COPY_DST 的缓冲区。
        device->queue_write_buffer(index_buffer, 24, indices, sizeof(indices));
        device->queue_write_buffer(index_buffer, 0, nullptr, sizeof(indices));

        GPUBufferDesc no_copy_desc;
        no_copy_desc.label = "smoke-buffer-no-copy";
        no_copy_desc.size = 64;
        no_copy_desc.usage = GPUBufferUsage::VERTEX;
        GPUBufferHandle no_copy_buffer = device->create_buffer(no_copy_desc);
        if (!no_copy_buffer.is_valid())
        {
            std::printf("[TEST] create_buffer(no-copy) failed.\n");
            return 1;
        }
        device->queue_write_buffer(no_copy_buffer, 0, ubo, sizeof(ubo)); // 期望打印 ERROR 后返回
        device->destroy_buffer(no_copy_buffer);

        // 5) 非法描述（size == 0 / usage 为空）必须在 create 入口被拒绝。
        if (device->create_buffer(GPUBufferDesc{}).is_valid())
        {
            std::printf("[TEST] create_buffer accepted an invalid description.\n");
            return 1;
        }

        std::printf("[TEST] buffer create/write OK (vertex + index with offset + uniform).\n");

        device->destroy_buffer(vertex_buffer);
        device->destroy_buffer(index_buffer);
        device->destroy_buffer(uniform_buffer);
        std::printf("[TEST] buffer write validation handled.\n");
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

        SamplerDesc nan_sampler;
        nan_sampler.lod_min_clamp = std::numeric_limits<float>::quiet_NaN();
        if (device->create_sampler(nan_sampler).is_valid())
        {
            std::printf("[TEST] create_sampler accepted a NaN lod clamp.\n");
            return 1;
        }

        SamplerDesc old_lod_sampler;
        old_lod_sampler.lod_max_clamp = 2000.0f; // 超过 VK_LOD_CLAMP_NONE
        if (device->create_sampler(old_lod_sampler).is_valid())
        {
            std::printf("[TEST] create_sampler accepted an out-of-range maxLod.\n");
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
