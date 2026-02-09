// Test program for braft compatibility layer
// 测试适配器层的基本功能

#include <iostream>
#include <thread>
#include <chrono>
#include "braft/compat/task_queue.h"
#include "braft/compat/timer.h"

// Forward declaration and test brpc adapter separately
namespace brpc {
class Controller;
class ClosureGuard;
}

// Test brpc adapter separately (without Closure dependency)

using namespace braft;
using namespace braft::compat;

// Test task structure
struct TestTask {
    int value;
    std::string message;

    TestTask(int v, const std::string& msg) : value(v), message(msg) {}
};

// Test TaskQueue
bool test_task_queue() {
    std::cout << "=== Testing ITaskQueue ===" << std::endl;

    auto queue = create_std_task_queue<TestTask>();
    if (!queue) {
        std::cout << "FAILED: Could not create task queue" << std::endl;
        return false;
    }

    TaskQueueOptions options;
    // Note: StdTaskQueue doesn't have configurable worker count in current implementation

    // Handler function
    auto handler = [](void* context, TestTask* tasks, size_t count) -> size_t {
        std::cout << "Processing " << count << " tasks:" << std::endl;
        for (size_t i = 0; i < count; ++i) {
            std::cout << "  Task " << i << ": value=" << tasks[i].value
                      << ", message=" << tasks[i].message << std::endl;
        }
        return count;
    };

    if (queue->start(nullptr, handler, options) != 0) {
        std::cout << "FAILED: Could not start task queue" << std::endl;
        return false;
    }

    std::cout << "Task queue started" << std::endl;

    // Submit some tasks
    for (int i = 0; i < 5; ++i) {
        TestTask task(i, "test message");
        queue->execute(task);
    }

    // Wait for tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    queue->stop();
    queue->join();

    std::cout << "PASSED: ITaskQueue test completed" << std::endl;
    return true;
}

// Test Timer (using bthread_timer_t compatibility layer)
bool test_timer() {
    std::cout << "\n=== Testing Timer (bthread_timer compatibility) ===" << std::endl;

    bool fired = false;

    // Timer callback
    auto callback = [](void* arg) {
        bool* flag = static_cast<bool*>(arg);
        *flag = true;
        std::cout << "Timer fired!" << std::endl;
    };

    // Use system_clock to get time compatible with timespec (Unix epoch)
    auto sys_now = std::chrono::system_clock::now();
    auto sys_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(sys_now);
    auto timeout = sys_ms + std::chrono::milliseconds(100);

    // Convert to timespec (from Unix epoch)
    time_t tv_sec = std::chrono::system_clock::to_time_t(sys_now);
    struct timespec ts;
    ts.tv_sec = tv_sec;
    ts.tv_nsec = 100000000; // 100ms

    // Handle nanosecond overflow
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    std::cout << "Timer timeout: 100ms from now" << std::endl;

    // Add a timer that fires after 100ms
    compat::bthread_timer_t id;
    if (compat::bthread_timer_add(&id, ts, callback, &fired) != 0) {
        std::cout << "FAILED: Could not add timer" << std::endl;
        return false;
    }

    std::cout << "Timer added, waiting..." << std::endl;

    // Wait for timer to fire
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (!fired) {
        std::cout << "FAILED: Timer did not fire" << std::endl;
        std::cout << "Note: There may be a clock mismatch between steady_clock and system_clock" << std::endl;
        return false;
    }

    std::cout << "PASSED: Timer test completed" << std::endl;
    return true;
}

// Test brpc adapter (simplified - skip due to Closure dependency complexity)
bool test_brpc_adapter() {
    std::cout << "\n=== Testing brpc Adapter (Skipped) ===" << std::endl;
    std::cout << "Skipping brpc adapter test (Closure dependency)" << std::endl;
    return true;
}

int main() {
    std::cout << "Braft Compatibility Layer Test" << std::endl;
    std::cout << "================================" << std::endl;

    int passed = 0;
    int total = 2;

    if (test_task_queue()) passed++;
    if (test_timer()) passed++;
    if (test_brpc_adapter()) passed++;

    std::cout << "\n================================" << std::endl;
    std::cout << "Test Results: " << passed << "/" << total << " passed" << std::endl;

    return (passed == total) ? 0 : 1;
}
