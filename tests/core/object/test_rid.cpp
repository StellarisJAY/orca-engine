#include "doctest/doctest.h"
#include "object/rid.h"

#include <cstdint>

namespace
{
    struct Payload
    {
        int value = 0;
        float weight = 0.0f;
    };
}

TEST_CASE("RIDUtil: packs and unpacks type / generation / index")
{
    const RID rid = RIDUtil::make(RIDType::Mesh, 12345, 7);

    CHECK(RIDUtil::get_type(rid) == RIDType::Mesh);
    CHECK(RIDUtil::get_index(rid) == 12345u);
    CHECK(RIDUtil::get_generation(rid) == 7);
    CHECK(RIDUtil::is_valid(rid));
}

TEST_CASE("RIDUtil: index and generation do not bleed into each other")
{
    const RID max_index = RIDUtil::make(RIDType::Camera, 0xFFFFFFFFu, 0xFFFF);
    CHECK(RIDUtil::get_type(max_index) == RIDType::Camera);
    CHECK(RIDUtil::get_index(max_index) == 0xFFFFFFFFu);
    CHECK(RIDUtil::get_generation(max_index) == 0xFFFF);
}

TEST_CASE("RID: zero is the invalid handle")
{
    CHECK_FALSE(RIDUtil::is_valid(RID(0)));
    CHECK_FALSE(RIDUtil::is_valid(RID()));
}

TEST_CASE("RIDStorage: create stores data and returns a typed handle")
{
    RIDStorage<Payload> storage(RIDType::Mesh);

    Payload payload;
    payload.value = 42;
    payload.weight = 1.5f;

    const RID rid = storage.create(payload);

    CHECK(RIDUtil::is_valid(rid));
    CHECK(RIDUtil::get_type(rid) == RIDType::Mesh);
    CHECK(RIDUtil::get_index(rid) == 0u);
    CHECK(RIDUtil::get_generation(rid) == 1);
    CHECK(storage.has(rid));
    CHECK(storage.get_count() == 1);

    Payload *stored = storage.get_data(rid);
    REQUIRE(stored != nullptr);
    CHECK(stored->value == 42);
    CHECK(stored->weight == 1.5f);

    // 通过句柄取到的指针可以直接改写。
    stored->value = 7;
    CHECK(storage.get_data(rid)->value == 7);
}

TEST_CASE("RIDStorage: rejects handles from a different type tag")
{
    RIDStorage<Payload> meshes(RIDType::Mesh);
    RIDStorage<Payload> cameras(RIDType::Camera);

    const RID mesh_rid = meshes.create(Payload{});

    CHECK(meshes.get_data(mesh_rid) != nullptr);
    CHECK(cameras.get_data(mesh_rid) == nullptr);
    CHECK_FALSE(cameras.has(mesh_rid));
}

TEST_CASE("RIDStorage: free invalidates the handle")
{
    RIDStorage<Payload> storage(RIDType::Mesh);
    const RID rid = storage.create(Payload{});

    storage.free(rid);

    CHECK_FALSE(storage.has(rid));
    CHECK(storage.get_data(rid) == nullptr);
    CHECK(storage.get_count() == 0);

    // 重复释放是空操作。
    storage.free(rid);
    CHECK(storage.get_count() == 0);
}

TEST_CASE("RIDStorage: freed index is reused with a new generation")
{
    RIDStorage<Payload> storage(RIDType::Mesh);

    const RID first = storage.create(Payload{});
    storage.free(first);
    const RID second = storage.create(Payload{});

    // 索引复用，但句柄不同（代际变化）。
    CHECK(RIDUtil::get_index(second) == RIDUtil::get_index(first));
    CHECK(RIDUtil::get_generation(second) != RIDUtil::get_generation(first));
    CHECK(second != first);

    // 旧句柄不会被复活。
    CHECK(storage.get_data(first) == nullptr);
    CHECK(storage.get_data(second) != nullptr);
    CHECK(storage.get_count() == 1);
}

TEST_CASE("RIDStorage: invalid / unknown handles are safe")
{
    RIDStorage<Payload> storage(RIDType::Mesh);

    CHECK(storage.get_data(RID()) == nullptr);
    CHECK(storage.get_data(RID(42)) == nullptr); // type 不匹配，且 generation 为 0
    CHECK_FALSE(storage.has(RID()));

    storage.free(RID());
    storage.free(RID(42));
    CHECK(storage.get_count() == 0);

    // 越界索引同样安全。
    const RID out_of_range = RIDUtil::make(RIDType::Mesh, 9999, 1);
    CHECK(storage.get_data(out_of_range) == nullptr);
    storage.free(out_of_range);
    CHECK(storage.get_count() == 0);
}

TEST_CASE("RIDStorage: clear resets indices and count")
{
    RIDStorage<Payload> storage(RIDType::Mesh);
    storage.create(Payload{});
    storage.create(Payload{});

    storage.clear();

    CHECK(storage.get_count() == 0);
    const RID rid = storage.create(Payload{});
    CHECK(RIDUtil::get_index(rid) == 0u);
    CHECK(RIDUtil::get_generation(rid) == 1);
}
