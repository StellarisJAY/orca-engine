#include "gl_executor.h"

#include "gl_rendering_device_refactor.h"

void GLExecutor::replay(GLRenderingDeviceRefactor &device, const GLCommandBuffer &buffer)
{
    // M3 实现：遍历 buffer.commands，按命令类型下发 GL 调用。
    // 当前为空实现，保证 M1 的录制链路可以独立验证。
    (void)device;
    (void)buffer;
}

void GLExecutor::flush_pending_writes(GLRenderingDeviceRefactor &device)
{
    // M5 实现：按调用顺序执行 pending_writes。
    (void)device;
}

void GLExecutor::flush_pending_destroys(GLRenderingDeviceRefactor &device)
{
    // M7 实现：提交完成后执行 pending_destroys。
    (void)device;
}
