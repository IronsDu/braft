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

// braft/compat/timer.cpp
// 标准库实现的定时器

#include "braft/compat/timer.h"

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <map>

namespace braft {
namespace compat {

// 定时器任务
struct TimerTask {
    int64_t execute_time_ms;  // 执行时间（绝对时间）
    TimerCallback callback;
    void* arg;
    uint64_t timer_id;  // 定时器唯一 ID
    bool cancelled;     // 是否已取消

    TimerTask()
        : execute_time_ms(0)
        , callback(NULL)
        , arg(NULL)
        , timer_id(0)
        , cancelled(false) {}

    TimerTask(int64_t t, TimerCallback cb, void* a, uint64_t id)
        : execute_time_ms(t)
        , callback(cb)
        , arg(a)
        , timer_id(id)
        , cancelled(false) {}

    // 优先级队列需要 operator>（最小堆）
    bool operator>(const TimerTask& other) const {
        if (execute_time_ms != other.execute_time_ms) {
            return execute_time_ms > other.execute_time_ms;
        }
        return timer_id > other.timer_id;
    }
};

// 标准库实现的定时器管理器
class StdTimerManager : public ITimerManager {
public:
    StdTimerManager()
        : _started(false)
        , _stopped(false)
        , _thread(NULL)
        , _next_timer_id(0) {}

    ~StdTimerManager() {
        if (_started) {
            stop();
            join();
        }
    }

    virtual int add_timer(int64_t abstime_ms,
                          TimerCallback callback,
                          void* arg,
                          TimerId* out_id) override {
        if (!callback || !out_id) {
            return -1;
        }

        std::unique_lock<std::mutex> lock(_mutex);

        if (_stopped.load(std::memory_order_acquire)) {
            return -1;
        }

        // 创建新的定时器 ID
        uint64_t timer_id = ++_next_timer_id;

        // 创建定时器任务
        TimerTask task(abstime_ms, callback, arg, timer_id);

        // 存储定时器
        _timers.push(task);
        _timer_map[timer_id] = task;

        // 设置输出 ID
        out_id->internal_id = reinterpret_cast<void*>(timer_id);

        // 通知工作线程
        _cond.notify_one();

        return 0;
    }

    virtual int cancel(const TimerId& id) override {
        if (!id.is_valid()) {
            return -1;
        }

        std::unique_lock<std::mutex> lock(_mutex);

        uint64_t timer_id = reinterpret_cast<uint64_t>(id.internal_id);
        auto it = _timer_map.find(timer_id);
        if (it != _timer_map.end()) {
            it->second.cancelled = true;
        }

        return 0;
    }

    virtual int start() override {
        if (_started) {
            return -1;
        }

        _stopped.store(false, std::memory_order_relaxed);

        // 启动工作线程
        _thread = new std::thread([this]() { this->worker_loop(); });

        _started = true;
        return 0;
    }

    virtual int stop() override {
        if (!_started) {
            return -1;
        }

        std::unique_lock<std::mutex> lock(_mutex);
        _stopped.store(true, std::memory_order_release);
        _cond.notify_all();

        return 0;
    }

    virtual int join() override {
        if (!_started || !_thread) {
            return -1;
        }

        if (_thread->joinable()) {
            _thread->join();
        }

        delete _thread;
        _thread = NULL;
        _started = false;

        return 0;
    }

    // 检查定时器是否存在并可取消
    bool has_timer_id(uint64_t timer_id) {
        std::unique_lock<std::mutex> lock(_mutex);
        return _timer_map.find(timer_id) != _timer_map.end();
    }

private:
    void worker_loop() {
        std::vector<TimerTask> ready_tasks;

        while (true) {
            ready_tasks.clear();

            {
                std::unique_lock<std::mutex> lock(_mutex);

                // 计算等待时间
                int64_t now_ms = current_time_ms();
                int64_t next_timeout = -1;

                if (!_timers.empty()) {
                    next_timeout = _timers.top().execute_time_ms - now_ms;
                    if (next_timeout < 0) {
                        next_timeout = 0;
                    }
                }

                // 等待定时器到期或停止信号
                if (next_timeout >= 0) {
                    _cond.wait_for(lock, std::chrono::milliseconds(next_timeout));
                } else {
                    _cond.wait(lock);
                }

                // 检查是否停止
                if (_stopped.load(std::memory_order_acquire) && _timers.empty()) {
                    break;
                }

                // 收集到期的定时器
                int64_t current_time = current_time_ms();
                while (!_timers.empty() && _timers.top().execute_time_ms <= current_time) {
                    ready_tasks.push_back(_timers.top());
                    _timers.pop();
                }
            }

            // 执行回调（不持有锁）
            for (const auto& task : ready_tasks) {
                // 从 map 中移除
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    _timer_map.erase(task.timer_id);
                }

                if (!task.cancelled && task.callback != NULL) {  // 未被取消
                    task.callback(task.arg);
                }
            }

            // 再次检查是否停止
            if (_stopped.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(_mutex);
                if (_timers.empty()) {
                    break;
                }
            }
        }

        // 清理剩余定时器
        std::unique_lock<std::mutex> lock(_mutex);
        _timer_map.clear();
        while (!_timers.empty()) {
            _timers.pop();
        }
    }

    // 获取当前时间（毫秒）- 使用 system_clock 以兼容 timespec
    static int64_t current_time_ms() {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    bool _started;
    std::atomic<bool> _stopped;
    std::thread* _thread;

    std::mutex _mutex;
    std::condition_variable _cond;
    std::priority_queue<TimerTask, std::vector<TimerTask>, std::greater<TimerTask>> _timers;
    std::map<uint64_t, TimerTask> _timer_map;  // 用于取消操作

    uint64_t _next_timer_id;
};

// 全局定时器管理器
static StdTimerManager* g_global_timer = NULL;
static std::mutex g_global_timer_mutex;

int init_global_timer() {
    std::lock_guard<std::mutex> lock(g_global_timer_mutex);

    if (g_global_timer != NULL) {
        return 0;  // 已初始化
    }

    g_global_timer = new StdTimerManager();
    if (g_global_timer->start() != 0) {
        delete g_global_timer;
        g_global_timer = NULL;
        return -1;
    }

    return 0;
}

int destroy_global_timer() {
    std::lock_guard<std::mutex> lock(g_global_timer_mutex);

    if (g_global_timer == NULL) {
        return 0;
    }

    g_global_timer->stop();
    g_global_timer->join();
    delete g_global_timer;
    g_global_timer = NULL;

    return 0;
}

int add_timer(TimerId* id,
              int64_t abstime_ms,
              TimerCallback callback,
              void* arg) {
    if (!id) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(g_global_timer_mutex);

    if (g_global_timer == NULL) {
        return -1;
    }

    return g_global_timer->add_timer(abstime_ms, callback, arg, id);
}

int cancel_timer(const TimerId& id) {
    std::lock_guard<std::mutex> lock(g_global_timer_mutex);

    if (g_global_timer == NULL) {
        return -1;
    }

    return g_global_timer->cancel(id);
}

// ============================================================================
// bthread_timer_t 兼容层实现
// ============================================================================

// 将 timespec 转换为毫秒
static int64_t timespec_to_ms(const struct timespec& ts) {
    return static_cast<int64_t>(ts.tv_sec) * 1000 +
           static_cast<int64_t>(ts.tv_nsec) / 1000000;
}

// bthread_timer_add 兼容实现
int bthread_timer_add(bthread_timer_t* id,
                      const struct timespec& abstime,
                      void (*on_timer)(void*),
                      void* arg) {
    if (!id || !on_timer) {
        return -1;
    }

    // 确保全局定时器已初始化
    if (g_global_timer == NULL) {
        if (init_global_timer() != 0) {
            return -1;
        }
    }

    // 转换时间格式
    int64_t abstime_ms = timespec_to_ms(abstime);

    // 使用 TimerId 作为临时存储
    TimerId timer_id;
    if (add_timer(&timer_id, abstime_ms, on_timer, arg) != 0) {
        return -1;
    }

    // 转换为 uint64_t ID
    *id = reinterpret_cast<uint64_t>(timer_id.internal_id);
    return 0;
}

// bthread_timer_del 兼容实现
// 返回值：
//   0 - 定时器存在且未执行（成功取消）
//   1 - 定时器正在执行或已完成
//   EINVAL (22) - 定时器不存在
int bthread_timer_del(bthread_timer_t id) {
    std::lock_guard<std::mutex> lock(g_global_timer_mutex);

    if (g_global_timer == NULL) {
        return 22;  // EINVAL
    }

    // 检查定时器是否存在于 map 中
    uint64_t timer_id = id;
    TimerId timer_id_struct;
    timer_id_struct.internal_id = reinterpret_cast<void*>(timer_id);

    // 尝试取消定时器
    int result = g_global_timer->cancel(timer_id_struct);

    if (result != 0) {
        // 取消失败，可能是定时器不存在或已执行
        return 22;  // EINVAL
    }

    // 返回 0 表示成功取消且未执行
    return 0;
}

}  // namespace compat
}  // namespace braft
