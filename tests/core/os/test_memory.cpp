#include "doctest/doctest.h"
#include "os/memory.h"

namespace
{
    struct Tracker
    {
        static int constructions;
        static int destructions;

        int value;

        explicit Tracker(int p_value = 0) : value(p_value) { constructions++; }
        ~Tracker() { destructions++; }
    };

    int Tracker::constructions = 0;
    int Tracker::destructions = 0;

    void reset_tracker()
    {
        Tracker::constructions = 0;
        Tracker::destructions = 0;
    }

    int g_derived_destroyed = 0;

    struct Base
    {
        virtual ~Base() = default;
        virtual int id() const { return 0; }
    };

    struct Derived : public Base
    {
        int id() const override { return 1; }
        ~Derived() override { g_derived_destroyed++; }
    };
}

TEST_CASE("Memory: memnew/memdelete call constructor and destructor")
{
    reset_tracker();

    Tracker *t = memnew(Tracker(5));
    CHECK(t != nullptr);
    CHECK(t->value == 5);
    CHECK(Tracker::constructions == 1);
    CHECK(Tracker::destructions == 0);

    memdelete(t);
    CHECK(Tracker::destructions == 1);
}

TEST_CASE("Memory: memnew default construction")
{
    reset_tracker();

    Tracker *t = memnew(Tracker);
    CHECK(t->value == 0);
    CHECK(Tracker::constructions == 1);

    memdelete(t);
    CHECK(Tracker::destructions == 1);
}

TEST_CASE("Memory: memdelete(nullptr) is a no-op")
{
    reset_tracker();

    Tracker *t = nullptr;
    memdelete(t);

    CHECK(Tracker::constructions == 0);
    CHECK(Tracker::destructions == 0);
}

TEST_CASE("Memory: memdelete dispatches through a base pointer")
{
    g_derived_destroyed = 0;

    Base *b = memnew(Derived);
    CHECK(b->id() == 1);

    memdelete(b);
    CHECK(g_derived_destroyed == 1);
}

TEST_CASE("Memory: memalloc/memfree")
{
    void *p = memalloc(64);
    CHECK(p != nullptr);
    memfree(p);

    // free(nullptr) 是空操作
    memfree(nullptr);
}

TEST_CASE("Memory: allocation count tracks live blocks")
{
    const size_t before = Memory::get_allocation_count();

    void *a = memalloc(16);
    void *b = memalloc(16);
    CHECK(Memory::get_allocation_count() == before + 2);

    memfree(a);
    memfree(b);
    CHECK(Memory::get_allocation_count() == before);
}

TEST_CASE("Memory: memnew_placement constructs in place")
{
    reset_tracker();

    void *raw = memalloc(sizeof(Tracker));
    Tracker *t = memnew_placement(raw, Tracker(9));
    CHECK(t->value == 9);
    CHECK(Tracker::constructions == 1);

    // placement new 所在内存仍需自行析构并释放
    memdelete(t);
    CHECK(Tracker::destructions == 1);
}

TEST_CASE("Memory: memnew_arr/memdelete_arr")
{
    reset_tracker();

    Tracker *arr = memnew_arr<Tracker>(3);
    CHECK(arr != nullptr);
    CHECK(Tracker::constructions == 3);
    CHECK(Tracker::destructions == 0);

    memdelete_arr(arr, 3);
    CHECK(Tracker::destructions == 3);
}

TEST_CASE("Memory: memnew_arr with zero count returns null")
{
    Tracker *arr = memnew_arr<Tracker>(0);
    CHECK(arr == nullptr);
}
