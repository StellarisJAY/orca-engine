#pragma once
#include <cstdint>
#include <vector>

using RID = uint32_t;

template<typename T>
class RIDStorage
{
    std::vector<RID> free_list;
    std::vector<T> data;
    std::vector<RID> generations;
public:
    T *get_data(RID rid) const;
    RID create(const T &data);
    void free(RID rid);
};