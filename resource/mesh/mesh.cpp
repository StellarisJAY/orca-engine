#include "mesh.h"
#include "os/memory.h"

Ref<Resource> Mesh::duplicate() const
{
    Mesh *copy = memnew(Mesh);
    copy->surfaces = surfaces;
    copy->rid_dirty = true;
    return Ref<Resource>(copy);
}

Mesh::~Mesh()
{
    // RID 由渲染服务器持有并释放；资源模块不依赖渲染层，这里保持空实现。
}
