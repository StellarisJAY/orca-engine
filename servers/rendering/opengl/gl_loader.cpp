// glad 的实现部分（gladLoadGL / glad_gl* 函数定义）必须且只能出现在一个
// 翻译单元里。之前 gl_rendering_device.cpp 与 gl_rendering_device_refactor.cpp
// 各自定义了 GLAD_GL_IMPLEMENTATION，两个目标文件都在 orca-rendering 里，
// 一旦两个后端对象都被链接进来就会产生 multiple definition 错误。
//
// 约定：
//   - 只有本文件定义 GLAD_GL_IMPLEMENTATION；
//   - 其它需要 GL 入口点的 .cpp（OpenGL 渲染设备、调试工具等）只 include
//     <glad/gl.h> 拿函数声明和 GL 类型，不再定义该宏。

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
