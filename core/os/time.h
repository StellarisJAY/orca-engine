#pragma once

#include <cstdint>

// 引擎时间工具。
//
// 区分两类时间，不要混用：
//   - 单调时间（steady）：由不随系统时间调整的时钟提供，
//     用于帧间隔 delta、性能计时、超时判断。
//       now() / get_ticks_usec() / get_ticks_msec()
//   - 系统挂钟时间（system）：用于日期、存档时间戳、日志时间。
//       get_unix_time()
//
// 全部为静态方法，无需实例化。
class Time {
public:
    // 单调时间，单位秒。其原点是实现定义的（steady_clock epoch），
    // 只能用于相减求 delta，不能当作日历时间。
    static double now();

    // 单调时钟，分别为微秒、毫秒。适合做高精度计时。
    static uint64_t get_ticks_usec();
    static uint64_t get_ticks_msec();

    // 系统挂钟时间，Unix 时间戳（秒，含小数部分）。
    static double get_unix_time();

    // 自进程内 Time 首次可用（静态初始化）以来经过的秒数。
    static double get_start_time();
};
