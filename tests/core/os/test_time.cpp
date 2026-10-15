#include "doctest/doctest.h"
#include "os/time.h"

TEST_CASE("Time: now() is monotonic non-decreasing")
{
    const double a = Time::now();
    const double b = Time::now();

    CHECK(b >= a);
    CHECK(a > 0.0); // steady_clock epoch 之前不会是 0 或负数
}

TEST_CASE("Time: tick counters are monotonic and coherent")
{
    const uint64_t usec1 = Time::get_ticks_usec();
    const uint64_t usec2 = Time::get_ticks_usec();
    CHECK(usec2 >= usec1);

    // 毫秒与微秒来自同一时钟，换算关系成立
    const uint64_t msec = Time::get_ticks_msec();
    CHECK(msec == Time::get_ticks_usec() / 1000);
}

TEST_CASE("Time: now() and get_ticks_usec() share the same clock")
{
    const double seconds = Time::now();
    const uint64_t usec = Time::get_ticks_usec();

    // 同一时钟的秒与微秒之差应在 1 秒以内（允许调用间隔与取整误差）
    const double diff = seconds - static_cast<double>(usec) / 1'000'000.0;
    CHECK(diff < 1.0);
    CHECK(diff > -1.0);
}

TEST_CASE("Time: get_unix_time() is a plausible wall-clock timestamp")
{
    // 2020-01-01 之后、尚未到达 2100 年
    const double unix_time = Time::get_unix_time();
    CHECK(unix_time > 1'577'836'800.0);
    CHECK(unix_time < 4'102'444'800.0);
}

TEST_CASE("Time: get_start_time() is small and non-negative")
{
    CHECK(Time::get_start_time() >= 0.0);
    CHECK(Time::get_start_time() < 60.0);
}
