#pragma once

#include "gl_internal.h"

class GLRenderingDeviceRefactor;

/// @brief 命令回放器（M3 起逐步实现）。
///
/// 职责：
///   - 按顺序回放 GLCommandBuffer，把命令翻译成 GL 调用；
///   - 维护影子状态（避免冗余状态切换）；
///   - 管理 FBO / VAO 缓存、延迟销毁、排队中的写入。
class GLExecutor
{
public:
    /// @brief 回放一个命令缓冲。当前为占位实现，M3 开始填充。
    void replay(GLRenderingDeviceRefactor &device, const GLCommandBuffer &buffer);

    /// @brief 执行排队中的 queue_write_*（M5）。
    void flush_pending_writes(GLRenderingDeviceRefactor &device);

    /// @brief 执行延迟销毁（M7）。
    void flush_pending_destroys(GLRenderingDeviceRefactor &device);
};
