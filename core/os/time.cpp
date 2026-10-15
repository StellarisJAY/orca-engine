#include "os/time.h"

#include <chrono>

namespace {

// 单调时钟起点，进程内静态初始化时确定，仅用于 get_start_time()。
const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

} // namespace

double Time::now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

uint64_t Time::get_ticks_usec() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

uint64_t Time::get_ticks_msec() {
    return get_ticks_usec() / 1000;
}

double Time::get_unix_time() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

double Time::get_start_time() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now() - start_time).count();
}
