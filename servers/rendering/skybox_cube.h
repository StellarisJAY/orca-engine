#pragma once

#include <cstdint>
#include <vector>
#include "math/vector3.h"

/// @brief 渲染天空盒时使用的默认立方体。
///
/// 天空盒只关心方向：顶点位置本身就会作为 cubemap 的采样方向传给
/// builtin-skybox 顶点着色器，因此这份几何数据只有 vertices 与 indices，
/// 没有法线、UV、切线、副切线。上传时对应参数传空 vector 即可，
/// 渲染设备会创建一个仅含 position 属性的 surface。
///
/// 立方体中心位于原点，半边长为 1，相机置于立方体内部即可看到整个天空盒。
class SkyboxCube
{
public:
    /// @brief 生成立方体几何数据。
    /// @param r_vertices 输出 8 个顶点（立方体的 8 个角）
    /// @param r_indices 输出 36 个索引（12 个三角形，共 6 个面）
    static void create_geometry(std::vector<Vector3> &r_vertices, std::vector<uint32_t> &r_indices);
};
