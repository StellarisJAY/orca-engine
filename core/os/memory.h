#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>

// 引擎统一的内存分配入口。
//
// 当前实现直接委托给 C++ 标准库堆（malloc/free + placement new），
// 不引入额外的分配逻辑、不添加分配头。所有引擎代码都应通过这些接口
// 分配/释放内存，这样可以：
//   1. 在未来把实现替换为带跟踪、池化或自定义分配器的版本，而无需改动上层；
//   2. 在调试期集中埋点，统计泄漏与分配次数。
//
// 命名与 Godot 保持一致，便于对照参考实现。
namespace Memory {

// 实时存活分配计数，仅供调试。
inline std::atomic<size_t> allocation_count{0};

// 分配未初始化的原始内存。失败时返回 nullptr。
inline void *alloc(size_t p_size) {
    void *mem = std::malloc(p_size);
    if (mem != nullptr) {
        allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
    return mem;
}

// 调整已分配内存的大小，原内容尽量保留。失败时返回 nullptr 且原内存不变。
inline void *realloc(void *p_mem, size_t p_size) {
    return std::realloc(p_mem, p_size);
}

// 释放 alloc/realloc 返回的内存。
inline void free(void *p_mem) {
    if (p_mem == nullptr) return;
    std::free(p_mem);
    allocation_count.fetch_sub(1, std::memory_order_relaxed);
}

inline size_t get_allocation_count() {
    return allocation_count.load(std::memory_order_relaxed);
}

// 内部：逆序析构一段数组内存。
template <typename T>
void destruct_array(T *p_ptr, size_t p_count) {
    while (p_count > 0) {
        --p_count;
        p_ptr[p_count].~T();
    }
}

} // namespace Memory

// ---- 原始内存 ----
#define memalloc(m_size) ::Memory::alloc(m_size)
#define memrealloc(m_mem, m_size) ::Memory::realloc(m_mem, m_size)
#define memfree(m_mem) ::Memory::free(m_mem)

// ---- 单个对象 ----
// 与 Godot 用法一致，支持两种写法：
//   memnew(Type)          -> 默认构造
//   memnew(Type(args...)) -> 带参构造
// sizeof(Type(args...)) 是合法的 sizeof 表达式，取值为 sizeof(Type)。
#define memnew(m_class) new (::Memory::alloc(sizeof(m_class))) m_class

// 在已分配的原始内存上做 placement new。
#define memnew_placement(m_placement, m_class) new (m_placement) m_class

// 析构并释放由 memnew 分配的对象。
// 注意：T 的析构函数若是虚函数，显式析构调用会正确派发到派生类。
template <typename T>
void memdelete(T *p_ptr) {
    if (p_ptr == nullptr) return;
    p_ptr->~T();
    ::Memory::free(p_ptr);
}

// ---- 数组 ----
// 分配并默认构造 p_count 个 T。元素为逆序析构。
template <typename T>
T *memnew_arr(size_t p_count) {
    if (p_count == 0) return nullptr;
    T *arr = static_cast<T *>(::Memory::alloc(sizeof(T) * p_count));
    if (arr == nullptr) return nullptr;
    for (size_t i = 0; i < p_count; i++) {
        new (arr + i) T();
    }
    return arr;
}

// 析构并释放由 memnew_arr 分配的数组。
// 与 Godot 不同，这里要求显式传入元素个数，以便非平凡类型能被正确析构。
template <typename T>
void memdelete_arr(T *p_ptr, size_t p_count) {
    if (p_ptr == nullptr) return;
    ::Memory::destruct_array(p_ptr, p_count);
    ::Memory::free(p_ptr);
}
