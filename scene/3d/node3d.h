#pragma once
#include "node.h"
#include "math/vector3.h"
#include "math/transform_3d.h"
#include "math/math_defs.h"

class World3D;

class Node3D : public Node
{
private:
    EulerOrder euler_rotation_order = EulerOrder::YXZ;
    Vector3 rotation = Vector3::ZERO;
    Vector3 position = Vector3::ZERO;
    Vector3 scale = Vector3::ONES;
    Transform3D global_transform;
    Transform3D local_transform;
    bool visible = true;

    void transform();
    void _update_global_transform();
    void _propagate_transform();

public:
    Node3D() {}
    Transform3D get_parent_global_transform() const;
    Transform3D get_global_transform() const;
    /// @brief 局部变换
    Transform3D get_transform() const;
    Vector3 get_position() const;
    Vector3 get_rotation() const;
    real_t get_rotation(Vector3::Axis axis) const;
    Vector3 get_scale() const;
    bool is_visible() const;

    void set_position(const Vector3 &position);
    void set_scale(const Vector3 &scale);
    /// @brief 直接设置局部变换（含任意旋转、负缩放的完整矩阵），并同步反解 position/rotation/scale。
    void set_transform(const Transform3D &p_transform);
    World3D *get_world() const;
    /// @brief 绕坐标轴旋转
    /// @param axis 坐标轴
    /// @param deg 角度
    void rotate(Vector3::Axis axis, real_t deg);
    
    /// @brief 绕任意向量旋转
    /// @param axis 轴向量
    /// @param deg 角度
    void rotate(const Vector3 axis, real_t deg);
    
    /// @brief 绕任意向量旋转
    /// @param axis 轴向量
    /// @param deg 角度
    void set_rotation(const Vector3 axis, real_t deg);
};