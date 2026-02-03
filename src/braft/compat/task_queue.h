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

// braft/compat/task_queue.h
// 抽象任务队列接口，用于替换 bthread::ExecutionQueue

#ifndef BRAFT_COMPAT_TASK_QUEUE_H
#define BRAFT_COMPAT_TASK_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>

namespace braft {
namespace compat {

// 任务处理器返回值
enum TaskQueueResult {
    TASK_QUEUE_SUCCESS = 0,
    TASK_QUEUE_FAILED = -1,
    TASK_QUEUE_STOPPED = -2,
};

// 任务处理器函数类型
// 参数：context - 上下文指针（如 this 指针）
//       tasks - 任务数组
//       count - 任务数量
// 返回：处理的任务数量（返回 0 表示停止处理）
template <typename T>
using TaskHandler = size_t (*)(void* context, T* tasks, size_t count);

// 任务队列选项
struct TaskQueueOptions {
    bool use_pthread;  // 是否使用 pthread（而非 bthread）
    size_t max_queue_size;  // 最大队列大小

    TaskQueueOptions()
        : use_pthread(false), max_queue_size(0) {}
};

// 任务队列抽象接口
// 用于替换 bthread::ExecutionQueue
//
// 使用方式：
// 1. 定义任务类型 T
// 2. 实现 TaskHandler<T> 函数来处理任务
// 3. 调用 start() 启动队列
// 4. 调用 execute() 提交任务
// 5. 调用 stop() 停止队列
template <typename T>
class ITaskQueue {
public:
    virtual ~ITaskQueue() {}

    // 启动任务队列
    // context: 上下文指针（如 this 指针），会传递给 handler
    // handler: 任务处理器函数
    // 返回：0 表示成功，-1 表示失败
    virtual int start(void* context, TaskHandler<T> handler) = 0;

    // 启动任务队列（带选项）
    // context: 上下文指针（如 this 指针），会传递给 handler
    // handler: 任务处理器函数
    // options: 队列配置选项
    // 返回：0 表示成功，-1 表示失败
    virtual int start(void* context, TaskHandler<T> handler, const TaskQueueOptions& options) = 0;

    // 提交任务到队列
    // task: 要提交的任务
    // 返回：0 表示成功，-1 表示队列已满或已停止
    virtual int execute(const T& task) = 0;

    // 提交紧急任务（高优先级）
    // task: 要提交的任务
    // 返回：0 表示成功，-1 表示队列已满或已停止
    virtual int execute_urgent(const T& task) = 0;

    // 停止任务队列（非阻塞）
    // 返回：0 表示成功，-1 表示失败
    virtual int stop() = 0;

    // 等待任务队列完全停止（阻塞）
    // 返回：0 表示成功，-1 表示失败
    virtual int join() = 0;

    // 检查队列是否已启动
    virtual bool is_started() const = 0;

    // 获取当前队列大小
    virtual size_t size() const = 0;
};

// 标准库实现的任务队列（头文件实现）
template <typename T>
class StdTaskQueue : public ITaskQueue<T> {
public:
    StdTaskQueue()
        : _started(false)
        , _stopped(false)
        , _context(NULL)
        , _handler(NULL)
        , _thread(NULL)
        , _max_queue_size(0) {
    }

    ~StdTaskQueue() {
        if (_started) {
            stop();
            join();
        }
    }

    virtual int start(void* context, TaskHandler<T> handler) override {
        TaskQueueOptions options;
        return start(context, handler, options);
    }

    virtual int start(void* context, TaskHandler<T> handler, const TaskQueueOptions& options) override {
        if (_started) {
            return TASK_QUEUE_FAILED;
        }

        _context = context;
        _handler = handler;
        _max_queue_size = options.max_queue_size;

        // 启动工作线程
        _stopped.store(false, std::memory_order_relaxed);
        _thread = new std::thread([this]() { this->worker_loop(); });

        _started = true;
        return TASK_QUEUE_SUCCESS;
    }

    virtual int execute(const T& task) override {
        std::unique_lock<std::mutex> lock(_mutex);

        if (_stopped.load(std::memory_order_acquire)) {
            return TASK_QUEUE_STOPPED;
        }

        // 检查队列大小限制
        if (_max_queue_size > 0 && _normal_queue.size() >= _max_queue_size) {
            return TASK_QUEUE_FAILED;
        }

        _normal_queue.push(task);
        _cond.notify_one();

        return TASK_QUEUE_SUCCESS;
    }

    virtual int execute_urgent(const T& task) override {
        std::unique_lock<std::mutex> lock(_mutex);

        if (_stopped.load(std::memory_order_acquire)) {
            return TASK_QUEUE_STOPPED;
        }

        // 紧急任务没有队列大小限制
        _urgent_queue.push(task);
        _cond.notify_one();

        return TASK_QUEUE_SUCCESS;
    }

    virtual int stop() override {
        if (!_started) {
            return TASK_QUEUE_FAILED;
        }

        std::unique_lock<std::mutex> lock(_mutex);
        _stopped.store(true, std::memory_order_release);
        _cond.notify_all();

        return TASK_QUEUE_SUCCESS;
    }

    virtual int join() override {
        if (!_started || !_thread) {
            return TASK_QUEUE_FAILED;
        }

        if (_thread->joinable()) {
            _thread->join();
        }

        delete _thread;
        _thread = NULL;
        _started = false;

        return TASK_QUEUE_SUCCESS;
    }

    virtual bool is_started() const override {
        return _started;
    }

    virtual size_t size() const override {
        std::unique_lock<std::mutex> lock(_mutex);
        return _normal_queue.size() + _urgent_queue.size();
    }

private:
    void worker_loop() {
        std::vector<T> batch;

        while (true) {
            batch.clear();

            {
                std::unique_lock<std::mutex> lock(_mutex);

                // 等待任务或停止信号
                _cond.wait(lock, [this]() {
                    return !_urgent_queue.empty() ||
                           !_normal_queue.empty() ||
                           _stopped.load(std::memory_order_acquire);
                });

                // 处理紧急任务
                while (!_urgent_queue.empty()) {
                    batch.push_back(_urgent_queue.front());
                    _urgent_queue.pop();
                }

                // 处理普通任务
                while (!_normal_queue.empty() && batch.size() < 1024) {
                    batch.push_back(_normal_queue.front());
                    _normal_queue.pop();
                }

                // 如果已停止且没有任务，退出
                if (batch.empty() && _stopped.load(std::memory_order_acquire)) {
                    break;
                }
            }

            // 执行任务处理器（不持有锁）
            if (!batch.empty() && _handler != NULL) {
                size_t processed = _handler(_context, batch.data(), batch.size());
                // processed 可以用于处理部分任务的情况
                (void)processed;
            }
        }
    }

    bool _started;
    std::atomic<bool> _stopped;
    void* _context;  // 上下文指针
    TaskHandler<T> _handler;
    std::thread* _thread;
    size_t _max_queue_size;

    mutable std::mutex _mutex;  // mutable for const methods
    std::condition_variable _cond;
    std::queue<T> _normal_queue;
    std::queue<T> _urgent_queue;
};

// 工厂函数：创建标准库任务队列
template <typename T>
inline ITaskQueue<T>* create_std_task_queue() {
    return new StdTaskQueue<T>();
}

}  // namespace compat
}  // namespace braft

#endif  // BRAFT_COMPAT_TASK_QUEUE_H
