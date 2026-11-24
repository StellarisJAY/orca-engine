// glad 的实现部分（gladLoadGL / glad_gl* 函数定义）必须且只能出现在一个
// 翻译单元里。早期 OpenGL 渲染设备把 GLAD_GL_IMPLEMENTATION 写在自己的 .cpp 里，
// 多个目标文件同时链接时会产生 multiple definition 错误。
//
// 约定：
//   - 只有本文件定义 GLAD_GL_IMPLEMENTATION；
//   - 其它需要 GL 入口点的 .cpp（OpenGL 渲染设备、调试工具等）只 include
//     <glad/gl.h> 拿函数声明和 GL 类型，不再定义该宏。

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
