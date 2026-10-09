#pragma once

#include <atomic>

class RefCounted
{
    std::atomic<uint32_t> refCount{1};
public:
    virtual ~RefCounted() = default;

    void ref()
    {
        refCount.fetch_add(1, std::memory_order_relaxed);
    }

    // 解引用，返回true表示引用为0，可以释放内存
    bool unref()
    {
        // 最后一个引用，删除指针
        if(refCount.fetch_sub(1 ,std::memory_order_relaxed) == 1)
        {
            delete this;
            return true;
        }
        return false;
    }

    uint32_t get_ref_count() const { return refCount.load(); }
};

template<typename T>
class Ref
{
    T *reference = nullptr;
public:
    bool operator==(const Ref<T> &other) const { return reference == other.reference; }
    bool operator!=(const Ref<T> &other) const { return reference != other.reference; }
    bool operator==(T *ptr) const { return reference == ptr; }
    bool operator!=(T *ptr) const { return reference != ptr; }

    T *operator->() const { return reference; }

    T *operator*() const { return reference; }

    T *ptr() const { return reference; }

    inline bool is_valid() const { return reference != nullptr; }
    inline bool is_null() const { return reference == nullptr; }

    void unref() 
    {
        if(reference != nullptr)
        {
            static_cast<RefCounted*>(reference)->unref();
            reference = nullptr;
        }
    }

    ~Ref() { unref(); }

    // 接管指针
    Ref(T *ptr) : reference(ptr) {}

    // 拷贝构造，需要增加计数
    Ref(const Ref<T> &other) : reference(other.reference)
    {
        if(reference != nullptr) static_cast<RefCounted*>(reference)->ref();
    }

    // 移动构造，引用转移，不需要增加计数
    Ref(Ref<T> &&moved) : reference(moved.reference)
    {
        moved.reference = nullptr;
    }

    // 拷贝赋值
    Ref<T> &operator=(const Ref<T> &copy) 
    {
        if(this != &copy)
        {
            // 新ref增加引用
            if(copy.reference != nullptr) static_cast<RefCounted*>(copy.reference)->ref();
            // 旧ref解引用
            if(reference != nullptr) static_cast<RefCounted*>(reference)->unref();
            reference = copy.reference;
        }
        return *this;
    }

    // 移动赋值
    Ref<T> &operator=(Ref<T> &&copy) 
    {
        if(this != &copy)
        {
            // 旧ref解引用
            if(reference != nullptr) static_cast<RefCounted*>(reference)->unref();
            reference = copy.reference;
            copy.reference = nullptr;
        }
        return *this;
    }
};