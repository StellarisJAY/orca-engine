#include "doctest/doctest.h"
#include "object/ref_counted.h"

#include <utility>

namespace
{
    struct TestObject : public RefCounted
    {
        static int instances;

        int value;

        explicit TestObject(int p_value = 0) : value(p_value) { instances++; }
        ~TestObject() override { instances--; }
    };

    int TestObject::instances = 0;
}

TEST_CASE("RefCounted: ref and unref")
{
    RefCounted *o = new RefCounted();
    CHECK(o->get_ref_count() == 1);

    o->ref();
    CHECK(o->get_ref_count() == 2);

    CHECK_FALSE(o->unref()); // 2 -> 1
    CHECK(o->get_ref_count() == 1);

    const bool deleted = o->unref(); // 1 -> 0，对象被删除
    CHECK(deleted);
}

TEST_CASE("Ref: takes ownership and releases on destruction")
{
    TestObject *o = new TestObject(42);
    CHECK(o->get_ref_count() == 1);

    {
        Ref<TestObject> r(o);
        CHECK(r.is_valid());
        CHECK_FALSE(r.is_null());
        CHECK(r.ptr() == o);
        CHECK(r->value == 42);
        CHECK(r == o);
        CHECK_FALSE(r != o);
        CHECK(o->get_ref_count() == 1);
        CHECK(TestObject::instances == 1);
    }

    // r 析构后计数归零，对象被释放
    CHECK(TestObject::instances == 0);
}

TEST_CASE("Ref: copy construction increments the count")
{
    Ref<TestObject> a = new TestObject(1);
    CHECK(a->get_ref_count() == 1);

    {
        Ref<TestObject> b(a);
        CHECK(a->get_ref_count() == 2);
        CHECK(b.ptr() == a.ptr());
        CHECK(b->value == 1);

        Ref<TestObject> c = b;
        CHECK(a->get_ref_count() == 3);
    }

    CHECK(a->get_ref_count() == 1);
    CHECK(TestObject::instances == 1);
}

TEST_CASE("Ref: copy assignment releases the old reference")
{
    Ref<TestObject> a = new TestObject(1);
    Ref<TestObject> b = new TestObject(2);
    TestObject *pa = a.ptr();

    CHECK(TestObject::instances == 2);

    b = a;
    CHECK(b.ptr() == pa);
    CHECK(a->get_ref_count() == 2);
    CHECK(TestObject::instances == 1); // 原来的 b 被释放

    // 自赋值是安全的
    Ref<TestObject> &alias = a;
    a = alias;
    CHECK(a.ptr() == pa);
    CHECK(a->get_ref_count() == 2);
}

TEST_CASE("Ref: move construction transfers ownership")
{
    Ref<TestObject> a = new TestObject(7);
    TestObject *p = a.ptr();
    CHECK(p->get_ref_count() == 1);

    Ref<TestObject> b(std::move(a));
    CHECK(b.ptr() == p);
    CHECK(a.is_null());
    CHECK(p->get_ref_count() == 1);
    CHECK(TestObject::instances == 1);
}

TEST_CASE("Ref: move assignment transfers ownership")
{
    Ref<TestObject> a = new TestObject(1);
    Ref<TestObject> b = new TestObject(2);
    TestObject *pa = a.ptr();

    b = std::move(a);
    CHECK(b.ptr() == pa);
    CHECK(a.is_null());
    CHECK(pa->get_ref_count() == 1);
    CHECK(TestObject::instances == 1); // 原来的 b 被释放
}

TEST_CASE("Ref: null reference")
{
    Ref<TestObject> n(nullptr);
    CHECK(n.is_null());
    CHECK_FALSE(n.is_valid());
    CHECK(n.ptr() == nullptr);
    CHECK(n == nullptr);
    CHECK_FALSE(n != nullptr);

    // 拷贝空引用不应崩溃
    Ref<TestObject> copy(n);
    CHECK(copy.is_null());
    CHECK(TestObject::instances == 0);
}

TEST_CASE("Ref: unref releases and nulls")
{
    Ref<TestObject> r = new TestObject(1);
    CHECK(TestObject::instances == 1);

    r.unref();
    CHECK(r.is_null());
    CHECK(TestObject::instances == 0);

    // 重复 unref 是空操作
    r.unref();
    CHECK(r.is_null());
}
