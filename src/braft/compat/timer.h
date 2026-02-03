// Copyright (c) 2026  Braft.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// braft/compat/timer.h
// 抽象定时器接口，用于替换 bthread_timer_t

#ifndef BRAFT_COMPAT_TIMER_H
#define BRAFT_COMPAT_TIMER_H

#include <stdint.h>
#include <cstddef>  // for NULL
#include <time.h>   // for timespec, timespec_get

namespace braft {
namespace compat {

// 定时器 ID 类型（ opaque 类型，用于替换 bthread_timer_t）
struct TimerId {
    void* internal_id;

    TimerId() : internal_id(NULL) {}
    explicit TimerId(void* id) : internal_id(id) {}

    bool is_valid() const { return internal_id != NULL; }
    void reset() { internal_id = NULL; }

    bool operator==(const TimerId& other) const {
        return internal_id == other.internal_id;
    }
};

// 定时器回调函数类型
// arg: 用户参数
using TimerCallback = void (*)(void* arg);

// 定时器抽象接口
// 用于替换 bthread_timer_add 等函数
//
// 使用方式：
// 1. 调用 add_timer() 添加定时器，获得 TimerId
// 2. 到达指定时间后，回调函数会被调用
// 3. 如果需要取消定时器，调用 cancel()
class ITimerManager {
public:
    virtual ~ITimerManager() {}

    // 添加定时器
    // abstime: 绝对时间（从纪元开始的毫秒数）
    // callback: 回调函数
    // arg: 回调参数
    // out_id: 输出定时器 ID
    // 返回：0 表示成功，-1 表示失败
    virtual int add_timer(int64_t abstime_ms,
                          TimerCallback callback,
                          void* arg,
                          TimerId* out_id) = 0;

    // 取消定时器
    // id: 要取消的定时器 ID
    // 返回：0 表示成功，-1 表示失败或定时器不存在
    virtual int cancel(const TimerId& id) = 0;

    // 启动定时器管理器
    // 返回：0 表示成功，-1 表示失败
    virtual int start() = 0;

    // 停止定时器管理器
    // 返回：0 表示成功，-1 表示失败
    virtual int stop() = 0;

    // 等待定时器管理器完全停止
    // 返回：0 表示成功，-1 表示失败
    virtual int join() = 0;
};

// 全局定时器辅助函数（保持与 bthread_timer_t 兼容的接口）

// 添加定时器
// id: 输出定时器 ID
// abstime: 绝对时间（从纪元开始的毫秒数）
// callback: 回调函数
// arg: 回调参数
// 返回：0 表示成功，-1 表示失败
int add_timer(TimerId* id,
              int64_t abstime_ms,
              TimerCallback callback,
              void* arg);

// 取消定时器
// id: 要取消的定时器 ID
// 返回：0 表示成功，-1 表示失败
int cancel_timer(const TimerId& id);

// 初始化全局定时器管理器
// 返回：0 表示成功，-1 表示失败
int init_global_timer();

// 销毁全局定时器管理器
// 返回：0 表示成功，-1 表示失败
int destroy_global_timer();

// ============================================================================
// bthread_timer_t 兼容层 API
// 以下 API 提供 bthread_timer_t 的兼容接口，可以直接替换 bthread_timer
// ============================================================================

// 定义 bthread_timer_t 类型（与 bthread 保持一致）
using bthread_timer_t = uint64_t;

// bthread_timer_add 的兼容实现
// 在 abstime 指定的绝对时间调用 on_timer(arg)
// id: 输出定时器 ID
// abstime: 绝对时间（timespec 格式）
// on_timer: 回调函数，签名为 void (*)(void*)
// arg: 传递给回调的参数
// 返回值：0 表示成功，其他值表示失败（errno）
int bthread_timer_add(bthread_timer_t* id,
                      const struct timespec& abstime,
                      void (*on_timer)(void*),
                      void* arg);

// bthread_timer_del 的兼容实现
// 取消与 id 关联的定时器
// 返回值：
//   0 - 定时器存在且未执行（成功取消）
//   1 - 定时器正在执行或已完成
//   EINVAL (22) - 定时器不存在
int bthread_timer_del(bthread_timer_t id);

}  // namespace compat
}  // namespace braft

#endif  // BRAFT_COMPAT_TIMER_H
