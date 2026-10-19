#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

/// @brief 资源句柄（Resource ID）。
///
/// RID 是一个 64 位整数，高位携带资源类型 tag，用于把不同种类的资源
/// 编码进同一个句柄空间，从而让统一的 free(RID) 能判断该去哪个 storage 释放，
/// 也避免不同 storage 各自从 0 开始的索引发生碰撞。
///
/// 位布局（自高到低）：
///   [63:48]  type tag   (16 bit)
///   [47:32]  generation (16 bit，0 保留表示“无效/初始”)
///   [31: 0]  index      (32 bit)
///
/// 0 是保留的无效 RID：generation 从 1 开始分配，因此任何有效 RID 都非 0。
using RID = uint64_t;

/// @brief 资源类型 tag。每个 RIDStorage 持有其中一种，用于隔离句柄。
enum class RIDType : uint16_t
{
    Unknown = 0,

    // servers/rendering
    Camera = 1,
    Scene = 2,
    Environment = 3,
    Mesh = 4,
    Instance = 5,
    Viewport = 6,

    // resource
    Material = 7,
    Texture = 8,

    Shader = 9,

    MaxType = 10,
};

/// @brief RID 的编码 / 解码工具。
namespace RIDUtil
{
constexpr uint32_t INDEX_BITS = 32;
constexpr uint32_t GENERATION_BITS = 16;
constexpr uint32_t GENERATION_SHIFT = INDEX_BITS;                    // 32
constexpr uint32_t TYPE_SHIFT = INDEX_BITS + GENERATION_BITS;        // 48

constexpr RID INDEX_MASK = 0xFFFFFFFFull;
constexpr RID GENERATION_MASK = 0xFFFFull;
constexpr RID TYPE_MASK = 0xFFFFull;

/// @brief 组装一个 RID。generation 为 0 的 RID 会被视为无效。
constexpr RID make(RIDType p_type, uint32_t p_index, uint16_t p_generation)
{
    return (static_cast<RID>(p_type) << TYPE_SHIFT) |
           (static_cast<RID>(p_generation) << GENERATION_SHIFT) |
           (static_cast<RID>(p_index) & INDEX_MASK);
}

constexpr RIDType get_type(RID p_rid)
{
    return static_cast<RIDType>((p_rid >> TYPE_SHIFT) & TYPE_MASK);
}

constexpr uint16_t get_generation(RID p_rid)
{
    return static_cast<uint16_t>((p_rid >> GENERATION_SHIFT) & GENERATION_MASK);
}

constexpr uint32_t get_index(RID p_rid)
{
    return static_cast<uint32_t>(p_rid & INDEX_MASK);
}

/// @brief 仅判断句柄是否为非 0（不保证它仍然存活）。
constexpr bool is_valid(RID p_rid) { return p_rid != 0; }

} // namespace RIDUtil

/// @brief 按类型持有资源数据的句柄表（RID owner）。
///
/// 负责索引分配 / 复用，并用 generation 识别悬空句柄（dangling handle）：
/// 一个 RID 被释放后，再次查询一定失败，即使它的索引后来被复用。
///
/// T 需要可默认构造（空闲槽位会被默认构造一次）。
template <typename T>
class RIDStorage
{
    struct Entry
    {
        T data{};
        uint16_t generation = 0;
        bool alive = false;
    };

public:
    explicit RIDStorage(RIDType p_type = RIDType::Unknown) : type(p_type) {}

    RIDType get_type() const { return type; }

    /// @brief 分配一个槽位并写入数据，返回新的有效 RID。
    RID create(const T &p_data)
    {
        uint32_t index;
        if (!free_list.empty())
        {
            index = free_list.back();
            free_list.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(entries.size());
            entries.emplace_back();
        }

        Entry &entry = entries[index];
        entry.alive = true;
        entry.generation = next_generation(entry.generation);
        entry.data = p_data;
        return RIDUtil::make(type, index, entry.generation);
    }

    /// @brief 取数据；RID 类型不符 / 越界 / 已释放 / 代际不符时返回 nullptr。
    T *get_data(RID p_rid)
    {
        Entry *entry = find(p_rid);
        return entry != nullptr ? &entry->data : nullptr;
    }

    const T *get_data(RID p_rid) const
    {
        const Entry *entry = find(p_rid);
        return entry != nullptr ? &entry->data : nullptr;
    }

    /// @brief RID 是否指向当前存活的槽位。
    bool has(RID p_rid) const { return find(p_rid) != nullptr; }

    /// @brief 释放槽位。重复释放 / 悬空句柄是安全的空操作。
    void free(RID p_rid)
    {
        if (RIDUtil::get_type(p_rid) != type) return;

        const uint32_t index = RIDUtil::get_index(p_rid);
        if (index >= entries.size()) return;

        Entry &entry = entries[index];
        if (!entry.alive || entry.generation != RIDUtil::get_generation(p_rid)) return;

        entry.alive = false;
        free_list.push_back(index);
    }

    /// @brief 当前存活槽位数量。
    size_t get_count() const { return entries.size() - free_list.size(); }

    /// @brief 遍历所有存活槽位，回调签名为 void(RID, T &)。
    ///
    /// 遍历期间不应对本 storage 进行 create/free/clear，否则下标会失效。
    /// 需要在遍历中增删时，请先把结果收集到 vector，遍历结束后再修改。
    template <typename Func>
    void for_each(Func &&p_func)
    {
        for (size_t i = 0; i < entries.size(); ++i)
        {
            Entry &entry = entries[i];
            if (!entry.alive) continue;
            p_func(RIDUtil::make(type, static_cast<uint32_t>(i), entry.generation), entry.data);
        }
    }

    /// @brief const 版本，回调签名为 void(RID, const T &)。
    template <typename Func>
    void for_each(Func &&p_func) const
    {
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const Entry &entry = entries[i];
            if (!entry.alive) continue;
            p_func(RIDUtil::make(type, static_cast<uint32_t>(i), entry.generation), entry.data);
        }
    }

    /// @brief 遍历所有存活 data 的迭代器，解引用得到 T*（const 版得到 const T*）。
    /// 自动跳过已释放的槽位。
    ///
    /// 用法：for (InstanceData *data : storage.instances) { ... }
    /// 注意：遍历期间不要 create/free，否则迭代器会失效。
    template <bool IsConst>
    class Iterator
    {
        using StoragePtr = std::conditional_t<IsConst, const RIDStorage *, RIDStorage *>;

        StoragePtr storage;
        size_t index;

        void skip_dead()
        {
            while (index < storage->entries.size() && !storage->entries[index].alive)
                ++index;
        }

    public:
        using value_type = std::conditional_t<IsConst, const T *, T *>;

        Iterator(StoragePtr p_storage, size_t p_index) : storage(p_storage), index(p_index)
        {
            skip_dead();
        }

        value_type operator*() const { return &storage->entries[index].data; }

        Iterator &operator++()
        {
            ++index;
            skip_dead();
            return *this;
        }

        bool operator==(const Iterator &p_other) const { return index == p_other.index; }
        bool operator!=(const Iterator &p_other) const { return index != p_other.index; }
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, entries.size()); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, entries.size()); }

    /// @brief 释放全部槽位；数据保留默认值，索引从 0 重新开始分配。
    void clear()
    {
        entries.clear();
        free_list.clear();
    }

private:
    /// generation 从 1 开始、递增、跳过 0（0 保留给无效 RID）。
    static uint16_t next_generation(uint16_t p_generation)
    {
        ++p_generation;
        if (p_generation == 0) p_generation = 1;
        return p_generation;
    }

    Entry *find(RID p_rid)
    {
        if (RIDUtil::get_type(p_rid) != type) return nullptr;

        const uint32_t index = RIDUtil::get_index(p_rid);
        if (index >= entries.size()) return nullptr;

        Entry &entry = entries[index];
        if (!entry.alive || entry.generation != RIDUtil::get_generation(p_rid)) return nullptr;
        return &entry;
    }

    const Entry *find(RID p_rid) const
    {
        if (RIDUtil::get_type(p_rid) != type) return nullptr;

        const uint32_t index = RIDUtil::get_index(p_rid);
        if (index >= entries.size()) return nullptr;

        const Entry &entry = entries[index];
        if (!entry.alive || entry.generation != RIDUtil::get_generation(p_rid)) return nullptr;
        return &entry;
    }

    RIDType type;
    std::vector<Entry> entries;
    std::vector<uint32_t> free_list;
};
